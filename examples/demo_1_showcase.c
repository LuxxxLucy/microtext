/* Render every microtext feature onto one PNG: a headless gallery for the
 * README. No window and no raylib, just microtext + stb_image_write. */
#define MICROTEXT_IMPLEMENTATION
#include "../microtext.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../tests/3rd/stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>

#define W 1040
#define H 812
#define CAP_X 48   // caption column
#define SAMP_X 300 // sample column
#define OUT_PNG "build/output/showcase.png"

static unsigned char *cv; // W*H opaque RGBA canvas

// Composite a straight-alpha RGBA bitmap onto the opaque canvas at (x, y).
static void over(int x, int y, const unsigned char *src, int sw, int sh)
{
    for (int r = 0; r < sh; r++) {
        int cy = y + r;
        if (cy < 0 || cy >= H) {
            continue;
        }
        for (int c = 0; c < sw; c++) {
            int cx = x + c;
            if (cx < 0 || cx >= W) {
                continue;
            }
            const unsigned char *s = src + ((size_t)r * sw + c) * 4;
            unsigned char *d = cv + ((size_t)cy * W + cx) * 4;
            unsigned a = s[3];
            for (int k = 0; k < 3; k++) {
                d[k] =
                    (unsigned char)((s[k] * a + d[k] * (255 - a) + 127) / 255);
            }
            d[3] = 255;
        }
    }
}

/* Draw one run with its baseline at (x, baseline). Returns the advance width.
 */
static float run_base(const mt_font *f, const char *txt, mt_color col, int x,
                      int baseline)
{
    int w, h;
    mt_metrics m;
    unsigned char *px = mt_render(f, txt, -1, col, &w, &h, &m);
    if (px) {
        over(x - m.origin_x, baseline - m.origin_y, px, w, h);
        mt_free(px);
    }
    return m.width;
}

// Render one mt_shaped line with its baseline at (x, baseline).
static void line_base(const mt_shaped *ln, int x, int baseline)
{
    int w, h;
    mt_metrics m = mt_shaped_metrics(ln);
    unsigned char *px = mt_shaped_render(ln, NULL, &w, &h, NULL);
    if (px) {
        over(x - m.origin_x, baseline - m.origin_y, px, w, h);
        mt_free(px);
    }
}

int main(void)
{
    cv = (unsigned char *)malloc((size_t)W * H * 4);
    if (!cv) {
        return 1;
    }
    for (size_t i = 0; i < (size_t)W * H; i++) {
        cv[i * 4 + 0] = 250;
        cv[i * 4 + 1] = 250;
        cv[i * 4 + 2] = 252;
        cv[i * 4 + 3] = 255;
    }

    mt_font *title = mt_font_open("Helvetica Neue", 64.0f);
    mt_font *body = mt_font_open("Helvetica Neue", 34.0f);
    mt_font *bold = mt_font_open_styled("Helvetica Neue", 34.0f, 1, 0);
    mt_font *cap = mt_font_open("Helvetica Neue", 15.0f);
    mt_font *serif = mt_font_open("Baskerville", 34.0f);

    mt_color ink = { 28, 28, 36, 255 };
    mt_color sub = { 110, 110, 122, 255 };
    mt_color gray = { 150, 150, 162, 255 };
    mt_color red = { 200, 44, 44, 255 };

    run_base(title, "microtext", ink, CAP_X, 92);
    run_base(body, "native text shaping in one header", sub, CAP_X, 140);

    int b = 232; // running baseline
    const int step = 62;

    run_base(cap, "mixed scripts, fallback", gray, CAP_X, b);
    run_base(body, "English  中文  日本語  한국어", ink, SAMP_X, b);
    b += step;

    run_base(cap, "bidi", gray, CAP_X, b);
    run_base(body, "العربية  עברית  English RTL", ink, SAMP_X, b);
    b += step;

    run_base(cap, "bidi + color emoji", gray, CAP_X, b);
    run_base(body, "Hello مرحبا \U0001F30D 世界 \U0001F3B5", ink, SAMP_X, b);
    b += step;

    run_base(cap, "color emoji", gray, CAP_X, b);
    run_base(body,
             "\U0001F3B5 \U0001F525 \U0001F30D \U0001F600 \U0001F44D "
             "\U0001F3B8 ✨",
             ink, SAMP_X, b);
    b += step;

    // rich runs: several fonts and colors on one baseline
    run_base(cap, "rich runs", gray, CAP_X, b);
    {
        mt_text *t = mt_text_new();
        mt_text_run(t, "regular ", -1, body, sub, NULL);
        mt_text_run(t, "bold ", -1, bold, ink, NULL);
        mt_text_run(t, "red ", -1, body, red, NULL);
        mt_text_run(t, "serif", -1, serif, ink, NULL);
        mt_block *blk = mt_text_wrap(t, 0.0f);
        if (mt_block_lines(blk) == 1) {
            line_base(mt_block_line(blk, 0), SAMP_X, b);
        }
        mt_block_free(blk);
        mt_text_free(t);
    }
    b += step;

    // OpenType: plain face beside the same word with small caps
    run_base(cap, "OpenType smcp", gray, CAP_X, b);
    {
        float adv = run_base(serif, "Quantum", ink, SAMP_X, b);
        run_base(body, "→", gray, SAMP_X + (int)adv + 24, b);
        mt_text *t = mt_text_new();
        mt_text_run(t, "Quantum", -1, serif, ink, "smcp");
        mt_block *blk = mt_text_wrap(t, 0.0f);
        if (mt_block_lines(blk) == 1) {
            line_base(mt_block_line(blk, 0), SAMP_X + (int)adv + 64, b);
        }
        mt_block_free(blk);
        mt_text_free(t);
    }
    b += step;

    // wrapped paragraph with a hard break and mixed scripts
    run_base(cap, "wrap + hard breaks", gray, CAP_X, b);
    {
        mt_text *t = mt_text_new();
        mt_text_run(t,
                    "microtext wraps this paragraph to a fixed width and\n"
                    "obeys hard breaks, mixing 中文 and العربية in one block.",
                    -1, body, ink, NULL);
        mt_block *blk = mt_text_wrap(t, 660.0f);
        int top = b;
        for (int i = 0; i < mt_block_lines(blk); i++) {
            const mt_shaped *ln = mt_block_line(blk, i);
            mt_metrics m = mt_shaped_metrics(ln);
            line_base(ln, SAMP_X, top + (int)(m.ascent + 0.5f));
            top += (int)(m.height + 0.5f);
        }
        mt_block_free(blk);
        mt_text_free(t);
    }

    stbi_write_png(OUT_PNG, W, H, 4, cv, W * 4);
    printf("wrote %s  %dx%d\n", OUT_PNG, W, H);

    free(cv);
    mt_font_close(title);
    mt_font_close(body);
    mt_font_close(bold);
    mt_font_close(cap);
    mt_font_close(serif);
    return 0;
}
