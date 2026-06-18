/* Render sample runs to PNGs for visual inspection. */
#define MICROTEXT_IMPLEMENTATION
#include "../microtext.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "3rd/stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define OUTDIR "output"

/* Count failures so the process exit code reflects them; skips do not fail. */
static int g_fails;
static int g_skips;
#define CHECK(cond) ((cond) ? "ok   " : (g_fails++, "FAIL "))
#define SKIP(what) (g_skips++, printf("SKIP  %s\n", (what)))
#define NEAR(a, b, tol) ((a) - (b) < (tol) && (b) - (a) < (tol))

static unsigned char *slurp(const char *path, unsigned long *n)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    unsigned char *b = (unsigned char *)malloc((size_t)sz);
    if (b && fread(b, 1, (size_t)sz, fp) != (size_t)sz) {
        free(b);
        b = NULL;
    }
    fclose(fp);
    *n = (unsigned long)sz;
    return b;
}

/* Stack a wrapped block's lines onto one canvas, each on its own baseline. */
static void composite_block(mt_block *b, const char *path)
{
    int n = mt_block_lines(b);
    int W = 1, H = 0;
    for (int i = 0; i < n; i++) {
        mt_metrics m = mt_shaped_metrics(mt_block_line(b, i));
        int lw, lh;
        mt_shaped_size(mt_block_line(b, i), &lw, &lh);
        if (lw > W) {
            W = lw;
        }
        H += (int)(m.height + 0.5f);
    }
    if (H < 1) {
        H = 1;
    }
    unsigned char *canvas = (unsigned char *)calloc((size_t)W * H, 4);
    if (!canvas) {
        return;
    }
    int y = 0;
    for (int i = 0; i < n; i++) {
        const mt_shaped *ln = mt_block_line(b, i);
        mt_metrics m = mt_shaped_metrics(ln);
        int lw, lh;
        unsigned char *px = mt_shaped_render(ln, NULL, &lw, &lh, NULL);
        if (px) {
            int top = y + (int)(m.ascent + 0.5f) - m.origin_y;
            for (int row = 0; row < lh; row++) {
                int cy = top + row;
                if (cy < 0 || cy >= H) {
                    continue;
                }
                memcpy(canvas + (size_t)cy * W * 4, px + (size_t)row * lw * 4,
                       (size_t)lw * 4);
            }
            mt_free(px);
        }
        y += (int)(m.height + 0.5f);
    }
    stbi_write_png(path, W, H, 4, canvas, W * 4);
    free(canvas);
}

static void dump(mt_font *f, const char *text, mt_color c, const char *path)
{
    int w, h;
    mt_metrics m;
    unsigned char *px = mt_render(f, text, -1, c, &w, &h, &m);
    if (!px) {
        printf("FAIL  %-16s (render returned NULL)\n", path);
        g_fails++;
        return;
    }
    char full[256];
    snprintf(full, sizeof(full), "%s/%s", OUTDIR, path);
    stbi_write_png(full, w, h, 4, px, w * 4);
    printf("ok    %-16s  %dx%d  advance=%.1f ascent=%.1f origin=(%d,%d)\n", path,
           w, h, m.width, m.ascent, m.origin_x, m.origin_y);
    mt_free(px);
}

