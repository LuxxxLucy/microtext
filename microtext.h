/*
 * microtext.h - native text shaping and rasterization in one header.
 *
 * One UTF-8 string in, one laid-out RGBA bitmap out. Bidi, complex shaping,
 * CJK, color emoji, and font fallback come from the operating system's own
 * text engine, so the visual result matches native apps. The core is renderer
 * agnostic: it returns pixels and knows nothing about any GPU or window
 * toolkit. Upload the bitmap however you like.
 *
 *   #define MICROTEXT_IMPLEMENTATION
 *   #include "microtext.h"
 *
 *   mt_font *f = mt_font_open("Helvetica Neue", 40.0f);
 *   int w, h; mt_metrics m;
 *   unsigned char *rgba = mt_render(f, "Hello 你好 \U0001F3B5", -1,
 *                                   (mt_color){20, 20, 20, 255}, &w, &h, &m);
 *   // ... upload rgba (w*h, 8-bit sRGB RGBA, straight alpha, top row first) ...
 *   mt_free(rgba);
 *   mt_font_close(f);
 *
 * Backends: macOS uses CoreText + CoreGraphics (link -framework CoreText
 * -framework CoreGraphics -framework CoreFoundation; no Objective-C). Windows
 * (DirectWrite + Direct2D) and Linux (FreeType/HarfBuzz/fontconfig) are not yet
 * written.
 *
 * Builds as C99. Not thread-safe: the last-error slot and an internal sRGB
 * color space are process-global. To use the library from more than one thread,
 * shape on one thread and hand the mt_shaped off to another (no shared state is
 * touched after shaping); otherwise serialize the calls.
 *
 * MIT licensed; see LICENSE. No warranty.
 */
#ifndef MICROTEXT_H
#define MICROTEXT_H

#include <stddef.h>  /* ptrdiff_t */

/* Monotonic feature level, bumped on every change to the public surface. The
 * surface only grows: new functions and appended struct fields, never a
 * signature change, so a higher value is a strict superset of a lower one. */
#define MICROTEXT_VERSION 1

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mt_font mt_font;

typedef struct {
    unsigned char r, g, b, a;
} mt_color;

/* width is the laid-out advance; height is ascent+descent+leading; the other
 * three are font vertical metrics in pixels.
 *
 * origin_x and origin_y locate the pen origin inside a bitmap returned by
 * mt_render: the baseline sits origin_y rows below the top, and the start pen
 * x sits origin_x columns from the left. To place a run with its pen at
 * (px, py) where py is the baseline, blit the bitmap top-left at
 * (px - origin_x, py - origin_y). Aligning two runs of different sizes is then
 * a matter of sharing py. mt_measure leaves origin_x/origin_y zero; they are
 * meaningful only on a rendered bitmap. */
typedef struct {
    float width, height;
    float ascent, descent, leading;
    int origin_x, origin_y;
} mt_metrics;

typedef enum {
    MT_OK = 0,
    MT_ERR_FONT,    /* font handle NULL, or the family/data did not resolve */
    MT_ERR_TEXT,    /* text NULL or not valid UTF-8 */
    MT_ERR_OOM,     /* out of memory */
    MT_ERR_BACKEND  /* the platform text engine refused the request */
} mt_error;

/* The error from the last microtext call. Process-global, not per-thread (see
 * the threading note at the top of the file). */
mt_error mt_last_error(void);

/* Open a font by family name at a pixel size. NULL family uses the system UI
 * font. Returns NULL on failure (see mt_last_error). */
mt_font *mt_font_open(const char *family, float pixel_size);
/* Same, selecting a bold and/or italic style. A requested style the family
 * lacks falls back to the regular face. */
mt_font *mt_font_open_styled(const char *family, float pixel_size, int bold,
                             int italic);
/* Open a font from an in-memory .ttf/.otf, so an app can ship its own. The
 * bytes are copied; the caller may free them after the call. */
mt_font *mt_font_open_memory(const void *data, size_t len, float pixel_size);
void mt_font_close(mt_font *f);

