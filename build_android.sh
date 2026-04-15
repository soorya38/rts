#!/bin/bash
set -e

echo "======================================"
echo "    RTS Android Build Automator       "
echo "======================================"

# 1. Ensure basic tools
if ! command -v brew &> /dev/null; then
    if [ -x "/opt/homebrew/bin/brew" ]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [ -x "/usr/local/bin/brew" ]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
fi

echo "[1/4] Checking minimum dependencies..."
brew install --quiet android-commandlinetools openjdk@17 curl wget

# Set Android variables based on standard Homebrew paths
export ANDROID_HOME="/opt/homebrew/share/android-commandlinetools"
export ANDROID_SDK_ROOT=$ANDROID_HOME
export JAVA_HOME="/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home"
export PATH="$ANDROID_HOME/cmdline-tools/latest/bin:$ANDROID_HOME/platform-tools:$PATH"

if [ ! -d "$ANDROID_HOME/cmdline-tools/latest/bin" ]; then
    echo "[!] Failed to locate sdkmanager from brew. Please ensure android-commandlinetools is installed."
    exit 1
fi

echo "[2/4] Downloading Android SDK & NDK..."
yes | sdkmanager --licenses > /dev/null 2>&1
sdkmanager --install "ndk;25.2.9519653" "build-tools;33.0.2" "platforms;android-33" "cmake;3.22.1" | grep -v "Downloading"

echo "[3/4] Verifying Android Project Structure..."
if [ ! -d "android/app" ]; then
    echo "[!] Error: 'android' directory not found or incomplete."
    echo "Please ensure the Android project files (build.gradle, etc.) are present."
    exit 1
fi

# Ensure gradlew is executable
if [ -f "android/gradlew" ]; then
    chmod +x android/gradlew
else
    echo "[!] Error: android/gradlew not found. Generating wrapper..."
    cd android
    # Use any available gradle to bootstrap the wrapper if needed, 
    # but normally it should be checked in.
    if [ -d "/tmp/gradle-7.4.2" ]; then
        /tmp/gradle-7.4.2/bin/gradle wrapper
    fi
    cd ..
fi

echo "[4/4] Building APK..."
cd android
./gradlew assembleDebug

echo "======================================"
echo " ✅ SUCCESS! Generated APK is at:      "
echo " $(pwd)/app/build/outputs/apk/debug/app-debug.apk"
echo "======================================"
