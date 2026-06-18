# microtext

A single-header C library for modern text: one UTF-8 string in, one laid-out RGBA bitmap out.
Bidi, complex shaping, CJK, color emoji, and font fallback come from the operating system's own text engine, so the result looks the way native apps look.
The core returns pixels and knows nothing about any GPU or window toolkit; upload the bitmap however you like.

This is `stb_truetype.h`'s ergonomics with the OS's typography.
`stb_truetype` reimplements TrueType in portable C and is therefore monochrome, single-font, and unshaped.
microtext instead delegates to the native engine, so it gains shaping, fallback, bidi, and color emoji that a from-scratch rasterizer cannot reach.

![Latin, CJK, and Korean fallback; Arabic and Hebrew bidi; full-color emoji; rich multi-font runs; OpenType small caps; and a width-wrapped paragraph with a hard break, all rendered by microtext](docs/showcase.png)

Every glyph above is rendered by microtext onto one bitmap.
`examples/demo_1_showcase.c` produces this image headlessly; `make demo_1_showcase` regenerates it.

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

The library is stateless: every `mt_render` allocates a buffer you own and free.
Caching belongs to the consumer.
`examples/mt_raylib.h` shows the pattern, a `(string, color)`-keyed `Texture2D` cache around `mt_render`.

It is not thread-safe: the last-error slot and an internal sRGB color space are process-global.
To use it from more than one thread, shape on one thread and render the `mt_shaped` on another (nothing shared is touched after shaping), or serialize the calls.

## API

| Function | Purpose |
| --- | --- |
| `mt_font_open(family, px)` | Open a font by family name; `NULL` uses the system UI font. |
| `mt_font_open_styled(family, px, bold, italic)` | Open with a bold and/or italic style; a missing style falls back to regular. |
| `mt_font_open_memory(data, len, px)` | Open from an in-memory `.ttf`/`.otf`, so an app can ship its own font. |
| `mt_font_close(f)` | Release a font. |
| `mt_measure(f, utf8, len)` | Return `mt_metrics` (advance width and vertical metrics) without rendering. |
| `mt_render(f, utf8, len, color, &w, &h, &m)` | Lay out and rasterize a run to a malloc'd RGBA bitmap. |
| `mt_free(p)` | Free a rendered bitmap. |
| `mt_last_error()` | The error from the last call (`mt_error`), process-global. |

`len < 0` means the string is NUL-terminated.
`color` tints non-color glyphs; color emoji keep their own colors.

## Rich text and wrapping

`mt_render` takes one font and one color. For mixed styles or multi-line paragraphs, build an `mt_text` from styled runs and lay it out.

```c
mt_text *t = mt_text_new();
mt_text_run(t, "Bold ",        -1, bold, red, NULL);    // run: font, color, features
mt_text_run(t, "中文 world", -1, reg,  ink, "smcp");  // OpenType feature tags
mt_block *b = mt_text_wrap(t, 520.0f);                  // <= 0 wraps only at hard breaks
for (int i = 0; i < mt_block_lines(b); i++) {
    mt_shaped *line = mt_block_line(b, i);              // each line renders + measures
    // ... mt_shaped_render(line, ...) ...              // like any shaped run
}
mt_block_free(b);
mt_text_free(t);
```

| Function | Purpose |
| --- | --- |
| `mt_text_new()` | Start an empty paragraph. |
| `mt_text_run(t, utf8, len, f, color, features)` | Append a styled run; `features` is a space-separated list of OpenType tags to enable (`"smcp tnum frac"`) or `NULL`. |
| `mt_text_align(t, align)` | Paragraph alignment: `MT_ALIGN_LEFT` (default), `RIGHT`, `CENTER`, `JUSTIFY`. Needs a positive wrap width. |
| `mt_text_line_height(t, multiple)` | Scale the baseline-to-baseline distance; `1.0` (or `0`) is natural, `1.5` is one-and-a-half spacing. |
| `mt_text_wrap(t, max_width)` | Lay out and wrap to `max_width` pixels (`<= 0` = one line); returns a list of lines. |
| `mt_block_lines(b)` / `mt_block_line(b, i)` | Line count, and line `i` as an `mt_shaped`. |
| `mt_text_free(t)` / `mt_block_free(b)` | Release the builder and the line list. |

