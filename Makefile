RAYLIB := $(shell brew --prefix raylib)
CC      := clang
CFLAGS  := -std=c99 -Wall -Wextra -pedantic -O2
SANFLAGS := -std=c99 -Wall -Wextra -pedantic -g -O1 \
            -fsanitize=address,undefined -fno-sanitize-recover=all \
            -fno-omit-frame-pointer

MAC_FW   := -framework CoreText -framework CoreGraphics -framework CoreFoundation
RL_FLAGS := -I$(RAYLIB)/include -L$(RAYLIB)/lib -lraylib
RL_FW    := -framework Cocoa -framework IOKit -framework CoreVideo \
            -framework OpenGL

.PHONY: all test sanitize leaks golden demo_1_showcase demo_2_raylib clean
all: test examples/demo_1_showcase examples/demo_2_raylib

test: tests/test_dump
	./tests/test_dump
tests/test_dump: tests/test_dump.c microtext.h tests/3rd/stb_image_write.h
	$(CC) $(CFLAGS) $< -o $@ $(MAC_FW)

sanitize: tests/test_dump.c microtext.h tests/3rd/stb_image_write.h
	@mkdir -p output
	$(CC) $(SANFLAGS) $< -o tests/test_dump_san $(MAC_FW)
	./tests/test_dump_san

# Ad-hoc codesign with get-task-allow so `leaks` can scan the data segment for
# roots; without it the rooted sRGB color-space singleton is a false positive.
leaks: tests/test_dump
	@mkdir -p output
	codesign -s - -f --entitlements tests/leaks.entitlements tests/test_dump
	leaks --atExit -- ./tests/test_dump

# Compare rendered output to committed reference PNGs (tied to the macOS/font
# version that produced them). `MICROTEXT_UPDATE_GOLDEN=1 make golden` regenerates.
golden: tests/golden.c microtext.h tests/3rd/stb_image_write.h tests/3rd/stb_image.h
	$(CC) $(CFLAGS) $< -o tests/golden_test $(MAC_FW)
	./tests/golden_test

demo_1_showcase: examples/demo_1_showcase
	@mkdir -p output
	./examples/demo_1_showcase
examples/demo_1_showcase: examples/demo_1_showcase.c microtext.h tests/3rd/stb_image_write.h
	$(CC) $(CFLAGS) $< -o $@ $(MAC_FW)

demo_2_raylib: examples/demo_2_raylib
	./examples/demo_2_raylib
examples/demo_2_raylib: examples/demo_2_raylib.c examples/mt_raylib.h microtext.h
	$(CC) $(CFLAGS) $< -o $@ $(RL_FLAGS) $(MAC_FW) $(RL_FW)

clean:
	rm -rf tests/test_dump tests/test_dump_san tests/golden_test \
	       examples/demo_1_showcase examples/demo_2_raylib output *.dSYM tests/*.dSYM
