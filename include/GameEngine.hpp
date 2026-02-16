#pragma once

#include "CPUController.hpp"
#include "ICPUSolver.hpp"
#include "SolverFactory.hpp"
#include "GraphData.hpp"
#include "MenuBar.hpp"
#ifdef _WIN32
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include <chrono>
#include <future>
#include <memory>
#include <vector>

namespace GreedyTangle {

/**
 * GameEngine - Core SDL lifecycle and render loop
 *
 * Render Loop Lifecycle:
 * 1. Input Poll: Handle SDL_Quit and Mouse events
 * 2. Update: Verify graph state and recalculate intersections
 * 3. Render: Clear → Draw Edges → Draw Nodes → Swap Buffers
 */
class GameEngine {
public:
  // Window configuration
  static constexpr int WINDOW_WIDTH = 1024;
  static constexpr int WINDOW_HEIGHT = 768;
  static constexpr const char *WINDOW_TITLE = "Greedy Tangle";

  // Color palette (minimalist dark theme)
  struct Colors {
    static constexpr SDL_Color BACKGROUND = {20, 20, 25, 255};
    static constexpr SDL_Color NODE_FILL = {200, 200, 210, 255};
    static constexpr SDL_Color NODE_BORDER = {255, 255, 255, 255};
    static constexpr SDL_Color EDGE_SAFE = {50, 205, 50, 255};     // Green
    static constexpr SDL_Color EDGE_CRITICAL = {220, 50, 50, 255}; // Red
    static constexpr SDL_Color NODE_DRAGGING = {100, 180, 255, 255};
  };

  // Game phases for animated graph initialization
  enum class GamePhase {
    SHOWING_UNTANGLED, // Display clean planar layout
    TANGLING,          // Animate nodes to tangled positions
    PLAYING,           // Human plays, CPU solves in background (RACE MODE)
    VICTORY_BLINK,     // Flash animation on win
    VICTORY            // Show analytics screen
  };

  // Difficulty levels
  enum class Difficulty {
    EASY,   // Few extra edges
    MEDIUM, // Moderate edges
    HARD    // Many edges
  };

  enum class GameMode {
    GREEDY,
    DIVIDE_AND_CONQUER_DP
  };

private:
  // SDL handles
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;

  // Game state
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  bool isRunning = false;

  // Game phase state machine
  GamePhase currentPhase = GamePhase::SHOWING_UNTANGLED;
  std::chrono::steady_clock::time_point phaseStartTime;
  static constexpr float UNTANGLED_DISPLAY_DURATION = 1.5f; // seconds
  static constexpr float TANGLE_ANIMATION_DURATION = 1.0f;  // seconds

  // Animation state
  std::vector<Vec2> startPositions;  // Positions at animation start
  std::vector<Vec2> targetPositions; // Random tangled positions
  float animationProgress = 0.0f;

  // Interaction state (Input State Machine)
  int selectedNodeID = -1; // ID of node being dragged (-1 = Idle state)
  int hoveredNodeID = -1;  // ID of node under cursor (-1 = none)
  Vec2 mousePosition;      // Current mouse coordinates (float)

  // Statistics
  int intersectionCount = 0;

  // Game analytics
  int moveCount = 0; // Number of node drags
  std::chrono::steady_clock::time_point gameStartTime;
  float gameDuration = 0.0f; // Time to solve in seconds

  // Victory animation state
  int blinkCount = 0;
  std::chrono::steady_clock::time_point victoryStartTime;
  static constexpr float BLINK_DURATION = 0.15f; // Time per blink
  static constexpr int TOTAL_BLINKS = 4;         // Number of blinks

  // Menu bar
  std::unique_ptr<MenuBar> menuBar;

  // Game settings
  int currentNodeCount = 10; // Default changed to 10
  Difficulty currentDifficulty = Difficulty::MEDIUM;
  GameMode currentMode = GameMode::GREEDY;

  // Custom node count input dialog
  bool showInputDialog = false;
  std::string inputBuffer;
  std::chrono::steady_clock::time_point inputCursorBlink;

  std::unique_ptr<ICPUSolver> currentSolver_;
  std::unique_ptr<ReplayLogger> cpuReplayLogger_;
  std::future<CPUMove> cpuFuture_;
  CPUMove currentCPUMove_;
  int cpuMoveCount_ = 0;

  // Race Mode: CPU has its own copy of the graph
  std::vector<Node> cpuNodes_; // CPU's graph state
  int cpuIntersectionCount_ =
      0;                     // CPU's current intersections (live scoreboard)
  bool cpuSolving_ = false;  // Is CPU currently thinking?
  bool cpuFinished_ = false; // Has CPU solved?
  std::string winner_ = "";  // "human" or "cpu"

