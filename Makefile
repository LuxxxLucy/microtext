RAYLIB := $(shell brew --prefix raylib)
CC     := clang
CFLAGS := -std=c99 -Wall -Wextra -pedantic -O2

MAC_FW   := -framework CoreText -framework CoreGraphics -framework CoreFoundation
RL_FLAGS := -I$(RAYLIB)/include -L$(RAYLIB)/lib -lraylib
RL_FW    := -framework Cocoa -framework IOKit -framework CoreVideo \
            -framework OpenGL

.PHONY: all test demo_1_showcase demo_2_raylib clean
all: test examples/demo_1_showcase examples/demo_2_raylib

test: tests/test_dump
	./tests/test_dump
tests/test_dump: tests/test_dump.c microtext.h tests/3rd/stb_image_write.h
	$(CC) $(CFLAGS) $< -o $@ $(MAC_FW)

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
	rm -rf tests/test_dump examples/demo_1_showcase examples/demo_2_raylib output
