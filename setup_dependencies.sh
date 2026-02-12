#!/bin/bash

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     machine=Linux;;
    Darwin*)    machine=Mac;;
    CYGWIN*)    machine=Cygwin;;
    MINGW*)     machine=MinGW;;
    MSYS*)      machine=MSYS;;
    *)          machine="UNKNOWN"
esac

echo "Detected OS: ${machine}"

install_deps_linux() {
    echo "Installing libraries for Linux..."
    # Check for distro ID
    if grep -q "ID=ubuntu" /etc/os-release || grep -q "ID=debian" /etc/os-release || grep -q "ID=pop" /etc/os-release || grep -q "ID=linuxmint" /etc/os-release; then
        echo "Detected Debian-based distro."
        sudo apt-get update
        sudo apt-get install -y build-essential cmake libsdl2-dev libsdl2-ttf-dev pkg-config
    elif grep -q "ID=fedora" /etc/os-release; then
        echo "Detected Fedora."
        sudo dnf install -y gcc-c++ cmake SDL2-devel SDL2_ttf-devel pkgconf-pkg-config
    elif grep -q "ID=arch" /etc/os-release || grep -q "ID=manjaro" /etc/os-release; then
        echo "Detected Arch-based distro."
        sudo pacman -S --noconfirm base-devel cmake sdl2 sdl2_ttf pkg-config
    else
        echo "Unsupported Linux distro (from /etc/os-release). Please install SDL2, SDL2_ttf, cmake, and pkg-config manually."
        exit 1
    fi
}

install_deps_mac() {
    echo "Installing libraries for macOS..."
    if ! command -v brew &> /dev/null; then
        echo "Error: Homebrew not found. Please install Homebrew first: https://brew.sh/"
        exit 1
    fi
    brew install cmake sdl2 sdl2_ttf pkg-config
}

install_deps_windows_msys() {
    echo "Installing libraries for Windows (MinGW/MSYS2)..."
    pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_ttf make pkg-config
}

if [ "$machine" == "Linux" ]; then
    install_deps_linux
elif [ "$machine" == "Mac" ]; then
    install_deps_mac
elif [[ "$machine" == "MinGW" || "$machine" == "MSYS" || "$machine" == "Cygwin" ]]; then
    install_deps_windows_msys
else
    echo "Unsupported OS: $machine"
    echo "Please install dependencies manually."
    exit 1
fi

echo "Dependencies installed successfully!"

# Ask user if they want to build the project
read -p "Do you want to build the project now? [y/N] " response
if [[ "$response" =~ ^([yY][eE][sS]|[yY])$ ]]
then
    if [ ! -d "build" ]; then
        mkdir build
    fi
    cd build
    cmake ..
    if [[ "$machine" == "MinGW" || "$machine" == "MSYS" ]]; then
        mingw32-make
    else
        make
    fi
    echo "Build complete. Run ./GreedyTangle to start."
else
    echo "Build skipped. Run cmake manually."
fi
