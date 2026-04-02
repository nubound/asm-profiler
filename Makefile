CC := gcc
PKG_CONFIG := pkg-config

CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror \
	-g3 -O2 -fno-omit-frame-pointer
CPPFLAGS := -Iinclude
LDLIBS := $(shell $(PKG_CONFIG) --libs ncursesw libelf)
LDLIBS += -lm
SRC := \
	src/main.c \
	src/args.c \
	src/util.c \
	src/vector.c \
	src/proc.c \
	src/perf_sampler.c \
	src/symbols.c \
	src/ui.c
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all clean test

all: build/asm-profiler

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags ncursesw libelf) -c $< -o $@

build/asm-profiler: $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

test: build/asm-profiler
	bash ./tests/smoke.sh

clean:
	rm -rf build
