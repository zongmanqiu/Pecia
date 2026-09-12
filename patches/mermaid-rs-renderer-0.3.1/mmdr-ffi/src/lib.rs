//! mmdr FFI 壳：把 mermaid-rs-renderer（mmdr，含其内部 resvg/usvg 0.47）
//! 暴露为 C ABI，供 Pecia C++ 侧进程内调用。
//!
//! 导出：
//!   - `mmdr_mermaid_to_png`: Mermaid 文本 → RGBA 位图（进程内渲染，免 spawn 进程）
//!   - `mmdr_svg_to_png`:     任意 SVG → RGBA 位图（替代独立 resvg-capi，全项目仅此一份 resvg）
//!   - `mmdr_bitmap_free`:    释放位图
//!
//! 内存约定：位图由本库分配（Box），调用方用完必须调用 mmdr_bitmap_free。
//! 像素格式：straight（非预乘）RGBA，stride = width*4，行序自上而下。
//! 线程安全：无全局可变状态（mmdr 内部 once_cell 缓存线程安全），可多线程并发调用。

use std::ffi::{c_char, CStr};
use std::os::raw::c_void;
use std::panic::catch_unwind;
use std::sync::{Mutex, OnceLock};

use mermaid_rs_renderer::{render_with_options, RenderOptions};

#[repr(C)]
pub struct MmdrBitmap {
    pub pixels: *mut u8,
    pub width: i32,
    pub height: i32,
    pub stride: i32,
    handle: *mut c_void,
}

/// 位图内存分配器（Box，thin 指针；free 时按此还原）
struct BmpAlloc {
    data: Box<[u8]>,
}

/// 全局共享的 usvg 字体数据库：首次调用时加载系统字体，之后复用。
/// Windows 系统字体枚举较慢（~数百 ms），每张图都重建会严重拖慢渲染。
/// Options.fontdb 是 Arc<Database>：锁内克隆 Arc 后即可无锁解析。
fn shared_fontdb() -> &'static Mutex<std::sync::Arc<fontdb::Database>> {
    static DB: OnceLock<Mutex<std::sync::Arc<fontdb::Database>>> = OnceLock::new();
    DB.get_or_init(|| Mutex::new(std::sync::Arc::new(fontdb::Database::new())))
}

/// 确保字体索引已加载（首次或 clear 后懒加载；load 在锁内，仅一次）

/// 任意 SVG 字符串 → straight-RGBA 位图。
/// width/height ≤ 0 时使用 SVG 自然尺寸。
/// 系统字体通过共享 fontdb 加载（中文渲染支持，仅首次枚举）。
fn svg_to_bitmap(svg: &str, width: i32, height: i32) -> Result<MmdrBitmap, ()> {
    let db_arc = {
        let mut guard = shared_fontdb().lock().map_err(|_| ())?;
        if guard.len() == 0 {
            // 懒加载：首次或 mmdr_fontdb_clear() 后重建索引
            let mut db = fontdb::Database::new();
            db.load_system_fonts();
            *guard = std::sync::Arc::new(db);
        }
        guard.clone()
    };
    let opt = usvg::Options {
        fontdb: db_arc,
        ..Default::default()
    };
    let tree = usvg::Tree::from_str(svg, &opt).map_err(|_| ())?;
    let size = tree.size();
    let pw = if width > 0 {
        width as u32
    } else {
        size.width().ceil().max(1.0) as u32
    };
    let ph = if height > 0 {
        height as u32
    } else {
        size.height().ceil().max(1.0) as u32
    };
    let mut pixmap = resvg::tiny_skia::Pixmap::new(pw, ph).ok_or(())?;
    let mut pm = pixmap.as_mut();
    // 指定渲染尺寸时内容必须按比例缩放：默认 transform（单位矩阵）会把
    // 自然尺寸的内容原样画到目标画布，超出部分被裁剪（"截断"现象）。
    // 与旧 lunasvg 方案 renderToBitmap(rw, rh) 的自动缩放行为对齐。
    let scale_x = pw as f32 / size.width().max(1.0);
    let scale_y = ph as f32 / size.height().max(1.0);
    let transform = resvg::tiny_skia::Transform::from_scale(scale_x, scale_y);
    resvg::render(&tree, transform, &mut pm);

    let mut data = pixmap.data().to_vec();
    // tiny-skia 输出预乘 RGBA；FLTK Fl_RGB_Image 需要直通 RGBA，转换之。
    for px in data.chunks_exact_mut(4) {
        let a = px[3] as u32;
        if a != 0 && a != 255 {
            px[0] = ((px[0] as u32 * 255) / a).min(255) as u8;
            px[1] = ((px[1] as u32 * 255) / a).min(255) as u8;
            px[2] = ((px[2] as u32 * 255) / a).min(255) as u8;
        }
    }

    let alloc = Box::new(BmpAlloc {
        data: data.into_boxed_slice(),
    });
    let handle = Box::into_raw(alloc) as *mut c_void;
    // SAFETY: handle 刚由 Box::into_raw 产生，指向有效 BmpAlloc。
    let ptr = unsafe { (*(handle as *mut BmpAlloc)).data.as_mut_ptr() };
    Ok(MmdrBitmap {
        pixels: ptr,
        width: pw as i32,
        height: ph as i32,
        stride: (pw * 4) as i32,
        handle,
    })
}