int main(void)
{
    mkdir(OUTDIR, 0755);
    mt_font *f = mt_font_open("Helvetica Neue", 64.0f);
    if (!f) {
        SKIP("Helvetica Neue unavailable; cannot run the suite");
        return 0;
    }
    mt_color ink = { 20, 20, 20, 255 };
    mt_color red = { 220, 30, 30, 255 };

    dump(f, "Ap Red", red, "out_latin.png");        /* orientation + color */
    dump(f, "中文 日本語 한국어", ink,
         "out_cjk.png");                             /* CJK fallback */
    dump(f, "العربية", ink,
         "out_arabic.png");                          /* RTL bidi + shaping */
    dump(f, "Hello مرحبا \U0001F30D 世界 "
            "\U0001F3B5",
         ink, "out_mixed.png");                      /* bidi + emoji + CJK */
    dump(f, "\U0001F3B5\U0001F525\U0001F30D\U0001F600\U0001F44D", ink,
         "out_emoji.png");                           /* color emoji */

    mt_font *bold = mt_font_open_styled("Helvetica Neue", 64.0f, 1, 0);
    mt_font *ital = mt_font_open_styled("Helvetica Neue", 64.0f, 0, 1);
    if (bold) {
        dump(bold, "Bold face", ink, "out_bold.png");
        mt_font_close(bold);
    } else {
        SKIP("bold face unavailable");
    }
    if (ital) {
        dump(ital, "Italic fy", ink, "out_italic.png");  /* overhang stress */
        mt_font_close(ital);
    } else {
        SKIP("italic face unavailable");
    }

    unsigned long n = 0;
    unsigned char *ttf =
        slurp("/System/Library/Fonts/Supplemental/Arial Unicode.ttf", &n);
    if (ttf) {
        mt_font *mem = mt_font_open_memory(ttf, n, 64.0f);
        dump(mem, "From memory 内存", ink, "out_mem.png");
        mt_font_close(mem);
        free(ttf);
    }

    /* shaped handle rendered into a caller-owned buffer */
    mt_shaped *sh = mt_shape(f, "Shaped 形状", -1, ink);
    if (sh) {
        int sw, shh;
        mt_shaped_size(sh, &sw, &shh);
        unsigned char *own = (unsigned char *)malloc((size_t)sw * shh * 4);
        int rw, rh;
        unsigned char *got = mt_shaped_render(sh, own, &rw, &rh, NULL);
        printf("%s  shaped render-into-buffer  %dx%d\n",
               CHECK(got == own && rw == sw && rh == shh), rw, rh);
        stbi_write_png(OUTDIR "/out_shaped.png", rw, rh, 4, own, rw * 4);
        free(own);
        mt_shaped_free(sh);
    }

    /* rich runs: several fonts and colors share one baseline */
    {
        mt_font *rb = mt_font_open_styled("Helvetica Neue", 48.0f, 1, 0);
        mt_text *t = mt_text_new();
        mt_text_run(t, "Bold ", -1, rb, red, NULL);
        mt_text_run(t, "中文 ", -1, f, ink, NULL);
        mt_text_run(t, "regular runs.", -1, f, ink, NULL);
        mt_block *b = mt_text_wrap(t, 0.0f);  /* one line */
        printf("%s  rich one line     %d line(s)\n",
               CHECK(b && mt_block_lines(b) == 1),
               b ? mt_block_lines(b) : -1);
        if (b && mt_block_lines(b) >= 1) {
            int rw, rh;
            unsigned char *px =
                mt_shaped_render(mt_block_line(b, 0), NULL, &rw, &rh, NULL);
            if (px) {
                stbi_write_png(OUTDIR "/out_rich.png", rw, rh, 4, px, rw * 4);
                mt_free(px);
            }
        }
        mt_block_free(b);
        mt_text_free(t);
        mt_font_close(rb);
    }

    /* width wrap: one long paragraph broken into stacked lines */
    {
        mt_text *t = mt_text_new();
        mt_text_run(t,
                    "The quick brown fox jumps over the lazy dog, then wraps "
                    "onto several lines at a fixed width.",
                    -1, f, ink, NULL);
        mt_block *b = mt_text_wrap(t, 520.0f);
        int n = b ? mt_block_lines(b) : 0;
        printf("%s  width wrap        %d lines\n", CHECK(n > 1), n);
        if (n > 0) {
            composite_block(b, OUTDIR "/out_wrap.png");
        }
        mt_block_free(b);
        mt_text_free(t);
    }

    /* hard breaks: '\n' splits lines, a blank line keeps its height */
    {
        mt_text *t = mt_text_new();
        mt_text_run(t, "Line one\nLine two\n\nLine four", -1, f, ink, NULL);
        mt_block *b = mt_text_wrap(t, 0.0f);  /* no width wrap, only hard breaks */
        int n = b ? mt_block_lines(b) : 0;
        float blank = n == 4 ? mt_shaped_metrics(mt_block_line(b, 2)).height : 0;
        printf("%s  hard breaks       %d lines, blank height=%.1f\n",
               CHECK(n == 4 && blank > 1), n, blank);
        if (n > 0) {
            composite_block(b, OUTDIR "/out_breaks.png");
        }
        mt_block_free(b);
        mt_text_free(t);
    }

    /* OpenType feature: smcp turns lowercase into small caps */
    {
        mt_font *bk = mt_font_open("Baskerville", 64.0f);
        if (!bk) {
            SKIP("Baskerville unavailable; smcp test skipped");
        } else {
        const char *txt = "Small Caps";
        int aw, ah, bw = 0, bh = 0;
        unsigned char *a = mt_render(bk, txt, -1, ink, &aw, &ah, NULL);
        mt_text *t = mt_text_new();
        mt_text_run(t, txt, -1, bk, ink, "smcp");
        mt_block *blk = mt_text_wrap(t, 0.0f);
        unsigned char *bpx = NULL;
        if (blk && mt_block_lines(blk) == 1) {
            bpx = mt_shaped_render(mt_block_line(blk, 0), NULL, &bw, &bh, NULL);
        }
        int changed = a && bpx &&
                      (aw != bw || ah != bh ||
                       memcmp(a, bpx, (size_t)aw * ah * 4) != 0);
        printf("%s  OT feature smcp   plain=%dx%d smcp=%dx%d %s\n",
               CHECK(changed), aw, ah, bw, bh,
               changed ? "(differs)" : "(no change)");
        if (a) {
            stbi_write_png(OUTDIR "/out_ot_plain.png", aw, ah, 4, a, aw * 4);
        }
        if (bpx) {
            stbi_write_png(OUTDIR "/out_ot_smcp.png", bw, bh, 4, bpx, bw * 4);
        }
        mt_free(a);
        mt_free(bpx);
        mt_block_free(blk);
        mt_text_free(t);
        mt_font_close(bk);
        }
    }

    /* hit-testing: byte offset <-> caret x round-trips on a shaped line */
    {
        mt_shaped *s = mt_shape(f, "Hello", -1, ink);
        int ok = s != NULL;
        if (s) {
            float wd = mt_shaped_metrics(s).width;
            ok = mt_shaped_caret_x(s, 0) < 1.0f &&
                 mt_shaped_caret_x(s, 5) > wd - 2.0f;
            for (int i = 0; i <= 5; i++) {
                if (mt_shaped_byte_at_x(s, mt_shaped_caret_x(s, i)) != i) {
                    ok = 0;
                }
            }
            mt_shaped_free(s);
        }
        printf("%s  hit-test ascii    round-trip bytes 0..5\n", CHECK(ok));
    }
    {
        /* "A" + U+00E9 (2 bytes, 1 u16) + U+1F600 (4 bytes, 2 u16) + "B" */
        const char *txt = "Aé\U0001F600B";
        ptrdiff_t bound[] = { 0, 1, 3, 7, 8 };
        mt_shaped *s = mt_shape(f, txt, -1, ink);
        int ok = s != NULL;
        if (s) {
            for (int i = 0; i < 5; i++) {
                if (mt_shaped_byte_at_x(s, mt_shaped_caret_x(s, bound[i])) !=
                    bound[i]) {
                    ok = 0;
                }
            }
            mt_shaped_free(s);
        }
        printf("%s  hit-test unicode  multibyte + astral round-trip\n",
               CHECK(ok));
    }
    {
        /* block lines carry line-relative offsets */
        mt_text *t = mt_text_new();
        mt_text_run(t, "alpha beta gamma delta epsilon zeta eta theta", -1, f,
                    ink, NULL);
        mt_block *bl = mt_text_wrap(t, 360.0f);
        int ok = bl && mt_block_lines(bl) >= 2;
        if (ok) {
            const mt_shaped *ln = mt_block_line(bl, 1);
            if (mt_shaped_byte_at_x(ln, 0.0f) != 0 ||
                mt_shaped_caret_x(ln, 0) > 1.0f) {
                ok = 0;
            }
        }
        printf("%s  hit-test block    line-relative offsets\n", CHECK(ok));
        mt_block_free(bl);
        mt_text_free(t);
    }

    /* alignment: left/right/center shift align_dx; justify fills the width */
    {
        const char *para =
            "The quick brown fox jumps over the lazy dog and keeps running";
        float wbox = 360.0f;
        mt_text *tl = mt_text_new();
        mt_text_run(tl, para, -1, f, ink, NULL);
        mt_block *bl = mt_text_wrap(tl, wbox);
        mt_text *tr = mt_text_new();
        mt_text_align(tr, MT_ALIGN_RIGHT);
        mt_text_run(tr, para, -1, f, ink, NULL);
        mt_block *br = mt_text_wrap(tr, wbox);
        mt_text *tc = mt_text_new();
        mt_text_align(tc, MT_ALIGN_CENTER);
        mt_text_run(tc, para, -1, f, ink, NULL);
        mt_block *bc = mt_text_wrap(tc, wbox);
        int ok = bl && br && bc && mt_block_lines(bl) >= 2;
        if (ok) {
            mt_metrics l0 = mt_shaped_metrics(mt_block_line(bl, 0));
            mt_metrics r0 = mt_shaped_metrics(mt_block_line(br, 0));
            mt_metrics c0 = mt_shaped_metrics(mt_block_line(bc, 0));
            float expect = wbox - l0.width;
            /* caret_x is line-relative: ~0 at the start even on a right-aligned
               line whose align_dx is large (the two axes are independent) */
            float caret0 = mt_shaped_caret_x(mt_block_line(br, 0), 0);
            ok = l0.align_dx == 0.0f && r0.align_dx > 1.0f &&
                 NEAR(r0.align_dx, expect, 1.0f) &&
                 NEAR(c0.align_dx, expect * 0.5f, 1.0f) && caret0 < 1.0f;
        }
        printf("%s  align L/R/C       dx left=%.0f right=%.0f center=%.0f\n",
               CHECK(ok), mt_shaped_metrics(mt_block_line(bl, 0)).align_dx,
               mt_shaped_metrics(mt_block_line(br, 0)).align_dx,
               mt_shaped_metrics(mt_block_line(bc, 0)).align_dx);
        mt_block_free(bl);
        mt_block_free(br);
        mt_block_free(bc);
        mt_text_free(tl);
        mt_text_free(tr);
        mt_text_free(tc);
    }
    {
        const char *para =
            "The quick brown fox jumps over the lazy dog and keeps running";
        float wbox = 360.0f;
        mt_text *t = mt_text_new();
        mt_text_align(t, MT_ALIGN_JUSTIFY);
        mt_text_run(t, para, -1, f, ink, NULL);
        mt_block *b = mt_text_wrap(t, wbox);
        int ok = b && mt_block_lines(b) >= 2;
        if (ok) {
            float w0 = mt_shaped_metrics(mt_block_line(b, 0)).width;
            ok = w0 > wbox - 6.0f && w0 <= wbox + 2.0f;
        }
        printf("%s  align justify     line0 width %.0f -> box %.0f\n", CHECK(ok),
               ok ? mt_shaped_metrics(mt_block_line(b, 0)).width : 0.0f, wbox);
        mt_block_free(b);
        mt_text_free(t);
    }
    {
        /* line-height multiplies the baseline-to-baseline distance */
        mt_text *t1 = mt_text_new();
        mt_text_run(t1, "one\ntwo", -1, f, ink, NULL);
        mt_block *b1 = mt_text_wrap(t1, 0.0f);
        mt_text *t2 = mt_text_new();
        mt_text_line_height(t2, 2.0f);
        mt_text_run(t2, "one\ntwo", -1, f, ink, NULL);
        mt_block *b2 = mt_text_wrap(t2, 0.0f);
        int ok = b1 && b2 && mt_block_lines(b1) >= 1 && mt_block_lines(b2) >= 1;
        float h1 = 0, h2 = 0;
        if (ok) {
            h1 = mt_shaped_metrics(mt_block_line(b1, 0)).height;
            h2 = mt_shaped_metrics(mt_block_line(b2, 0)).height;
            ok = NEAR(h2, h1 * 2.0f, 0.5f);
        }
        printf("%s  line-height 2x    %.1f -> %.1f\n", CHECK(ok), h1, h2);
        mt_block_free(b1);
        mt_block_free(b2);
        mt_text_free(t1);
        mt_text_free(t2);
    }
    {
        /* font metrics without shaping agree with a shaped line */
        mt_metrics fm = mt_font_metrics(f);
        mt_metrics sm = mt_measure(f, "Mg", -1);
        int ok = fm.height > 0 && NEAR(fm.ascent, sm.ascent, 0.01f) &&
                 NEAR(fm.descent, sm.descent, 0.01f);
        printf("%s  font metrics      ascent=%.1f height=%.1f\n", CHECK(ok),
               fm.ascent, fm.height);
    }
    {
        /* block geometry: line_at_y inverts the stacking, source spans
           partition the text, height totals the stack, selection covers a line */
        const char *para = "alpha beta gamma delta epsilon zeta eta theta iota";
        mt_text *t = mt_text_new();
        mt_text_run(t, para, -1, f, ink, NULL);
        mt_block *b = mt_text_wrap(t, 120.0f);
        int n = mt_block_lines(b);
        int ok = n >= 2;
        for (int i = 0; ok && i < n; i++) {
            if (mt_block_line_at_y(b, mt_block_line_y(b, i) + 1.0f) != i) {
                ok = 0;
            }
        }
        ptrdiff_t expect = 0;
        for (int i = 0; ok && i < n; i++) {
            ptrdiff_t len, start = mt_block_line_source(b, i, &len);
            if (start != expect) {
                ok = 0;
            }
            expect += len;
        }
        if (ok && expect != (ptrdiff_t)strlen(para)) {
            ok = 0;
        }
        if (ok) {
            float last = mt_block_line_y(b, n - 1) +
                         mt_shaped_metrics(mt_block_line(b, n - 1)).height;
            ok = NEAR(mt_block_height(b), last, 0.01f);
        }
        if (ok) {
            ptrdiff_t llen;
            mt_block_line_source(b, 0, &llen);
            float sp[8];
            int sn = mt_shaped_selection(mt_block_line(b, 0), 0, llen, sp, 4);
            float w0 = mt_shaped_metrics(mt_block_line(b, 0)).width;
            ok = sn >= 1 && sp[0] < 1.0f && sp[sn * 2 - 1] > w0 - 30.0f;
        }
        printf("%s  block geometry    %d lines, y+source+selection round-trip\n",
               CHECK(ok), n);
        mt_block_free(b);
        mt_text_free(t);
    }
    {
        /* a bidi line splits one logical range into disjoint visual spans */
        mt_text *t = mt_text_new();
        mt_text_run(t, "abc \xD7\x90\xD7\x91\xD7\x92 xyz", -1, f, ink, NULL);
        mt_block *b = mt_text_wrap(t, 0.0f);
        int ok = mt_block_lines(b) == 1;
        if (ok) {
            ptrdiff_t llen;
            mt_block_line_source(b, 0, &llen);
            float sp[16];
            int sn = mt_shaped_selection(mt_block_line(b, 0), 0, llen, sp, 8);
            ok = sn >= 1;
            for (int i = 0; ok && i < sn; i++) {
                if (sp[i * 2] > sp[i * 2 + 1]) {  /* each span is ordered */
                    ok = 0;
                }
            }
        }
        printf("%s  bidi selection    spans ordered on a mixed line\n", CHECK(ok));
        mt_block_free(b);
        mt_text_free(t);
    }
    {
        /* RTL line: logical index 0 sits at the right, so caret x descends */
        mt_shaped *r = mt_shape(
            f, "\xD8\xA7\xD9\x84\xD8\xB9\xD8\xB1\xD8\xA8\xD9\x8A\xD8\xA9", -1,
            ink);  /* العربية, 14 bytes */
        int ok = r != NULL;
        if (r) {
            ok = mt_shaped_caret_x(r, 0) > mt_shaped_caret_x(r, 14) + 1.0f;
            mt_shaped_free(r);
        }
        printf("%s  rtl caret order   start right of end\n", CHECK(ok));
    }
    {
        /* byte_at_x is the exact inverse of caret_x, and caret x is monotonic */
        const char *s8 = "Hello world";
        int len = (int)strlen(s8);
        mt_shaped *s = mt_shape(f, s8, -1, ink);
        int ok = s != NULL;
        float prev = -1.0f;
        for (int i = 0; ok && i <= len; i++) {
            float cx = mt_shaped_caret_x(s, i);
            if (cx < prev - 0.5f || mt_shaped_byte_at_x(s, cx) != i) {
                ok = 0;
            }
            prev = cx;
        }
        printf("%s  caret sweep       inverse over %d bytes\n", CHECK(ok), len);
        mt_shaped_free(s);
    }
    {
        /* an empty run still carries the font's height in its bitmap */
        mt_shaped *e = mt_shape(f, "", 0, ink);
        int ok = e != NULL;
        if (e) {
            int ew, eh;
            mt_shaped_size(e, &ew, &eh);
            mt_metrics m = mt_shaped_metrics(e);
            ok = m.width == 0.0f && eh >= (int)(m.ascent + m.descent);
            mt_shaped_free(e);
        }
        printf("%s  empty line height ascent+descent fallback\n", CHECK(ok));
    }
    /* error channel */
    int w, h;
    int pass = 1;
    if (mt_render(f, NULL, -1, ink, &w, &h, NULL) != NULL ||
        mt_last_error() != MT_ERR_TEXT) {
        pass = 0;
    }
    const char bad[] = { (char)0xff, (char)0xfe, 0 };
    if (mt_render(f, bad, -1, ink, &w, &h, NULL) != NULL ||
        mt_last_error() != MT_ERR_TEXT) {
        pass = 0;
    }
    if (mt_render(NULL, "x", -1, ink, &w, &h, NULL) != NULL ||
        mt_last_error() != MT_ERR_FONT) {
        pass = 0;
    }
    printf("%s  error channel (TEXT/TEXT/FONT distinguished)\n", CHECK(pass));
    {
        /* malformed UTF-8 is rejected with MT_ERR_TEXT, not shaped */
        static const char *const malformed[] = {
            "\x80",                  /* lone continuation byte */
            "\xC3",                  /* truncated 2-byte sequence */
            "\xE4\xB8",              /* truncated 3-byte sequence */
            "\xC0\x80",              /* overlong encoding */
            "\xED\xA0\x80",          /* lone surrogate U+D800 */
            "\xF8\x88\x80\x80\x80",  /* 5-byte sequence */
        };
        int bad_ok = 1;
        size_t cases = sizeof(malformed) / sizeof(malformed[0]);
        for (size_t i = 0; i < cases; i++) {
            if (mt_measure(f, malformed[i], -1).width != 0.0f ||
                mt_last_error() != MT_ERR_TEXT) {
                bad_ok = 0;
            }
        }
        printf("%s  malformed utf-8   %zu vectors rejected\n", CHECK(bad_ok),
               cases);
    }

    if (g_skips) {
        printf("(%d skipped)\n", g_skips);
    }
    mt_font_close(f);
    return g_fails ? 1 : 0;
}
