# microtext

[![CI](https://github.com/LuxxxLucy/microtext/actions/workflows/ci.yml/badge.svg)](https://github.com/LuxxxLucy/microtext/actions/workflows/ci.yml)
![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![platform: macOS](https://img.shields.io/badge/platform-macOS-lightgrey.svg)
![C99](https://img.shields.io/badge/C-99-blue.svg)
![single-header](https://img.shields.io/badge/single--header-yes-brightgreen.svg)

A single-header C library for modern text: one UTF-8 string in, one laid-out RGBA bitmap out.
Bidi, complex shaping, CJK, color emoji, and font fallback come from the operating system's own text engine, so it looks the way native apps look.
It returns pixels and knows nothing about any GPU or window toolkit; upload the bitmap however you like.
It is `stb_truetype.h`'s ergonomics with the OS's typography, so it gains shaping, fallback, bidi, and color emoji a from-scratch rasterizer cannot reach.
macOS only (CoreText); Windows and Linux backends are planned, see [Backends](#backends).

Vendor it: drop `microtext.h` into your tree and `#define MICROTEXT_IMPLEMENTATION` in exactly one `.c` file. No build step, no submodule.

![The raylib demo: Latin, CJK, Korean, Arabic and Hebrew, color emoji, multi-font runs, small caps, and a wrapped paragraph, all rendered by microtext](assets/demo.png)

The `microtext + raylib` demo (`examples/demo_2_raylib.c`); every glyph is one microtext bitmap uploaded as a texture.
A headless variant, `make demo_1_showcase`, writes the same gallery to `output/showcase.png` with no window.

## Usage

```c
#define MICROTEXT_IMPLEMENTATION
#include "microtext.h"

mt_font *f = mt_font_open("Helvetica Neue", 40.0f);   // NULL family = system UI font
int w, h;
mt_metrics m;
unsigned char *rgba = mt_render(f, "Hello 你好 \U0001F3B5", -1,
                                (mt_color){20, 20, 20, 255}, &w, &h, &m);
// rgba is w*h 8-bit sRGB RGBA, straight alpha, top row first. Upload it, then:
mt_free(rgba);
mt_font_close(f);
```

```sh
clang -std=c99 yourfile.c -o yourapp \
    -framework CoreText -framework CoreGraphics -framework CoreFoundation
```

Every `mt_render` allocates a buffer you own and free; caching is the consumer's job (`examples/mt_raylib.h` shows a `Texture2D` cache).
Not thread-safe: the last-error slot and an internal sRGB color space are process-global, so shape on one thread and render the `mt_shaped` on another, or serialize.

## API

| Function | Purpose |
| --- | --- |
| `mt_font_open(family, px)` | Open a font by family name; `NULL` uses the system UI font. |
| `mt_font_open_styled(family, px, bold, italic)` | Open with a bold and/or italic style; a missing style falls back to regular. |
| `mt_font_open_memory(data, len, px)` | Open from an in-memory `.ttf`/`.otf`. |
| `mt_font_metrics(f)` | Vertical metrics (ascent, descent, height) from the font alone, no shaping. |
| `mt_font_close(f)` | Release a font. |
| `mt_measure(f, utf8, len)` | `mt_metrics` (advance and vertical metrics) without rendering. |
| `mt_render(f, utf8, len, color, &w, &h, &m)` | Lay out and rasterize a run to a malloc'd RGBA bitmap. |
| `mt_free(p)` | Free a rendered bitmap. |
| `mt_last_error()` | The error from the last call (`mt_error`), process-global. |

`len < 0` means NUL-terminated. `color` tints non-color glyphs; color emoji keep their own colors.

## Shaped handle

Hold an `mt_shaped` to measure then render the same run without shaping twice, or to render into a buffer you own (a glyph or line atlas).

| Function | Purpose |
| --- | --- |
| `mt_shape(f, utf8, len, color)` | Shape a run once; returns an owned handle, `color` baked in. |
| `mt_shaped_metrics(s)` | Its `mt_metrics`, including the pen origin. |
| `mt_shaped_size(s, &w, &h)` | The bitmap size `mt_shaped_render` will use. |
| `mt_shaped_render(s, dst, &w, &h, &m)` | Rasterize; non-`NULL` `dst` renders into your `w*h*4` buffer, else one is allocated. |
| `mt_shaped_free(s)` | Release the handle. |

A wrapped line (`mt_block_line`) is an `mt_shaped` too, but the block owns it: never `mt_shaped_free` a line.

## Rich text and wrapping

Build an `mt_text` from styled runs, then lay it out. Each line is an `mt_shaped`.

```c
mt_text *t = mt_text_new();
mt_text_run(t, "Bold ",      -1, bold, red, NULL);    // run: font, color, features
mt_text_run(t, "中文 world", -1, reg,  ink, "smcp");  // OpenType feature tags
mt_block *b = mt_text_wrap(t, 520.0f);                 // <= 0 wraps only at hard breaks
for (int i = 0; i < mt_block_lines(b); i++)
    mt_shaped_render(mt_block_line(b, i), /* ... */);  // each line renders like any run
mt_block_free(b);
mt_text_free(t);
```

| Function | Purpose |
| --- | --- |
| `mt_text_new()` | Start an empty paragraph. |
| `mt_text_run(t, utf8, len, f, color, features)` | Append a styled run; `features` is space-separated OpenType tags (`"smcp tnum"`) or `NULL`. |
| `mt_text_align(t, align)` | `MT_ALIGN_LEFT` (default), `RIGHT`, `CENTER`, `JUSTIFY`. Needs a positive wrap width. |
| `mt_text_line_height(t, multiple)` | Scale baseline-to-baseline distance; `1.0`/`0` natural, below `1.0` may overlap. |
| `mt_text_wrap(t, max_width)` | Lay out and wrap to `max_width` pixels (`<= 0` = one line). |
| `mt_block_lines(b)` / `mt_block_line(b, i)` | Line count, and line `i` as an `mt_shaped`. |
| `mt_block_line_y(b,i)` / `mt_block_height(b)` / `mt_block_line_at_y(b,y)` | Line top y, total height, and the line at a y, for click-to-line. |
| `mt_block_line_source(b, i, &len)` | Byte span of line `i` in the text, to map a line offset back to the document. |
| `mt_text_free(t)` / `mt_block_free(b)` | Release the builder and the line list. |

Stack lines by advancing the pen y by each `mt_metrics.height`, and shift the pen x by `mt_metrics.align_dx` for non-left alignment.
Mandatory breaks (UAX #14: LF, VT, FF, CR, CRLF, NEL, LS, PS) start a new line; a blank line keeps the font's height.
Not yet supported: text decorations (underline, strikethrough), tab stops.

## Layout

`mt_render` sizes the bitmap to the glyph ink, not the advance, so overhang does not clip.
`mt_metrics.origin_x`/`origin_y` give the pen origin inside it: the baseline is `origin_y` rows below the top, the start pen x is `origin_x` columns from the left.
To place a run with its pen at `(px, py)` (`py` the baseline), blit the bitmap top-left at `(px - origin_x, py - origin_y)`; two runs align by sharing `py`.

Output is 8-bit sRGB RGBA, straight alpha (the premultiplied-to-straight step loses precision at very low alpha).
`pixel_size` is in physical pixels: on a 2x display, open at the logical size times the backing scale.
On failure a call returns `NULL` (or a zeroed `mt_metrics`) and sets `mt_last_error`.

## Hit-testing

A shaped line maps between byte offsets and screen positions, so a consumer can place a cursor, turn a click into an offset, and draw a selection.

| Function | Purpose |
| --- | --- |
| `mt_shaped_caret_x(s, byte_off)` | The caret x for a byte offset. |
| `mt_shaped_byte_at_x(s, x)` | The nearest insertion byte offset for a pixel x. |
| `mt_shaped_selection(s, a, b, out, max)` | Visual x-spans the byte range `[a, b)` covers; a bidi range splits into several. |

Offsets are into the line's own bytes (line-relative for an `mt_block` line) and round-trip at cluster boundaries.
The x axis is the line's own and excludes `mt_metrics.align_dx`: a line drawn at `pen_x + align_dx` takes `click_x - pen_x - align_dx` in and gives `pen_x + align_dx + caret_x` out.
For the y axis of a wrapped block, `mt_block_line_at_y` finds the line and `mt_block_line_source` maps a line offset back to a document byte.
An `mt_block` line's bytes include any trailing hard-break character; a single `mt_shape` run never carries one.

## Types

| Type | Definition |
| --- | --- |
| `mt_color` | `{ unsigned char r, g, b, a; }`. |
| `mt_metrics` | `width`, `height`, `ascent`, `descent`, `leading` (float); `origin_x`, `origin_y` (int pen origin); `align_dx` (float alignment shift). |
| `mt_align` | `MT_ALIGN_LEFT`, `MT_ALIGN_RIGHT`, `MT_ALIGN_CENTER`, `MT_ALIGN_JUSTIFY`. |
| `mt_error` | `MT_OK`, `MT_ERR_FONT`, `MT_ERR_TEXT`, `MT_ERR_OOM`, `MT_ERR_BACKEND`. |
| `MICROTEXT_VERSION` | Compile-time feature level, for `#if MICROTEXT_VERSION >= N` checks. |

## Scope and non-goals

- Renders to a CPU RGBA bitmap; no GPU, window, or event layer (you upload and draw).
- One backend per platform via the OS engine; no portable from-scratch rasterizer.
- Caching is the consumer's job; the library holds no glyph or layout cache.
- Lays out and hit-tests, but owns no document, selection, or undo model.
- Not yet: text decorations (underline, strikethrough), tab stops, per-glyph boxes.

## Backends

| Platform | Engine | Status |
| --- | --- | --- |
| macOS | CoreText + CoreGraphics | working, no Objective-C |
| Windows | DirectWrite + Direct2D | not yet written |
| Linux | FreeType + HarfBuzz + fontconfig | not yet written |

The macOS backend links `-framework CoreText -framework CoreGraphics -framework CoreFoundation` and needs nothing else.
Each backend sits behind the same `mt_*` API; the others are a compile error until written.

## Build

```sh
make test       # render samples and check them (exits non-zero on failure)
make sanitize   # the test under AddressSanitizer + UBSan
make leaks      # the test under the macOS leaks tool
make demo_1_showcase  # render every feature to output/showcase.png
make demo_2_raylib    # the interactive raylib demo (needs brew raylib)
```

## Configuration

Optional macros, defined before the implementation include:

| Macro | Effect |
| --- | --- |
| `MICROTEXT_STATIC` | Internal linkage, folding the implementation into the one TU that defines `MICROTEXT_IMPLEMENTATION`. |
| `MICROTEXTDEF` | Override the linkage of every public function (e.g. `__declspec(dllexport)`). |
| `MICROTEXT_MALLOC` / `MICROTEXT_REALLOC` / `MICROTEXT_FREE` | Route allocation through your own allocator. Define all three, or none. |

## License

MIT; see [LICENSE](LICENSE).