/// 把 C 字符串读为 &str（空指针或非 UTF-8 → Err）
unsafe fn cstr_to_str<'a>(p: *const c_char) -> Result<&'a str, ()> {
    if p.is_null() {
        return Err(());
    }
    // SAFETY: 调用方保证 p 指向有效的 NUL 结尾 UTF-8 C 字符串，且调用期间存活。
    unsafe { CStr::from_ptr(p) }.to_str().map_err(|_| ())
}

/// 统一入口：捕获 panic（Rust panic 跨越 C 边界是 UB），返回 0/-1
fn run_ffi(f: impl FnOnce() -> Result<(), ()>) -> i32 {
    match catch_unwind(std::panic::AssertUnwindSafe(f)) {
        Ok(Ok(())) => 0,
        _ => -1,
    }
}

/// Mermaid 文本 → RGBA 位图。width/height ≤0 时用图表自然尺寸。
/// 成功返回 0，`out` 被填充（用完必须 mmdr_bitmap_free）。
#[unsafe(no_mangle)]
pub extern "C" fn mmdr_mermaid_to_png(
    mmd: *const c_char,
    width: i32,
    height: i32,
    out: *mut MmdrBitmap,
) -> i32 {
    run_ffi(|| {
        if out.is_null() {
            return Err(());
        }
        let md = unsafe { cstr_to_str(mmd)? };
        let svg = render_with_options(md, RenderOptions::default()).map_err(|_| ())?;
        let bmp = svg_to_bitmap(&svg, width, height)?;
        // SAFETY: out 非空（已检查），bmp 无别名。
        unsafe { *out = bmp };
        Ok(())
    })
}

/// 任意 SVG 文本 → RGBA 位图（内部 resvg/usvg，加载系统字体）。
/// 成功返回 0，`out` 被填充（用完必须 mmdr_bitmap_free）。
#[unsafe(no_mangle)]
pub extern "C" fn mmdr_svg_to_png(
    svg: *const c_char,
    width: i32,
    height: i32,
    out: *mut MmdrBitmap,
) -> i32 {
    run_ffi(|| {
        if out.is_null() {
            return Err(());
        }
        let s = unsafe { cstr_to_str(svg)? };
        let bmp = svg_to_bitmap(s, width, height)?;
        // SAFETY: out 非空（已检查），bmp 无别名。
        unsafe { *out = bmp };
        Ok(())
    })
}

/// 释放位图。空指针/已释放 → 空操作。
#[unsafe(no_mangle)]
pub extern "C" fn mmdr_bitmap_free(bmp: *mut MmdrBitmap) {
    if bmp.is_null() {
        return;
    }
    // SAFETY: bmp 非空；handle 必为 svg_to_bitmap 里 Box::into_raw 的产物，
    // 且 free 后 handle 置空，防 double-free。
    unsafe {
        if !(*bmp).handle.is_null() {
            drop(Box::from_raw((*bmp).handle as *mut BmpAlloc));
            (*bmp).handle = std::ptr::null_mut();
            (*bmp).pixels = std::ptr::null_mut();
        }
    }
}

/// 清空全局字体索引缓存（预览关闭时调用，释放系统字体枚举占用的内存）。
/// 在途渲染持有的旧 Arc 继续有效；下次渲染自动重新加载（首次稍慢）。
#[unsafe(no_mangle)]
pub extern "C" fn mmdr_fontdb_clear() {
    if let Ok(mut guard) = shared_fontdb().lock() {
        *guard = std::sync::Arc::new(fontdb::Database::new());
    }
}
