# Greedy Tangle

A Competitive Graph Theory Puzzle game where you race against a CPU solver to untangle a graph.

## Game Modes

Choose your opponent's strategy from the **Mode** menu:

1. **Greedy Solver**: Local optimization. Moves nodes to positions that immediately maximize intersection reduction. Fast but can get stuck in local minima.
2. **D&C + DP Hybrid**: Divide & Conquer with Dynamic Programming. Spatially partitions the graph, uses DP to solve sub-regions optimally, and refines boundaries. Slower but more robust against complex tangles.

## How to Play

1. **Goal**: Untangle the graph so no edges cross (all edges turn green).
2. **Race**: The CPU starts solving immediately on a copy of the graph. Be faster than the algorithm!
3. **Controls**:
   - **Left Click + Drag**: Move nodes.
   - **ESC**: Quit.
   - **Menu Bar**: Change difficulty, node count, or solver mode.

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
