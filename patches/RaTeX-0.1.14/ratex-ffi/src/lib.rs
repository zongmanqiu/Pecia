//! Pecia FFI 壳：RaTeX（KaTeX 的 Rust 移植）LaTeX 公式 → SVG 字符串。
//!
//! 输出字形轮廓化（embed_glyphs）的独立 SVG（KaTeX 字体已编译进二进制），
//! C++ 侧用 mmdr_svg_to_png（resvg）光栅化为 PNG——与 mmdr 共用同一份 resvg。
//!
//! 导出：
//!   - `pecia_ratex_svg`: LaTeX 公式 → SVG 字符串（调用方 free）
//!   - `pecia_ratex_free`: 释放 SVG 字符串
//! 线程安全：无全局可变状态（错误信息线程局部，见 ratex_get_last_error 惯例）。

use std::ffi::{c_char, CStr, CString};
use std::panic::catch_unwind;

use ratex_layout::{layout, to_display_list, LayoutOptions};
use ratex_parser::parse;
use ratex_svg::{render_to_svg, SvgOptions};
use ratex_types::math_style::MathStyle;

/// LaTeX 公式 → SVG 字符串（字形轮廓化，自包含，可被 resvg 直接光栅化）。
/// display_mode：0=行内($...$)，非 0=块级($$...$$)。
/// font_size：公式字号（与 ratex CLI --font-size 语义一致）。
/// 成功返回 0，*out 指向 NUL 结尾 UTF-8 SVG（用完必须 pecia_ratex_free）。
/// 失败返回 -1。
#[unsafe(no_mangle)]
pub extern "C" fn pecia_ratex_svg(
    latex: *const c_char,
    display_mode: i32,
    font_size: f64,
    out: *mut *mut c_char,
) -> i32 {
    let result = catch_unwind(|| {
        if latex.is_null() || out.is_null() {
            return Err("null pointer".to_string());
        }
        let latex_str = unsafe { CStr::from_ptr(latex) }.to_str().map_err(|e| e.to_string())?;
        let nodes = parse(latex_str).map_err(|e| format!("parse error: {e}"))?;
        let style = if display_mode == 0 { MathStyle::Text } else { MathStyle::Display };
        let options = LayoutOptions::default().with_style(style);
        let layout_box = layout(&nodes, &options);
        let display_list = to_display_list(&layout_box);
        let svg_opts = SvgOptions {
            font_size: if font_size > 0.0 { font_size } else { 16.0 },
            embed_glyphs: true,   // 字形轮廓化：SVG 自包含，resvg 无需 KaTeX 字体
            // 默认 padding=10（KaTeX 安全边距）会让公式 PNG 四周大量空白、
            // 占位远超内容（实测 a/b 内容 7x21 被包在 39x47 里）；
            // 置 0 又会让内容贴边（分数顶部顶到图片边界）。取小值 2.0：
            // 保留少量间隙，图片仍紧凑（行内公式与正文行高协调）。
            padding: 2.0,
            ..Default::default()
        };
        let svg = render_to_svg(&display_list, &svg_opts);
        let cs = CString::new(svg).map_err(|e| e.to_string())?;
        // SAFETY: out 非空（已检查），写入成功路径唯一一次。
        unsafe { *out = cs.into_raw() };
        Ok(())
    });
    match result {
        Ok(Ok(())) => 0,
        _ => -1,
    }
}

/// 释放 pecia_ratex_svg 返回的字符串。空指针为无害空操作。
#[unsafe(no_mangle)]
pub extern "C" fn pecia_ratex_free(s: *mut c_char) {
    if !s.is_null() {
        // SAFETY: s 必为 pecia_ratex_svg 的 CString::into_raw 产物，未重复释放。
        unsafe { drop(CString::from_raw(s)) };
    }
}
