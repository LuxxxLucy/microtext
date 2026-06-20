/* Golden-image regression: render fixed cases and compare to committed PNGs.
 * MICROTEXT_UPDATE_GOLDEN=1 regenerates the goldens instead of comparing.
 * Goldens are tied to the macOS and font version that produced them. */
#define MICROTEXT_IMPLEMENTATION
#include "../microtext.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "3rd/stb_image_write.h"
#define STB_IMAGE_IMPLEMENTATION
#include "3rd/stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

// per-pixel max-abs-diff allowed, to absorb sub-pixel anti-aliasing noise
#define TOL 16

static int g_fails;

typedef struct {
    const char *name;
    const char *font;
    float px;
    const char *text;
} gcase;

static void check(const char *name, unsigned char *px, int w, int h)
{
    char path[256];
    snprintf(path, sizeof(path), "tests/golden/%s.png", name);
    if (getenv("MICROTEXT_UPDATE_GOLDEN")) {
        stbi_write_png(path, w, h, 4, px, w * 4);
        printf("wrote %-10s %dx%d\n", name, w, h);
        return;
    }
    int gw, gh, gn;
    unsigned char *g = stbi_load(path, &gw, &gh, &gn, 4);
    if (!g) {
        printf(
            "FAIL  %-10s no golden; run `MICROTEXT_UPDATE_GOLDEN=1 make "
            "golden`\n",
            name);
        g_fails++;
        return;
    }
    int maxd = 0;
    int ok = gw == w && gh == h;
    if (ok) {
        for (long i = 0; i < (long)w * h * 4; i++) {
            int d = px[i] > g[i] ? px[i] - g[i] : g[i] - px[i];
            if (d > maxd) {
                maxd = d;
            }
        }
        ok = maxd <= TOL;
    }
    if (!ok) {
        g_fails++;
    }
    printf("%s  %-10s %dx%d maxdiff=%d\n", ok ? "ok   " : "FAIL ", name, w, h,
           maxd);
    stbi_image_free(g);
}

int main(void)
{
    static const gcase cases[] = {
        { "latin", "Helvetica Neue", 48.0f, "Hello, microtext" },
        { "cjk", "Helvetica Neue", 48.0f,
          "\xE4\xB8\xAD\xE6\x96\x87 \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" },
        { "overhang", "Helvetica Neue", 48.0f, "Ягfيp" },
    };
    mt_color ink = { 20, 20, 20, 255 };
    mkdir("tests/golden", 0755);
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mt_font *f = mt_font_open(cases[i].font, cases[i].px);
        if (!f) {
            printf("SKIP  %-10s %s unavailable\n", cases[i].name,
                   cases[i].font);
            continue;
        }
        int w, h;
        unsigned char *px = mt_render(f, cases[i].text, -1, ink, &w, &h, NULL);
        if (px) {
            check(cases[i].name, px, w, h);
            mt_free(px);
        }
        mt_font_close(f);
    }
    return g_fails ? 1 : 0;
}
