#include "../include/GameEngine.hpp"
#include <iostream>
#include <stdexcept>
#include <SDL2/SDL.h>

int main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  std::cout << "=== Greedy Tangle ===" << std::endl;
  std::cout << "A Competitive Graph Theory Puzzle" << std::endl;
  std::cout << "-----------------------------------" << std::endl;
  std::cout << "Controls:" << std::endl;
  std::cout << "  - Left Click + Drag: Move nodes" << std::endl;
  std::cout << "  - ESC: Quit" << std::endl;
  std::cout << "-----------------------------------" << std::endl;
  std::cout << "Goal: Untangle the graph (make all edges green)" << std::endl;
  std::cout << std::endl;

  try {
    GreedyTangle::GameEngine engine;

    // Initialize SDL and create window
    engine.Init();

    // Initial graph generation is now handled by the Home Screen

    // Run main game loop
    engine.Run();

    // Report final state
    std::cout << "\n[Result] Final intersection count: "
              << engine.GetIntersectionCount() << std::endl;

    if (engine.GetIntersectionCount() == 0) {
      std::cout << "[Victory!] Graph successfully untangled!" << std::endl;
    }

  } catch (const std::exception &e) {
    std::cerr << "[FATAL ERROR] " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
