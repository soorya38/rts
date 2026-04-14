CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -g
TARGET  = rts

# Detect Homebrew prefix (Apple Silicon first, then Intel)
BREW_PREFIX := $(shell /opt/homebrew/bin/brew --prefix 2>/dev/null || /usr/local/bin/brew --prefix 2>/dev/null)
RAYLIB_PREFIX := $(shell $(BREW_PREFIX)/bin/brew --prefix raylib 2>/dev/null)

ifeq ($(RAYLIB_PREFIX),)
$(error Raylib not found. Run: brew install raylib)
endif

ENET_PREFIX := $(shell $(BREW_PREFIX)/bin/brew --prefix enet 2>/dev/null)
ifeq ($(ENET_PREFIX),)
$(error ENet not found. Run: brew install enet)
endif

# Recursively find all .c files in src/
SRCS    := $(shell find src -name '*.c')
OBJS    := $(patsubst src/%.c, build/%.o, $(SRCS))

INCLUDES = -I$(RAYLIB_PREFIX)/include \
           -Isrc \
           -Isrc/core \
           -Isrc/core/unit \
           -Isrc/ui \
           -Isrc/ui/renderer \
           -Isrc/ui/hud \
           -Isrc/ui/input \
           -Isrc/ai \
           -I$(ENET_PREFIX)/include

LIBS     = -L$(RAYLIB_PREFIX)/lib -lraylib \
           -L$(ENET_PREFIX)/lib -lenet \
           -framework OpenGL -framework Cocoa -framework IOKit \
           -framework CoreFoundation -framework CoreVideo \
           -framework CoreAudio -framework AudioToolbox

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build $(TARGET)

install-deps:
	brew install raylib

.PHONY: all run clean install-deps