/* Both calls lay out exactly one line. Embedded newlines are not breaks; to
 * draw a paragraph, split on '\n' yourself and stack the lines, advancing the
 * pen y by mt_metrics.height per line. An empty run yields a minimal
 * transparent bitmap with metrics width 0. len < 0 means NUL-terminated. */

/* Measure a UTF-8 run without rendering. origin_x/origin_y are left zero. */
mt_metrics mt_measure(const mt_font *f, const char *utf8, ptrdiff_t len);

/* Render a UTF-8 run to a freshly allocated bitmap of 8-bit sRGB RGBA, straight
 * alpha, top row first, sized to the glyph ink. Fills *out_w, *out_h, and
 * *out_m (out_m may be NULL); out_m carries the pen origin inside the bitmap.
 * Free the result with mt_free. color tints non-color glyphs; color emoji keep
 * their own colors. Returns NULL on failure. len < 0 means NUL-terminated.
 * The conversion from premultiplied alpha loses color precision at very low
 * alpha. pixel_size is in physical pixels: on a 2x display, open the font at
 * the logical size times the backing scale and draw one texel per pixel. */
unsigned char *mt_render(const mt_font *f, const char *utf8, ptrdiff_t len,
                         mt_color color, int *out_w, int *out_h,
                         mt_metrics *out_m);

void mt_free(void *bitmap);

/* A run shaped once and reused, so measuring then rendering the same bytes
 * does not pay the shaping cost twice, and an atlas builder can render into a
 * buffer it owns. color is baked in at shape time. */
typedef struct mt_shaped mt_shaped;
mt_shaped *mt_shape(const mt_font *f, const char *utf8, ptrdiff_t len,
                    mt_color color);
/* Metrics including the pen origin and the bitmap size mt_shaped_render uses. */
mt_metrics mt_shaped_metrics(const mt_shaped *s);
void mt_shaped_size(const mt_shaped *s, int *w, int *h);
/* Render the shaped run. If dst is non-NULL it must hold w*h*4 bytes for the
 * size from mt_shaped_size, with row stride w*4; otherwise a buffer is
 * allocated. Returns the buffer (dst when given) or NULL. */
unsigned char *mt_shaped_render(const mt_shaped *s, unsigned char *dst,
                                int *out_w, int *out_h, mt_metrics *out_m);
void mt_shaped_free(mt_shaped *s);

/* Rich, multi-run text and width-based line wrapping.
 *
 * Build text from styled runs, then lay it out: a width <= 0 wraps only at
 * hard breaks, a positive width also wraps to that many pixels. Each resulting
 * line is an mt_shaped, rendered and queried exactly like a single run; stack
 * them by advancing the pen y by each line's mt_metrics.height. The Unicode
 * mandatory breaks (UAX #14: LF, VT, FF, CR, CRLF, NEL, LS, PS) start a new
 * line; a blank line keeps the font's height. */
typedef struct mt_text mt_text;
typedef struct mt_block mt_block;

/* Start an empty paragraph. Free with mt_text_free. Returns NULL on OOM. */
mt_text *mt_text_new(void);
/* Append a styled run. features is a space-separated list of OpenType feature
 * tags to enable (e.g. "smcp tnum frac"), or NULL for none. Returns 0 on
 * success, -1 on failure (see mt_last_error). len < 0 means NUL-terminated. */
int mt_text_run(mt_text *t, const char *utf8, ptrdiff_t len, const mt_font *f,
                mt_color color, const char *features);
void mt_text_free(mt_text *t);

/* Lay the text out: wrap to max_width pixels (<= 0 wraps only at hard breaks)
 * and split at mandatory breaks. Returns a list of lines, each an mt_shaped;
 * free with mt_block_free. Returns NULL on failure; empty text yields a block
 * of zero lines. */
mt_block *mt_text_wrap(const mt_text *t, float max_width);
int mt_block_lines(const mt_block *b);
/* Borrow line i (0-based); owned by the block, valid until mt_block_free. */
mt_shaped *mt_block_line(const mt_block *b, int i);
void mt_block_free(mt_block *b);

