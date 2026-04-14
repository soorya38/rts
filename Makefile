CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -g
TARGET  = rts
SRC_DIR = src

SRCS    = $(wildcard $(SRC_DIR)/*.c)
OBJS    = $(patsubst $(SRC_DIR)/%.c, $(SRC_DIR)/%.o, $(SRCS))

# Detect Homebrew prefix (Apple Silicon first, then Intel)
BREW_PREFIX := $(shell /opt/homebrew/bin/brew --prefix 2>/dev/null || /usr/local/bin/brew --prefix 2>/dev/null)
RAYLIB_PREFIX := $(shell $(BREW_PREFIX)/bin/brew --prefix raylib 2>/dev/null)

ifeq ($(RAYLIB_PREFIX),)
$(error Raylib not found. Run: brew install raylib)
endif

INCLUDES = -I$(RAYLIB_PREFIX)/include -I$(SRC_DIR)
LIBS     = -L$(RAYLIB_PREFIX)/lib -lraylib \
           -framework OpenGL -framework Cocoa -framework IOKit \
           -framework CoreFoundation -framework CoreVideo \
           -framework CoreAudio -framework AudioToolbox

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(SRC_DIR)/*.o $(TARGET)

install-deps:
	brew install raylib

.PHONY: all run clean install-deps
