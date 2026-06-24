/*
 * mt_raylib.h - raylib glue for microtext.
 *
 * Shows the consumer side of the contract: microtext returns pixels, the
 * consumer uploads and caches them. mtr_text renders a string to a cached
 * Texture2D keyed by (string, color) and draws it. The cache lives here, not
 * in the library, so the core stays renderer agnostic.
 *
 * The cache is a demo-only toy, not a production design: keys are truncated to
 * 256 bytes so long or shared-prefix strings collide or never match, eviction
 * is round-robin rather than LRU, and lookup is a linear scan. A real consumer
 * keys on a hash of the full bytes plus color and touches entries on hit.
 *
 *   #define MT_RAYLIB_IMPLEMENTATION   // in exactly one .c
 *   #include "mt_raylib.h"
 */
#ifndef MT_RAYLIB_H
#define MT_RAYLIB_H

#include "../microtext.h"
#include "raylib.h"

// Draw a UTF-8 run with its top-left at (x, y).
void mtr_text(mt_font *f, const char *utf8, float x, float y, mt_color color);
// Release every cached texture.
void mtr_shutdown(void);

#endif  // MT_RAYLIB_H

#ifdef MT_RAYLIB_IMPLEMENTATION
#ifndef MT_RAYLIB_IMPLEMENTATION_ONCE
#define MT_RAYLIB_IMPLEMENTATION_ONCE

#include <stdio.h>
#include <string.h>

#define MTR_CACHE 128

typedef struct {
    char key[256];
    unsigned int color;
    Texture2D tex;
    int used;
} mtr_entry;

static mtr_entry mtr_cache[MTR_CACHE];
static unsigned int mtr_clock;

static Texture2D mtr_get(mt_font *f, const char *utf8, mt_color c)
{
    unsigned int col = ((unsigned)c.r << 24) | ((unsigned)c.g << 16) |
                       ((unsigned)c.b << 8) | (unsigned)c.a;
    for (int i = 0; i < MTR_CACHE; i++) {
        if (mtr_cache[i].used && mtr_cache[i].color == col &&
            strcmp(mtr_cache[i].key, utf8) == 0) {
            return mtr_cache[i].tex;
        }
    }
    int w, h;
    unsigned char *px = mt_render(f, utf8, -1, c, &w, &h, NULL);
    Image img = { px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    mt_free(px);

    int slot = (int)(mtr_clock++ % MTR_CACHE);
    if (mtr_cache[slot].used) {
        UnloadTexture(mtr_cache[slot].tex);
    }
    snprintf(mtr_cache[slot].key, sizeof(mtr_cache[slot].key), "%s", utf8);
    mtr_cache[slot].color = col;
    mtr_cache[slot].tex = tex;
    mtr_cache[slot].used = 1;
    return tex;
}

void mtr_text(mt_font *f, const char *utf8, float x, float y, mt_color color)
{
    Texture2D tex = mtr_get(f, utf8, color);
    DrawTexture(tex, (int)x, (int)y, WHITE);
}

void mtr_shutdown(void)
{
    for (int i = 0; i < MTR_CACHE; i++) {
        if (mtr_cache[i].used) {
            UnloadTexture(mtr_cache[i].tex);
            mtr_cache[i].used = 0;
        }
    }
}

#endif  // MT_RAYLIB_IMPLEMENTATION_ONCE
#endif  // MT_RAYLIB_IMPLEMENTATION
