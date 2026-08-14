#pragma once
/* mmdr_ffi.h - C ABI for mermaid-rs-renderer (mmdr) FFI shell.
 *
 * Provides in-process rendering of Mermaid text and arbitrary SVG to
 * RGBA bitmaps. This is the ONLY resvg/usvg instance in the project
 * (mmdr's internal resvg 0.47 is linked through this library).
 *
 * Pixel format: straight (non-premultiplied) RGBA, stride = width*4,
 * rows top-to-bottom. Bitmaps are allocated by the library; the caller
 * MUST call mmdr_bitmap_free() when done. Thread-safe.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char* pixels; /* RGBA8, straight alpha, row-major */
    int width;
    int height;
    int stride;
    void* _handle; /* internal, do not touch */
} MmdrBitmap;

/* Render Mermaid text to RGBA bitmap. width/height <= 0 uses the
 * diagram's natural size. Returns 0 on success, -1 on failure. */
int mmdr_mermaid_to_png(const char* mermaid_utf8, int width, int height, MmdrBitmap* out);

/* Render arbitrary SVG text to RGBA bitmap (system fonts loaded).
 * width/height <= 0 uses the SVG's natural size. Returns 0/-1. */
int mmdr_svg_to_png(const char* svg_utf8, int width, int height, MmdrBitmap* out);

/* Release a bitmap returned by the functions above. Null/cleared
 * handles are no-ops. */
void mmdr_bitmap_free(MmdrBitmap* bmp);

/* Drop the cached system-font index (frees memory when the preview is
 * closed). In-flight renders keep their Arc; the next render reloads. */
void mmdr_fontdb_clear(void);

#ifdef __cplusplus
}
#endif
