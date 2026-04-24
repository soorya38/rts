CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -g
DEFINES = -DRTS_HAS_CURL=1
TARGET  = rts

# Detect Homebrew prefix (Apple Silicon first, then Intel)
BREW_PREFIX := $(shell /opt/homebrew/bin/brew --prefix 2>/dev/null || /usr/local/bin/brew --prefix 2>/dev/null)
RAYLIB_PREFIX := $(shell $(BREW_PREFIX)/bin/brew --prefix raylib 2>/dev/null)
CURL_PREFIX   := $(shell $(BREW_PREFIX)/bin/brew --prefix curl 2>/dev/null)

ifeq ($(RAYLIB_PREFIX),)
$(error Raylib not found. Run: brew install raylib)
endif

# Recursively find all .c files in src/
SRCS    := $(shell find src -name '*.c')
OBJS    := $(patsubst src/%.c, build/%.o, $(SRCS))
DEPS    := $(OBJS:.o=.d)

INCLUDES = -I$(RAYLIB_PREFIX)/include \
           -I$(CURL_PREFIX)/include \
           -Isrc \
           -Isrc/core \
           -Isrc/core/unit \
           -Isrc/ui \
           -Isrc/ui/renderer \
           -Isrc/ui/hud \
           -Isrc/ui/input \
           -Isrc/ai \
           -Isrc/core/enet

LIBS     = -L$(RAYLIB_PREFIX)/lib -lraylib \
           -L$(CURL_PREFIX)/lib -lcurl \
           -framework OpenGL -framework Cocoa -framework IOKit \
           -framework CoreFoundation -framework CoreVideo \
           -framework CoreAudio -framework AudioToolbox

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(DEFINES) -o $@ $^ $(LIBS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEFINES) -MMD -MP $(INCLUDES) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build $(TARGET)

install-deps:
	brew install raylib enet

android:
	./build_android.sh

android-clean:
	cd android && ./gradlew clean
	./build_android.sh

android-install:
	export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools && \
	$$ANDROID_HOME/platform-tools/adb install -r android/app/build/outputs/apk/debug/app-debug.apk

# Stream logcat filtered to RTS trace messages + crash signals.
# Press Ctrl-C to stop. Requires USB debugging enabled on the device.
android-logs:
	export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools && \
	$$ANDROID_HOME/platform-tools/adb logcat -c && \
	$$ANDROID_HOME/platform-tools/adb logcat "raylib:I" "DEBUG:F" "libc:F" "*:S"

# Full cycle: clean NDK build → install → stream logs
android-deploy: android-clean android-install android-logs

.PHONY: all run clean install-deps android android-clean android-install android-logs android-deploy

-include $(DEPS)
