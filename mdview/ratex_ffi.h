#pragma once
/* ratex_ffi.h - C ABI for RaTeX (KaTeX in Rust) via our ratex-ffi shell.
 *
 * Converts a LaTeX formula to a self-contained SVG string (glyph outlines,
 * KaTeX fonts embedded at build time). The SVG is rasterized by the
 * existing mmdr_svg_to_png (resvg) - no external process, no font files.
 */
#ifdef __cplusplus
extern "C" {
#endif

/* LaTeX formula -> SVG string. display_mode: 0 = inline, nonzero = block.
 * font_size: formula font size (matches ratex CLI --font-size semantics).
 * On success returns 0 and *out points to a NUL-terminated UTF-8 string
 * that must be freed with pecia_ratex_free(). On failure returns -1. */
int pecia_ratex_svg(const char* latex_utf8, int display_mode, double font_size, char** out);

/* Free a string returned by pecia_ratex_svg. Null is a no-op. */
void pecia_ratex_free(char* s);

#ifdef __cplusplus
}
#endif
