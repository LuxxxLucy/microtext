/* Render mixed-script, bidi, emoji, wrapped, and rich text with microtext. */
#define MICROTEXT_IMPLEMENTATION
#define MT_RAYLIB_IMPLEMENTATION
#include "mt_raylib.h"

#include "raylib.h"

#define MAX_LINES 32 /* per-paragraph line cap */

/* A wrapped block baked to one texture per line, laid out once. */
typedef struct {
    Texture2D tex;
    int dx, dy;
} demo_line;
typedef struct {
    demo_line lines[MAX_LINES];
    int n;
    float h;
} demo_para;

static demo_para bake(mt_block *b)
{
    demo_para p = { 0 };
    int y = 0;
    for (int i = 0; i < mt_block_lines(b) && i < MAX_LINES; i++) {
        const mt_shaped *ln = mt_block_line(b, i);
        mt_metrics m = mt_shaped_metrics(ln);
        int w, h;
        unsigned char *px = mt_shaped_render(ln, NULL, &w, &h, NULL);
        Image img = { px, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        Texture2D tex = LoadTextureFromImage(img);
        SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
        mt_free(px);
        p.lines[p.n].tex = tex;
        p.lines[p.n].dx = -m.origin_x;
        p.lines[p.n].dy = y + (int)(m.ascent + 0.5f) - m.origin_y;
        p.n++;
        y += (int)(m.height + 0.5f);
    }
    p.h = (float)y;
    return p;
}

static void draw_para(const demo_para *p, float x, float y)
{
    for (int i = 0; i < p->n; i++) {
        DrawTexture(p->lines[i].tex, (int)x + p->lines[i].dx,
                    (int)y + p->lines[i].dy, WHITE);
    }
}

static void free_para(demo_para *p)
{
    for (int i = 0; i < p->n; i++) {
        UnloadTexture(p->lines[i].tex);
    }
}

int main(void)
{
    InitWindow(960, 660, "microtext + raylib");
    SetTargetFPS(60);

    mt_font *big = mt_font_open("Helvetica Neue", 56.0f);
    mt_font *body = mt_font_open("Helvetica Neue", 38.0f);
    mt_font *bold = mt_font_open_styled("Helvetica Neue", 38.0f, 1, 0);
    mt_font *serif = mt_font_open("Baskerville", 38.0f);
    mt_color ink = { 28, 28, 36, 255 };
    mt_color sub = { 120, 120, 132, 255 };
    mt_color red = { 200, 44, 44, 255 };

    /* rich: several fonts, colors, and an OpenType feature on one line */
    mt_text *rt = mt_text_new();
    mt_text_run(rt, "rich runs: ", -1, body, sub, NULL);
    mt_text_run(rt, "bold ", -1, bold, ink, NULL);
    mt_text_run(rt, "red ", -1, body, red, NULL);
    mt_text_run(rt, "small caps", -1, serif, ink, "smcp");
    mt_block *rb = mt_text_wrap(rt, 0.0f);
    demo_para rich = bake(rb);
    mt_block_free(rb);
    mt_text_free(rt);

    /* wrapped paragraph with a hard break and mixed scripts */
    mt_text *pt = mt_text_new();
    mt_text_run(pt,
                "microtext wraps this paragraph to a fixed width and obeys\n"
                "hard breaks, mixing 中文 and العربية inside one block.",
                -1, body, ink, NULL);
    mt_block *pb = mt_text_wrap(pt, 860.0f);
    demo_para para = bake(pb);
    mt_block_free(pb);
    mt_text_free(pt);

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 250, 250, 252, 255 });
        float x = 44, y = 24;
        mtr_text(big, "microtext", x, y, ink);
        y += 86;
        mtr_text(body, "English  中文  日本語  한국어", x, y, ink);
        y += 54;
        mtr_text(body, "العربية  עברית  bidi RTL", x, y, ink);
        y += 54;
        mtr_text(body, "Hello مرحبا \U0001F30D 世界 \U0001F3B5", x, y, ink);
        y += 54;
        mtr_text(body,
                 "\U0001F3B5 \U0001F525 \U0001F30D \U0001F600 "
                 "\U0001F44D \U0001F3B8 ✨",
                 x, y, ink);
        y += 58;
        draw_para(&rich, x, y);
        y += rich.h + 10;
        draw_para(&para, x, y);

        DrawFPS(44, 624);
        EndDrawing();
    }

    free_para(&rich);
    free_para(&para);
    mtr_shutdown();
    mt_font_close(big);
    mt_font_close(body);
    mt_font_close(bold);
    mt_font_close(serif);
    CloseWindow();
    return 0;
}
