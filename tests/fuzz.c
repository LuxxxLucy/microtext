/* Fuzz the shaping and hit-test walkers with arbitrary bytes, asserting no
 * crash or undefined behaviour under ASan/UBSan.
 *
 * `make fuzz` builds a standalone driver that replays built-in adversarial
 * seeds plus any corpus files passed on the command line; it runs anywhere.
 * For coverage-guided fuzzing where libFuzzer is available, build with
 *   clang -fsanitize=fuzzer,address,undefined -DMICROTEXT_LIBFUZZER
 * tests/fuzz.c and run `./a.out -runs=N` or point it at a corpus directory. */
#define MICROTEXT_IMPLEMENTATION
#include "../microtext.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static mt_font *f;
    if (!f) {
        f = mt_font_open("Helvetica Neue", 16.0f);
    }
    if (!f) {
        return 0;
    }
    mt_color ink = { 0, 0, 0, 255 };
    mt_shaped *s = mt_shape(f, (const char *)data, (ptrdiff_t)size, ink);
    if (s) {
        ptrdiff_t n = (ptrdiff_t)size;
        for (ptrdiff_t i = -2; i <= n + 2; i++) { // in- and out-of-range
            float x = mt_shaped_caret_x(s, i);
            mt_shaped_byte_at_x(s, x);
        }
        float spans[16];
        mt_shaped_selection(s, 0, n, spans, 8);
        mt_shaped_selection(s, n / 2, n, spans, 8);
        mt_shaped_free(s);
    }
    return 0;
}

#ifndef MICROTEXT_LIBFUZZER // libFuzzer supplies its own main
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    static const char *const seeds[] = {
        "",
        "A",
        "Aé😀B",
        "café\n",
        "中文 mixed العربية ‮rtl",
        "\xff\xfe",
        "\xED\xA0\x80",
        "\xC3",
        "\xF0\x9F",
    };
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        LLVMFuzzerTestOneInput((const uint8_t *)seeds[i], strlen(seeds[i]));
    }
    for (int a = 1; a < argc; a++) {
        FILE *fp = fopen(argv[a], "rb");
        if (!fp) {
            continue;
        }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        unsigned char *b = (unsigned char *)malloc(sz > 0 ? (size_t)sz : 1);
        if (b && fread(b, 1, (size_t)sz, fp) == (size_t)sz) {
            LLVMFuzzerTestOneInput(b, (size_t)sz);
        }
        free(b);
        fclose(fp);
    }
    printf("fuzz replay ok (%zu seeds)\n", sizeof(seeds) / sizeof(seeds[0]));
    return 0;
}
#endif