  // CPU delay based on difficulty (makes CPU beatable on easier levels)
  std::chrono::steady_clock::time_point cpuLastMoveTime_;
  static constexpr float CPU_DELAY_EASY = 3.0f;   // 3 seconds between moves
  static constexpr float CPU_DELAY_MEDIUM = 1.5f; // 1.5 seconds
  static constexpr float CPU_DELAY_HARD = 0.0f;   // No delay

  // Auto-solve mode (forfeit + visualization)
  bool autoSolveActive_ = false;
  bool autoSolveAnimating_ = false;
  float autoSolveAnimProgress_ = 0.0f;
  CPUMove autoSolveCurrentMove_;
  static constexpr float AUTO_SOLVE_ANIM_DURATION = 0.3f; // Animation per move

public:
  GameEngine() = default;
  ~GameEngine();

  // Prevent copying
  GameEngine(const GameEngine &) = delete;
  GameEngine &operator=(const GameEngine &) = delete;

  /**
   * Initialize SDL video subsystem and create window/renderer
   * @throws std::runtime_error on SDL initialization failure
   */
  void Init();

  /**
   * Main game loop entry point
   */
  void Run();

  /**
   * Clean shutdown of SDL resources
   */
  void Cleanup();

  // Graph manipulation
  void AddNode(const Vec2 &position);
  void AddEdge(int u_id, int v_id);
  void GenerateTestGraph(); // For initial testing

  /**
   * Generate a random tangled graph
   * @param nodeCount Number of nodes to create
   * Creates Hamiltonian path + random edges for complexity
   */
  void GenerateRandomGraph(int nodeCount);

  /**
   * Generate a dynamic graph that starts untangled, then tangles
   * @param nodeCount Number of nodes to create
   * Dispatches to Easy/Medium/Hard based on currentDifficulty
   */
  void GenerateDynamicGraph(int nodeCount);

  /**
   * Easy: Cycle + Chords (low rigidity, floppy)
   * Creates ring with 2-3 non-crossing chords
   */
  void GenerateEasyGraph(int nodeCount);

  /**
   * Medium: Grid Mesh with holes (medium rigidity)
   * Grid with 20-30% edges removed
   */
  void GenerateMediumGraph(int nodeCount);

  /**
   * Hard: Triangulation (high rigidity, maximal planar)
   * Incrementally builds by splitting triangle faces
   */
  void GenerateHardGraph(int nodeCount);

  /**
   * Tangle method: place all nodes on circle in random order
   */
  void ApplyCircleScramble();

  // Clear current graph
  void ClearGraph();

  // Accessors
  bool IsRunning() const { return isRunning; }
  int GetIntersectionCount() const { return intersectionCount; }
  const std::vector<Node> &GetNodes() const { return nodes; }
  const std::vector<Edge> &GetEdges() const { return edges; }

  // Menu actions
  void StartNewGame();
  void SetNodeCount(int count);
  void SetDifficulty(Difficulty diff);
  void SetGameMode(GameMode mode);

private:
  /**
   * Process SDL_Event queue
   * Handles: SDL_QUIT, Mouse button down/up, Mouse motion
   */
  void HandleInput();

  /**
   * Update game state
   * - Recalculate all edge intersections
   * - Update isIntersecting flags
   */
  void Update();

  /**
   * Render current frame
   * Order: Clear → Edges → Nodes → Present
   */
  void Render();

  // Rendering helpers
  void DrawFilledCircle(int cx, int cy, int radius);
  void DrawNode(const Node &node); // Uses node.isDragging/isHovered for state
  void DrawEdge(const Edge &edge);

  // Interaction helpers
  int GetNodeAtPosition(const Vec2 &pos);
  void UpdateHoverState(); // Update isHovered flags based on mouse position

  // Phase management
  void UpdatePhase();            // Manage phase transitions and animations
  void GeneratePlanarLayout();   // Arrange nodes in untangled circle
  void GenerateTangledTargets(); // Generate random target positions
  float EaseOutCubic(float t);   // Smooth animation easing

  // Menu setup
  void SetupMenus(); // Configure menu bar with game options

  // Victory handling
  void CheckVictory();        // Check if game is won
  void RenderVictoryScreen(); // Draw analytics overlay

  // Custom input dialog
  void ShowCustomNodeDialog(); // Open custom node count input
  void RenderInputDialog();    // Render the input dialog

  // Race Mode helpers
  void StartCPURace();       // Initialize CPU copy and start solving
  void UpdateCPURace();      // Check if CPU made progress, update counts
  void RenderScoreboard();   // Draw "H: X | CPU: Y" live scoreboard
  void StartNextCPUMove();   // Dispatch next CPU move computation
  float GetCPUDelay() const; // Get delay based on difficulty

  // Auto-solve feature
  void StartAutoSolve();  // Forfeit and show CPU solving human's graph
  void UpdateAutoSolve(); // Animate auto-solve moves
};

} // namespace GreedyTangle
