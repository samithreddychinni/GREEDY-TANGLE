# Greedy Tangle

A Competitive Graph Theory Puzzle game.

## Prerequisites

This project requires C++20 and SDL2 (including SDL2_ttf).

## Setup & Installation

I have provided a setup script to automatically install dependencies for Linux, macOS, and Windows (MinGW/MSYS2).

### 1. Run the Setup Script
Run the following command in your terminal:

```bash
chmod +x setup_dependencies.sh
./setup_dependencies.sh
```

This script will:
- Detect your OS.
- Install `cmake`, `sdl2`, `sdl2_ttf`, and `pkg-config`.
- Offer to build the project for you.

### 2. Manual Build (Optional)
If you prefer to build manually:

```bash
mkdir build
cd build
cmake ..
make
```

### 3. Run the Game
```bash
./build/GreedyTangle
```

## Cross-Platform Notes
- **Windows**: The code is compatible with MinGW/MSYS2 environments. The setup script supports `pacman`.
- **macOS**: Requires Homebrew.
- **Linux**: Supports Debian/Ubuntu, Fedora, and Arch-based distros.