Each line is an `mt_shaped`: stack them by advancing the pen y by each line's `mt_metrics.height`, and shift the pen x by `mt_metrics.align_dx` for non-left alignment.
Left, right, and center position the laid-out line in the width; justify stretches it to the width, leaving the last line of each paragraph ragged.
The Unicode mandatory breaks (UAX #14: LF, VT, FF, CR, CRLF, NEL, LS, PS) start a new line on their own; a blank line keeps the font's height.
Which OpenType features a tag activates depends on the font (`smcp` needs a font with small caps).

## Layout

`mt_render` sizes the bitmap to the glyph ink, not the advance, so overhang does not clip.
`mt_metrics.origin_x` and `origin_y` give the pen origin inside the bitmap: the baseline is `origin_y` rows below the top, the start pen x is `origin_x` columns from the left.
To place a run with its pen at `(px, py)` where `py` is the baseline, blit the bitmap top-left at `(px - origin_x, py - origin_y)`; two runs of different sizes align by sharing `py`.

`mt_render` lays out exactly one line; embedded newlines are not breaks.
For paragraphs and hard breaks use `mt_text_wrap`, or split on `\n` yourself and stack the lines, advancing `py` by `mt_metrics.height` per line.
An empty run yields a minimal transparent bitmap with metrics width 0.

The output is 8-bit sRGB RGBA with straight alpha; the premultiplied-to-straight conversion loses color precision at very low alpha.
`pixel_size` is in physical pixels: on a 2x display, open the font at the logical size times the backing scale and draw one texel per pixel.

On failure the call returns `NULL` (or a zeroed `mt_metrics`) and sets `mt_last_error`, which separates a bad font, invalid UTF-8, out of memory, and a backend refusal.

## Hit-testing

A shaped line maps between byte offsets and caret positions, so a consumer can place a cursor and turn a click into a text offset.

| Function | Purpose |
| --- | --- |
| `mt_shaped_caret_x(s, byte_off)` | The caret x for a byte offset into the line's text. |
| `mt_shaped_byte_at_x(s, x)` | The nearest insertion byte offset for a pixel x. |

Both work against the shaped line's own text: the whole run for `mt_shape`, or the line's bytes for an `mt_block` line (offsets are line-relative).
The x is measured from the pen origin, the same axis as `mt_metrics.width`, so add it to the pen x at which the line is drawn.
The two round-trip at cluster boundaries, and the UTF-8 byte offsets account for multibyte and astral characters.

```c
mt_shaped *s = mt_shape(f, "Click here", -1, ink);
ptrdiff_t i = mt_shaped_byte_at_x(s, mouse_x - pen_x);  // click -> byte offset
float caret = pen_x + mt_shaped_caret_x(s, i);          // byte offset -> caret x
```

## Backends

| Platform | Engine | Status |
| --- | --- | --- |
| macOS | CoreText + CoreGraphics | working, no Objective-C |
| Windows | DirectWrite + Direct2D | not yet written |
| Linux | FreeType + HarfBuzz + fontconfig | not yet written |

The macOS backend links `-framework CoreText -framework CoreGraphics -framework CoreFoundation` and needs nothing else.
Each backend sits behind the same `mt_*` API; the other two are stubbed with a compile error until written.

## Build

```sh
make test             # render sample runs to output/ and check them (exits non-zero on failure)
make demo_1_showcase  # render every feature to output/showcase.png (no dependencies)
make demo_2_raylib    # the interactive raylib demo (needs brew raylib)
```

Two demos render the same features through two integration paths.
`examples/demo_1_showcase.c` is headless: it composites every feature onto one bitmap with `mt_render` and `mt_text_wrap` and writes the PNG shown above, so it builds and runs with no dependency beyond the macOS frameworks.
`examples/demo_2_raylib.c` is the live path: it uploads each `mt_render` result to a `Texture2D` through the cache in `examples/mt_raylib.h` and draws it per frame.

## License

MIT; see [LICENSE](LICENSE).