#ifdef __cplusplus
}
#endif

#endif /* MICROTEXT_H */

/* ------------------------------------------------------------------------- */

#ifdef MICROTEXT_IMPLEMENTATION
#ifndef MICROTEXT_IMPLEMENTATION_ONCE
#define MICROTEXT_IMPLEMENTATION_ONCE

#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

struct mt_font {
    CTFontRef ct;
};

static mt_error mt_err;
mt_error mt_last_error(void) { return mt_err; }

/* One process-wide sRGB color space for both glyph color and bitmap context,
 * lazily initialized and never released. Unsynchronized, matching the library's
 * single-threaded contract. */
static CGColorSpaceRef mt_srgb(void)
{
    static CGColorSpaceRef space;
    if (!space) {
        space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    }
    return space;
}

/* Take ownership of ct and box it; releases ct on allocation failure. */
static mt_font *mt_wrap(CTFontRef ct)
{
    if (!ct) {
        mt_err = MT_ERR_FONT;
        return NULL;
    }
    mt_font *f = (mt_font *)calloc(1, sizeof(*f));
    if (!f) {
        CFRelease(ct);
        mt_err = MT_ERR_OOM;
        return NULL;
    }
    f->ct = ct;
    mt_err = MT_OK;
    return f;
}

mt_font *mt_font_open_styled(const char *family, float pixel_size, int bold,
                             int italic)
{
    CTFontRef base = NULL;
    if (family) {
        CFStringRef name = CFStringCreateWithCString(NULL, family,
                                                     kCFStringEncodingUTF8);
        if (name) {
            base = CTFontCreateWithName(name, pixel_size, NULL);
            CFRelease(name);
        }
    } else {
        base = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, pixel_size,
                                             NULL);
    }
    if (!base) {
        mt_err = MT_ERR_FONT;
        return NULL;
    }
    CTFontSymbolicTraits traits = 0, mask = 0;
    if (bold) {
        traits |= kCTFontBoldTrait;
        mask |= kCTFontBoldTrait;
    }
    if (italic) {
        traits |= kCTFontItalicTrait;
        mask |= kCTFontItalicTrait;
    }
    if (mask) {
        CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(base, pixel_size,
                                                              NULL, traits, mask);
        if (styled) {  /* a missing style keeps the regular face */
            CFRelease(base);
            base = styled;
        }
    }
    return mt_wrap(base);
}

mt_font *mt_font_open(const char *family, float pixel_size)
{
    return mt_font_open_styled(family, pixel_size, 0, 0);
}

mt_font *mt_font_open_memory(const void *data, size_t len, float pixel_size)
{
    if (!data || !len) {
        mt_err = MT_ERR_FONT;
        return NULL;
    }
    CFDataRef d = CFDataCreate(NULL, (const UInt8 *)data, (CFIndex)len);
    if (!d) {
        mt_err = MT_ERR_OOM;
        return NULL;
    }
    CTFontDescriptorRef desc = CTFontManagerCreateFontDescriptorFromData(d);
    CFRelease(d);
    if (!desc) {
        mt_err = MT_ERR_FONT;
        return NULL;
    }
    CTFontRef ct = CTFontCreateWithFontDescriptor(desc, pixel_size, NULL);
    CFRelease(desc);
    return mt_wrap(ct);
}

void mt_font_close(mt_font *f)
{
    if (!f) {
        return;
    }
    if (f->ct) {
        CFRelease(f->ct);
    }
    free(f);
}

/* Build a shaped line; CoreText applies bidi, fallback, and color glyphs.
 * Sets mt_err on every failure path. */
