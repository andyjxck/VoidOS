CC ?= clang
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)
TTF_PREFIX := $(shell brew --prefix sdl2_ttf)
TTF_CFLAGS := -I$(TTF_PREFIX)/include
TTF_LIBS := -L$(TTF_PREFIX)/lib -lSDL2_ttf
IMG_PREFIX := $(shell brew --prefix sdl2_image)
IMG_CFLAGS := -I$(IMG_PREFIX)/include
IMG_LIBS := -L$(IMG_PREFIX)/lib -lSDL2_image
CFLAGS := -std=c11 -Wall -Wextra -Werror -Iinclude
CORE_SRC := core/app.c core/font.c

all: build/voidos-sim

build/voidos-sim: $(CORE_SRC) simulator/main.c simulator/sdl_hal.c
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(TTF_CFLAGS) $(IMG_CFLAGS) $^ -o $@ $(SDL_LIBS) $(TTF_LIBS) $(IMG_LIBS)

build/test_app: $(CORE_SRC) tests/test_app.c
	mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@

build/test_screenshot: simulator/sdl_hal.c tests/test_screenshot.c
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(TTF_CFLAGS) $(IMG_CFLAGS) $^ -o $@ $(SDL_LIBS) $(TTF_LIBS) $(IMG_LIBS)

build/test_input: $(CORE_SRC) simulator/sdl_hal.c tests/test_input.c
	mkdir -p build
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(TTF_CFLAGS) $(IMG_CFLAGS) $^ -o $@ $(SDL_LIBS) $(TTF_LIBS) $(IMG_LIBS)

test: build/test_app build/test_screenshot build/test_input
	./build/test_app
	SDL_VIDEODRIVER=dummy ./build/test_screenshot
	SDL_VIDEODRIVER=dummy ./build/test_input

clean:
	rm -f build/voidos-sim build/test_app build/test_screenshot build/test_input
	-rmdir build 2>/dev/null || true

.PHONY: all test clean