static CTLineRef mt_line(const mt_font *f, const char *utf8, ptrdiff_t len,
                         mt_color c)
{
    if (!utf8) {
        mt_err = MT_ERR_TEXT;
        return NULL;
    }
    CFIndex n = len < 0 ? (CFIndex)strlen(utf8) : (CFIndex)len;
    CFStringRef s = CFStringCreateWithBytes(NULL, (const UInt8 *)utf8, n,
                                            kCFStringEncodingUTF8, false);
    if (!s) {
        mt_err = MT_ERR_TEXT;
        return NULL;
    }
    CGFloat comps[4] = { c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0 };
    CGColorRef color = CGColorCreate(mt_srgb(), comps);
    if (!color) {
        CFRelease(s);
        mt_err = MT_ERR_OOM;
        return NULL;
    }

    CFStringRef keys[2] = { kCTFontAttributeName,
                            kCTForegroundColorAttributeName };
    CFTypeRef vals[2] = { f->ct, color };
    CFDictionaryRef attrs = CFDictionaryCreate(
        NULL, (const void **)keys, (const void **)vals, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CTLineRef line = NULL;
    if (attrs) {
        CFAttributedStringRef as = CFAttributedStringCreate(NULL, s, attrs);
        if (as) {
            line = CTLineCreateWithAttributedString(as);
            CFRelease(as);
        }
        CFRelease(attrs);
    }
    CGColorRelease(color);
    CFRelease(s);
    if (!line) {
        mt_err = MT_ERR_OOM;
    }
    return line;
}

#define MT_BLEED 1.0

struct mt_shaped {
    CTLineRef line;
    mt_metrics m;     /* origin filled; m.width is the advance */
    int w, h;         /* bitmap size */
    double pen_x, pen_y;  /* text position that lands the ink in the bitmap */
};

/* Box a CTLine into mt_shaped, taking ownership of line. Derives the ink-sized
 * bitmap and the pen position that lands the ink inside it. */
static mt_shaped *mt_shaped_from_line(CTLineRef line)
{
    mt_shaped *s = (mt_shaped *)calloc(1, sizeof(*s));
    if (!s) {
        CFRelease(line);
        mt_err = MT_ERR_OOM;
        return NULL;
    }
    s->line = line;
    CGFloat ascent = 0, descent = 0, leading = 0;
    double advance = CTLineGetTypographicBounds(line, &ascent, &descent,
                                                &leading);
    /* Size to the ink, not the advance, so overhang does not clip. Text-space
     * box [x0,x1] x [y0,y1], y up, origin at the pen baseline. */
    CGRect ink = CTLineGetImageBounds(line, NULL);
    double x0, y0, x1, y1;
    if (CGRectIsNull(ink) || CGRectIsEmpty(ink)) {
        x0 = -MT_BLEED;
        y0 = -descent - MT_BLEED;
        x1 = advance + MT_BLEED;
        y1 = ascent + MT_BLEED;
    } else {
        x0 = floor(ink.origin.x - MT_BLEED);
        y0 = floor(ink.origin.y - MT_BLEED);
        x1 = ceil(ink.origin.x + ink.size.width + MT_BLEED);
        y1 = ceil(ink.origin.y + ink.size.height + MT_BLEED);
    }
    s->w = (int)(x1 - x0) < 1 ? 1 : (int)(x1 - x0);
    s->h = (int)(y1 - y0) < 1 ? 1 : (int)(y1 - y0);
    s->pen_x = -x0;
    s->pen_y = -y0;
    s->m.width = (float)advance;
    s->m.ascent = (float)ascent;
    s->m.descent = (float)descent;
    s->m.leading = (float)leading;
    s->m.height = (float)(ascent + descent + leading);
    s->m.origin_x = (int)(-x0);
    s->m.origin_y = (int)y1;
    mt_err = MT_OK;
    return s;
}

mt_shaped *mt_shape(const mt_font *f, const char *utf8, ptrdiff_t len,
                    mt_color color)
{
    if (!f) {
        mt_err = MT_ERR_FONT;
        return NULL;
    }
    CTLineRef line = mt_line(f, utf8, len, color);
    if (!line) {
        return NULL;
    }
    return mt_shaped_from_line(line);
}

mt_metrics mt_shaped_metrics(const mt_shaped *s)
{
    if (s) {
        return s->m;
    }
    mt_metrics m;
    memset(&m, 0, sizeof(m));
    return m;
}

void mt_shaped_size(const mt_shaped *s, int *w, int *h)
{
    if (w) {
        *w = s ? s->w : 0;
    }
    if (h) {
        *h = s ? s->h : 0;
    }
}

unsigned char *mt_shaped_render(const mt_shaped *s, unsigned char *dst,
                                int *out_w, int *out_h, mt_metrics *out_m)
{
    if (!s) {
        mt_err = MT_ERR_FONT;
        return NULL;
    }
    int w = s->w, h = s->h;
    unsigned char *buf = dst;
    if (buf) {
        memset(buf, 0, (size_t)w * h * 4);
    } else {
        buf = (unsigned char *)calloc((size_t)w * h, 4);
        if (!buf) {
            mt_err = MT_ERR_OOM;
            return NULL;
        }
    }
    CGContextRef ctx = CGBitmapContextCreate(buf, w, h, 8, (size_t)w * 4,
                                             mt_srgb(),
                                             kCGImageAlphaPremultipliedLast);
    if (!ctx) {
        if (!dst) {
            free(buf);
        }
        mt_err = MT_ERR_BACKEND;
        return NULL;
    }
    CGContextSetTextPosition(ctx, (CGFloat)s->pen_x, (CGFloat)s->pen_y);
    CTLineDraw(s->line, ctx);
    CGContextRelease(ctx);

    /* CoreGraphics stores premultiplied alpha; convert to straight. */
    for (size_t i = 0, px = (size_t)w * h; i < px; i++) {
        unsigned char a = buf[i * 4 + 3];
        if (a != 0 && a != 255) {
            for (int k = 0; k < 3; k++) {
                int v = (buf[i * 4 + k] * 255 + a / 2) / a;
                buf[i * 4 + k] = v > 255 ? 255 : (unsigned char)v;
            }
        }
    }
    if (out_w) {
        *out_w = w;
    }
    if (out_h) {
        *out_h = h;
    }
    if (out_m) {
        *out_m = s->m;
    }
    mt_err = MT_OK;
    return buf;
}

void mt_shaped_free(mt_shaped *s)
{
    if (!s) {
        return;
    }
    if (s->line) {
        CFRelease(s->line);
    }
    free(s);
}

mt_metrics mt_measure(const mt_font *f, const char *utf8, ptrdiff_t len)
{
    mt_color black = { 0, 0, 0, 255 };
    mt_shaped *s = mt_shape(f, utf8, len, black);
    mt_metrics m;
    memset(&m, 0, sizeof(m));
    if (!s) {
        return m;
    }
    m = s->m;
    m.origin_x = 0;
    m.origin_y = 0;
    mt_shaped_free(s);
    mt_err = MT_OK;
    return m;
}

unsigned char *mt_render(const mt_font *f, const char *utf8, ptrdiff_t len,
                         mt_color color, int *out_w, int *out_h,
                         mt_metrics *out_m)
{
    mt_shaped *s = mt_shape(f, utf8, len, color);
    if (!s) {
        return NULL;
    }
    unsigned char *buf = mt_shaped_render(s, NULL, out_w, out_h, out_m);
    mt_shaped_free(s);
    return buf;
}

/* Copy base with the given space-separated OpenType feature tags enabled.
 * features NULL or empty returns a retained copy of base. */
static CTFontRef mt_font_with_features(CTFontRef base, const char *features)
{
    if (!features || !*features) {
        return (CTFontRef)CFRetain(base);
    }
    CFMutableArrayRef arr =
        CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if (!arr) {
        return (CTFontRef)CFRetain(base);
    }
    for (const char *p = features; *p;) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        long taglen = p - start;
        if (taglen < 1 || taglen > 4) {
            continue;
        }
        CFStringRef tag = CFStringCreateWithBytes(
            NULL, (const UInt8 *)start, taglen, kCFStringEncodingUTF8, false);
        int one = 1;
        CFNumberRef val = CFNumberCreate(NULL, kCFNumberIntType, &one);
        if (tag && val) {
            CFStringRef keys[2] = { kCTFontOpenTypeFeatureTag,
                                    kCTFontOpenTypeFeatureValue };
            CFTypeRef vals[2] = { tag, val };
            CFDictionaryRef d = CFDictionaryCreate(
                NULL, (const void **)keys, (const void **)vals, 2,
                &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks);
            if (d) {
                CFArrayAppendValue(arr, d);
                CFRelease(d);
            }
        }
        if (tag) {
            CFRelease(tag);
        }
        if (val) {
            CFRelease(val);
        }
    }
    CTFontRef out = NULL;
    if (CFArrayGetCount(arr) > 0) {
        CFStringRef k = kCTFontFeatureSettingsAttribute;
        CFTypeRef v = arr;
        CFDictionaryRef da = CFDictionaryCreate(
            NULL, (const void **)&k, (const void **)&v, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (da) {
            CTFontDescriptorRef desc = CTFontDescriptorCreateWithAttributes(da);
            if (desc) {
                out = CTFontCreateCopyWithAttributes(base, 0.0, NULL, desc);
                CFRelease(desc);
            }
            CFRelease(da);
        }
    }
    CFRelease(arr);
    return out ? out : (CTFontRef)CFRetain(base);
}

struct mt_text {
    CFMutableAttributedStringRef as;
};

mt_text *mt_text_new(void)
{
    mt_text *t = (mt_text *)calloc(1, sizeof(*t));
    if (!t) {
        mt_err = MT_ERR_OOM;
        return NULL;
    }
    t->as = CFAttributedStringCreateMutable(NULL, 0);
    if (!t->as) {
        free(t);
        mt_err = MT_ERR_OOM;
        return NULL;
    }
    mt_err = MT_OK;
    return t;
}

int mt_text_run(mt_text *t, const char *utf8, ptrdiff_t len, const mt_font *f,
                mt_color color, const char *features)
{
    if (!t || !f) {
        mt_err = MT_ERR_FONT;
        return -1;
    }
    if (!utf8) {
        mt_err = MT_ERR_TEXT;
        return -1;
    }
    CFIndex n = len < 0 ? (CFIndex)strlen(utf8) : (CFIndex)len;
    CFStringRef s = CFStringCreateWithBytes(NULL, (const UInt8 *)utf8, n,
                                            kCFStringEncodingUTF8, false);
    if (!s) {
        mt_err = MT_ERR_TEXT;
        return -1;
    }
    CTFontRef font = mt_font_with_features(f->ct, features);
    CGFloat comps[4] = { color.r / 255.0, color.g / 255.0, color.b / 255.0,
                         color.a / 255.0 };
    CGColorRef col = CGColorCreate(mt_srgb(), comps);
    int rc = -1;
    if (col) {
        CFStringRef keys[2] = { kCTFontAttributeName,
                                kCTForegroundColorAttributeName };
        CFTypeRef vals[2] = { font, col };
        CFDictionaryRef attrs = CFDictionaryCreate(
            NULL, (const void **)keys, (const void **)vals, 2,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if (attrs) {
            CFAttributedStringRef run = CFAttributedStringCreate(NULL, s, attrs);
            if (run) {
                CFIndex cur = CFAttributedStringGetLength(t->as);
                CFAttributedStringReplaceAttributedString(
                    t->as, CFRangeMake(cur, 0), run);
                CFRelease(run);
                rc = 0;
            }
            CFRelease(attrs);
        }
        CGColorRelease(col);
    }
    if (font) {
        CFRelease(font);
    }
    CFRelease(s);
    mt_err = rc == 0 ? MT_OK : MT_ERR_OOM;
    return rc;
}

void mt_text_free(mt_text *t)
{
    if (!t) {
        return;
    }
    if (t->as) {
        CFRelease(t->as);
    }
    free(t);
}

struct mt_block {
    mt_shaped **lines;
    int n;
};

/* Length of the mandatory line break (UAX #14) at index i, or 0 if none. CRLF
 * counts as one break of length 2. */
static int mt_break_len(const UniChar *c, CFIndex i, CFIndex total)
{
    UniChar u = c[i];
    if (u == 0x000D) {  /* CR, possibly CRLF */
        return (i + 1 < total && c[i + 1] == 0x000A) ? 2 : 1;
    }
    if (u == 0x000A || u == 0x000B || u == 0x000C || u == 0x0085 ||
        u == 0x2028 || u == 0x2029) {
        return 1;
    }
    return 0;
}

mt_block *mt_text_wrap(const mt_text *t, float max_width)
{
    if (!t) {
        mt_err = MT_ERR_TEXT;
        return NULL;
    }
    mt_block *b = (mt_block *)calloc(1, sizeof(*b));
    if (!b) {
        mt_err = MT_ERR_OOM;
        return NULL;
    }
    CFIndex total = CFAttributedStringGetLength(t->as);
    if (total == 0) {
        mt_err = MT_OK;
        return b;  /* empty paragraph: zero lines */
    }
    CTTypesetterRef ts = CTTypesetterCreateWithAttributedString(t->as);
    if (!ts) {
        free(b);
        mt_err = MT_ERR_BACKEND;
        return NULL;
    }
    CFStringRef str = CFAttributedStringGetString(t->as);
    UniChar *chars = (UniChar *)malloc((size_t)total * sizeof(UniChar));
    if (!chars) {
        CFRelease(ts);
        free(b);
        mt_err = MT_ERR_OOM;
        return NULL;
    }
    CFStringGetCharacters(str, CFRangeMake(0, total), chars);

    double width = max_width > 0 ? (double)max_width : 1e7;
    int cap = 0;
    for (CFIndex start = 0; start < total;) {
        /* Span the current paragraph up to the next mandatory break. */
        CFIndex mb = start;
        int mblen = 0;
        while (mb < total) {
            mblen = mt_break_len(chars, mb, total);
            if (mblen) {
                break;
            }
            mb++;
        }
        CFIndex count = CTTypesetterSuggestLineBreak(ts, start, width);
        CFIndex end;
        if (count <= 0 || start + count >= mb) {
            /* The paragraph's remainder fits; carry the break char into the
             * line so a blank line keeps the font's height. */
            end = mb < total ? mb + mblen : mb;
        } else {
            end = start + count;  /* width forces a break before the hard one */
        }
        CTLineRef line =
            CTTypesetterCreateLine(ts, CFRangeMake(start, end - start));
        if (!line) {
            mt_err = MT_ERR_BACKEND;
            goto fail;
        }
        if (b->n == cap) {
            int ncap = cap ? cap * 2 : 8;
            mt_shaped **nl = (mt_shaped **)realloc(
                b->lines, (size_t)ncap * sizeof(*nl));
            if (!nl) {
                CFRelease(line);
                mt_err = MT_ERR_OOM;
                goto fail;
            }
            b->lines = nl;
            cap = ncap;
        }
        mt_shaped *sh = mt_shaped_from_line(line);  /* takes ownership */
        if (!sh) {
            goto fail;  /* mt_err set; line released by helper */
        }
        b->lines[b->n++] = sh;
        start = end;
    }
    free(chars);
    CFRelease(ts);
    mt_err = MT_OK;
    return b;
fail:
    free(chars);
    CFRelease(ts);
    mt_block_free(b);
    return NULL;
}

int mt_block_lines(const mt_block *b) { return b ? b->n : 0; }

mt_shaped *mt_block_line(const mt_block *b, int i)
{
    if (!b || i < 0 || i >= b->n) {
        return NULL;
    }
    return b->lines[i];
}

void mt_block_free(mt_block *b)
{
    if (!b) {
        return;
    }
    for (int i = 0; i < b->n; i++) {
        mt_shaped_free(b->lines[i]);
    }
    free(b->lines);
    free(b);
}

void mt_free(void *bitmap) { free(bitmap); }

#else
#error "microtext: only the macOS (CoreText) backend exists so far"
#endif

#endif /* MICROTEXT_IMPLEMENTATION_ONCE */
#endif /* MICROTEXT_IMPLEMENTATION */
