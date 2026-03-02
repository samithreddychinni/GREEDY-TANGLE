#include "GameEngine.hpp"
#include "MathUtils.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <functional>
#include <mutex>

namespace GreedyTangle {

class StreamRedirector : public std::streambuf {
public:
    StreamRedirector(std::ostream& stream, std::function<void(const std::string&)> callback)
        : stream_(stream), callback_(callback), old_buf_(stream.rdbuf()) {
        stream_.rdbuf(this);
    }
    ~StreamRedirector() {
        stream_.rdbuf(old_buf_);
    }
protected:
    virtual int_type overflow(int_type v) override {
        if (v == '\n') {
            callback_(buffer_);
            buffer_.clear();
        } else if (v != EOF) {
            buffer_ += static_cast<char>(v);
        }
        return old_buf_->sputc(v); // Also print to terminal
    }
private:
    std::ostream& stream_;
    std::function<void(const std::string&)> callback_;
    std::streambuf* old_buf_;
    std::string buffer_;
};

GameEngine::~GameEngine() { Cleanup(); }


void GameEngine::Init() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
  }

  window = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

  if (!window) {
    throw std::runtime_error(std::string("SDL_CreateWindow failed: ") +
                             SDL_GetError());
  }

  renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (!renderer) {
    throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") +
                             SDL_GetError());
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // Initialize menu bar
  menuBar = std::make_unique<MenuBar>();
  if (!menuBar->Init(renderer)) {
    std::cerr << "[Game] MenuBar init failed, continuing without menu"
              << std::endl;
    menuBar.reset();
  } else {
    SetupMenus();
  }

  isRunning = true;

  currentSolver_ = CreateSolver(static_cast<SolverMode>(currentMode));
  cpuReplayLogger_ = std::make_unique<ReplayLogger>();

  std::cout << "[Game] Initialized successfully" << std::endl;

  // Load UI Fonts
  std::vector<std::string> fontPaths = {
      "assets/fonts/Inter-Bold.ttf",
      "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf",
      "/usr/share/fonts/google-droid-sans-fonts/DroidSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "c:/windows/fonts/arialbd.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
      "/Library/Fonts/Arial.ttf"};

  for (const auto &path : fontPaths) {
    titleFont = TTF_OpenFont(path.c_str(), 64);
    if (titleFont) {
      uiFont = TTF_OpenFont(path.c_str(), 24);
      std::cout << "[Game] Loaded UI fonts from: " << path << std::endl;
      break;
    }
  }

  if (!titleFont) {
    std::cerr << "[Game] Failed to load UI fonts!" << std::endl;
  }
}

void GameEngine::Run() {
  while (isRunning) {
    UpdatePhase();
    HandleInput();
    UpdateCPURace();   // Check CPU progress in race mode
    UpdateAutoSolve(); // Animate auto-solve if active
    Update();
    Render();
  }
}

void GameEngine::Cleanup() {
  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
  }
  if (window) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }
  if (titleFont) {
    TTF_CloseFont(titleFont);
    titleFont = nullptr;
  }
  if (uiFont) {
    TTF_CloseFont(uiFont);
    uiFont = nullptr;
  }

  SDL_Quit();
  std::cout << "[Game] Cleanup complete" << std::endl;
}

void GameEngine::HandleInput() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    // Let menu handle events first
    if (menuBar && menuBar->HandleEvent(event)) {
      continue;
    }

    if (currentPhase == GamePhase::MAIN_MENU) {
      if (event.type == SDL_QUIT) {
        isRunning = false;
      }
      HandleHomeScreenInput(event);
      continue;
    }

    // Replay viewer has its own input handling
    if (currentPhase == GamePhase::REPLAY_VIEWER) {
      if (event.type == SDL_QUIT) {
        isRunning = false;
      }
      HandleReplayInput(event);
      continue;
    }

    // Benchmark results has its own input handling
    if (currentPhase == GamePhase::BENCHMARK_RESULTS) {
      if (event.type == SDL_QUIT) {
        isRunning = false;
      }
      HandleBenchmarkInput(event);
      continue;
    }

    // Scalability results has its own input handling
    if (currentPhase == GamePhase::SCALABILITY_RESULTS) {
      if (event.type == SDL_QUIT) {
        isRunning = false;
      }
      HandleScalabilityInput(event);
      continue;
    }

    // How It Works has its own input handling
    if (currentPhase == GamePhase::HOW_IT_WORKS) {
      if (event.type == SDL_QUIT) {
        isRunning = false;
      }
      HandleHowItWorksInput(event);
      continue;
    }

    switch (event.type) {
    case SDL_QUIT:
      isRunning = false;
      break;

    case SDL_MOUSEBUTTONDOWN:
      if (event.button.button == SDL_BUTTON_LEFT) {
        Vec2 clickPos(static_cast<float>(event.button.x),
                      static_cast<float>(event.button.y));

        // Check in-game buttons during PLAYING phase
        if (currentPhase == GamePhase::PLAYING) {
          int winW, winH;
          SDL_GetWindowSize(window, &winW, &winH);
          int scoreW = 450;
          int scoreX = (winW - scoreW) / 2;
          int scoreY = winH - 40 - 10;
          int btnY = scoreY - 35; // Buttons above scoreboard

          // End Game button (above scoreboard, left)
          SDL_Rect endGameBtn = {scoreX, btnY, 120, 30};
          SDL_Point clickPt = {event.button.x, event.button.y};
          if (SDL_PointInRect(&clickPt, &endGameBtn)) {
            EndGame();
            break;
          }

          // Pause/Continue button (above scoreboard, right)
          SDL_Rect pauseBtn = {scoreX + scoreW - 140, btnY, 140, 30};
          if (SDL_PointInRect(&clickPt, &pauseBtn)) {
            TogglePauseCPU();
            break;
          }
        }

        int nodeId = GetNodeAtPosition(clickPos);
        if (nodeId != -1) {
          selectedNodeID = nodeId;
          nodes[selectedNodeID].isDragging = true;
        }
      }
      break;

    case SDL_MOUSEBUTTONUP:
      if (event.button.button == SDL_BUTTON_LEFT) {
        if (selectedNodeID != -1 &&
            selectedNodeID < static_cast<int>(nodes.size())) {
          nodes[selectedNodeID].isDragging = false;
          // Count move only during gameplay
          if (currentPhase == GamePhase::PLAYING) {
            ++moveCount;
          }
        }
        selectedNodeID = -1;
      }
      break;

    case SDL_MOUSEMOTION:
      mousePosition = Vec2(static_cast<float>(event.motion.x),
                           static_cast<float>(event.motion.y));

      if (selectedNodeID != -1 &&
          selectedNodeID < static_cast<int>(nodes.size())) {
        nodes[selectedNodeID].position = mousePosition;
      } else {
        UpdateHoverState();
      }
      break;

    case SDL_KEYDOWN:
      // Handle input dialog first
      if (showInputDialog) {
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
          showInputDialog = false;
          inputBuffer.clear();
          break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          if (!inputBuffer.empty()) {
            int customCount = std::stoi(inputBuffer);
            showInputDialog = false;
            inputBuffer.clear();
            SetNodeCount(customCount); // Will clamp to 3-200
          }
          break;
        case SDLK_BACKSPACE:
          if (!inputBuffer.empty()) {
            inputBuffer.pop_back();
          }
          break;
        }
        break;
      }

      // Normal key handling when dialog is closed
      switch (event.key.keysym.sym) {
      case SDLK_ESCAPE:
        isRunning = false;
        break;
      case SDLK_r:
        ClearGraph();
        GenerateRandomGraph(8);
        break;
      case SDLK_t:
        ClearGraph();
        GenerateTestGraph();
        break;
      case SDLK_h:
        ToggleHeatmap();
        break;
      }
      break;

    case SDL_TEXTINPUT:
      // Only capture digits when dialog is open
      if (showInputDialog) {
        for (char c : std::string(event.text.text)) {
          if (std::isdigit(c) && inputBuffer.length() < 3) {
            inputBuffer += c;
          }
        }
      }
      break;
    }
  }
}

void GameEngine::UpdateHoverState() {
  int previousHovered = hoveredNodeID;
  hoveredNodeID = GetNodeAtPosition(mousePosition);

  if (previousHovered != hoveredNodeID) {
    if (previousHovered != -1 &&
        previousHovered < static_cast<int>(nodes.size())) {
      nodes[previousHovered].isHovered = false;
    }
    if (hoveredNodeID != -1) {
      nodes[hoveredNodeID].isHovered = true;
    }
  }
}

void GameEngine::Update() {
  if (currentPhase == GamePhase::COMPUTING_BENCHMARK || currentPhase == GamePhase::COMPUTING_SCALABILITY) {
    if (backgroundTask_.valid() && backgroundTask_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      if (logRedirector_) {
        delete static_cast<StreamRedirector*>(logRedirector_);
        logRedirector_ = nullptr;
      }
      if (currentPhase == GamePhase::COMPUTING_BENCHMARK) {
        currentPhase = GamePhase::BENCHMARK_RESULTS;
      } else {
        currentPhase = GamePhase::SCALABILITY_RESULTS;
      }
    }
    return;
  }

  for (Edge &edge : edges) {
    edge.isIntersecting = false;
  }

  size_t numEdges = edges.size();
  intersectionCount = 0;

  for (size_t i = 0; i < numEdges; ++i) {
    for (size_t j = i + 1; j < numEdges; ++j) {
      Edge &e1 = edges[i];
      Edge &e2 = edges[j];

      if (e1.sharesVertex(e2)) {
        continue;
      }

      const Vec2 &a = nodes[e1.u_id].position;
      const Vec2 &b = nodes[e1.v_id].position;
      const Vec2 &c = nodes[e2.u_id].position;
      const Vec2 &d = nodes[e2.v_id].position;

      if (CheckIntersection(a, b, c, d)) {
        e1.isIntersecting = true;
        e2.isIntersecting = true;
        ++intersectionCount;
      }
    }
  }

  // Check for victory condition
  CheckVictory();

  // Periodically recalculate heatmap if enabled
  if (heatmapEnabled_ && currentPhase == GamePhase::PLAYING &&
      intersectionCount > 0) {
    auto now = std::chrono::steady_clock::now();
    float elapsed =
        std::chrono::duration<float>(now - heatmapLastUpdate_).count();
    if (elapsed >= HEATMAP_UPDATE_INTERVAL ||
        nodeHeatmapScores_.size() != nodes.size()) {
      CalculateHeatmap();
      heatmapLastUpdate_ = now;
    }
  }
}

void GameEngine::Render() {
  // Handle blink effect during victory animation
  if (currentPhase == GamePhase::VICTORY_BLINK) {
    auto now = std::chrono::steady_clock::now();
    float blinkElapsed =
        std::chrono::duration<float>(now - victoryStartTime).count();
    int blinkPhase = static_cast<int>(blinkElapsed / BLINK_DURATION) % 2;

    if (blinkPhase == 0) {
      // Flash white
      SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    } else {
      // Normal background
      SDL_SetRenderDrawColor(renderer, Colors::BACKGROUND.r,
                             Colors::BACKGROUND.g, Colors::BACKGROUND.b,
                             Colors::BACKGROUND.a);
    }
  } else {
    SDL_SetRenderDrawColor(renderer, Colors::BACKGROUND.r, Colors::BACKGROUND.g,
                           Colors::BACKGROUND.b, Colors::BACKGROUND.a);
  }
  SDL_RenderClear(renderer);

  if (currentPhase == GamePhase::COMPUTING_BENCHMARK) {
    RenderComputingScreen("Running Benchmark Comparison...");
    return;
  }

  if (currentPhase == GamePhase::COMPUTING_SCALABILITY) {
    RenderComputingScreen("Running Empirical Complexity Analysis...");
    return;
  }

  if (currentPhase == GamePhase::MAIN_MENU) {
    RenderHomeScreen();
    SDL_RenderPresent(renderer);
    return;
  }

  if (currentPhase == GamePhase::REPLAY_VIEWER) {
    RenderReplayViewer();
    if (menuBar) {
      menuBar->Render();
    }
    SDL_RenderPresent(renderer);
    return;
  }

  if (currentPhase == GamePhase::BENCHMARK_RESULTS) {
    RenderBenchmarkResults();
    if (menuBar) {
      menuBar->Render();
    }
    SDL_RenderPresent(renderer);
    return;
  }

  if (currentPhase == GamePhase::SCALABILITY_RESULTS) {
    RenderScalabilityResults();
    if (menuBar) {
      menuBar->Render();
    }
    SDL_RenderPresent(renderer);
    return;
  }

  if (currentPhase == GamePhase::HOW_IT_WORKS) {
    RenderHowItWorks();
    if (menuBar) {
      menuBar->Render();
    }
    SDL_RenderPresent(renderer);
    return;
  }

  for (const Edge &edge : edges) {
    DrawEdge(edge);
  }

  for (const Node &node : nodes) {
    DrawNode(node);
  }

  // Render victory screen overlay
  if (currentPhase == GamePhase::VICTORY) {
    RenderVictoryScreen();
  }

  // Render game ended overlay
  if (currentPhase == GamePhase::GAME_ENDED) {
    RenderGameEndedScreen();
  }

  // Render menu bar on top
  if (menuBar) {
    menuBar->Render();
  }

  // Render input dialog on top of everything
  RenderInputDialog();

  // Render live scoreboard during gameplay
  RenderScoreboard();

  // Render algorithm description panel during gameplay
  RenderAlgorithmPanel();

  // Render heatmap legend if active
  RenderHeatmapLegend();

  SDL_RenderPresent(renderer);
}

void GameEngine::DrawFilledCircle(int cx, int cy, int radius) {
  for (int y = -radius; y <= radius; ++y) {
    int halfWidth = static_cast<int>(std::sqrt(radius * radius - y * y));
    SDL_RenderDrawLine(renderer, cx - halfWidth, cy + y, cx + halfWidth,
                       cy + y);
  }
}

void GameEngine::DrawNode(const Node &node) {
  int cx = static_cast<int>(node.position.x);
  int cy = static_cast<int>(node.position.y);
  int r = static_cast<int>(node.radius);

  SDL_Color fillColor;
  if (node.isDragging) {
    fillColor = Colors::NODE_DRAGGING;
  } else if (node.isHovered) {
    fillColor = {150, 220, 255, 255};
  } else if (heatmapEnabled_ && currentPhase == GamePhase::PLAYING &&
             node.id >= 0 &&
             node.id < static_cast<int>(nodeHeatmapScores_.size())) {
    fillColor = GetHeatmapColor(nodeHeatmapScores_[node.id]);
  } else {
    fillColor = Colors::NODE_FILL;
  }

  SDL_SetRenderDrawColor(renderer, fillColor.r, fillColor.g, fillColor.b,
                         fillColor.a);
  DrawFilledCircle(cx, cy, r);

  SDL_Color borderColor = (node.isDragging || node.isHovered)
                              ? SDL_Color{255, 255, 255, 255}
                              : Colors::NODE_BORDER;

  SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b,
                         borderColor.a);

  int x = r, y = 0;
  int radiusError = 1 - x;
  while (x >= y) {
    SDL_RenderDrawPoint(renderer, cx + x, cy + y);
    SDL_RenderDrawPoint(renderer, cx + y, cy + x);
    SDL_RenderDrawPoint(renderer, cx - y, cy + x);
    SDL_RenderDrawPoint(renderer, cx - x, cy + y);
    SDL_RenderDrawPoint(renderer, cx - x, cy - y);
    SDL_RenderDrawPoint(renderer, cx - y, cy - x);
    SDL_RenderDrawPoint(renderer, cx + y, cy - x);
    SDL_RenderDrawPoint(renderer, cx + x, cy - y);

    ++y;
    if (radiusError < 0) {
      radiusError += 2 * y + 1;
    } else {
      --x;
      radiusError += 2 * (y - x + 1);
    }
  }
}

void GameEngine::DrawEdge(const Edge &edge) {
  const Vec2 &p1 = nodes[edge.u_id].position;
  const Vec2 &p2 = nodes[edge.v_id].position;

  SDL_Color color =
      edge.isIntersecting ? Colors::EDGE_CRITICAL : Colors::EDGE_SAFE;

  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderDrawLine(renderer, static_cast<int>(p1.x), static_cast<int>(p1.y),
                     static_cast<int>(p2.x), static_cast<int>(p2.y));
}

int GameEngine::GetNodeAtPosition(const Vec2 &pos) {
  for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
    if (nodes[i].containsPoint(pos)) {
      return nodes[i].id;
    }
  }
  return -1;
}

void GameEngine::AddNode(const Vec2 &position) {
  int newId = static_cast<int>(nodes.size());
  nodes.emplace_back(newId, position);
}

void GameEngine::AddEdge(int u_id, int v_id) {
  if (u_id >= 0 && u_id < static_cast<int>(nodes.size()) && v_id >= 0 &&
      v_id < static_cast<int>(nodes.size()) && u_id != v_id) {

    for (const Edge &e : edges) {
      if ((e.u_id == u_id && e.v_id == v_id) ||
          (e.u_id == v_id && e.v_id == u_id)) {
        return;
      }
    }

    edges.emplace_back(u_id, v_id);
    nodes[u_id].adjacencyList.push_back(v_id);
    nodes[v_id].adjacencyList.push_back(u_id);
  }
}

void GameEngine::ClearGraph() {
  selectedNodeID = -1;
  hoveredNodeID = -1;
  intersectionCount = 0;
  nodes.clear();
  edges.clear();
}

void GameEngine::GenerateRandomGraph(int nodeCount) {
  if (nodeCount < 3)
    nodeCount = 3;

  std::random_device rd;
  std::mt19937 gen(rd());

  const float margin = 60.0f;
  std::uniform_real_distribution<float> xDist(margin, WINDOW_WIDTH - margin);
  std::uniform_real_distribution<float> yDist(margin, WINDOW_HEIGHT - margin);

  for (int i = 0; i < nodeCount; ++i) {
    Vec2 pos(xDist(gen), yDist(gen));
    AddNode(pos);
  }

  for (int i = 0; i < nodeCount; ++i) {
    AddEdge(i, (i + 1) % nodeCount);
  }

  int extraEdges = static_cast<int>(nodeCount * 1.5f);
  std::uniform_int_distribution<int> nodeDist(0, nodeCount - 1);

  int attempts = 0;
  int added = 0;
  while (added < extraEdges && attempts < extraEdges * 10) {
    int u = nodeDist(gen);
    int v = nodeDist(gen);

    if (u != v) {
      size_t prevSize = edges.size();
      AddEdge(u, v);
      if (edges.size() > prevSize) {
        ++added;
      }
    }
    ++attempts;
  }

  std::cout << "[Game] Generated random graph: " << nodes.size()
            << " nodes, " << edges.size() << " edges" << std::endl;
}

void GameEngine::GenerateTestGraph() {
  float centerX = WINDOW_WIDTH / 2.0f;
  float centerY = WINDOW_HEIGHT / 2.0f;
  float spread = 150.0f;

  AddNode(Vec2(centerX - spread, centerY - spread * 0.5f));
  AddNode(Vec2(centerX + spread, centerY - spread * 0.5f));
  AddNode(Vec2(centerX - spread, centerY + spread * 0.5f));
  AddNode(Vec2(centerX + spread, centerY + spread * 0.5f));
  AddNode(Vec2(centerX, centerY - spread * 1.5f));
  AddNode(Vec2(centerX, centerY + spread * 1.5f));

  AddEdge(0, 3);
  AddEdge(1, 2);
  AddEdge(0, 1);
  AddEdge(2, 3);
  AddEdge(4, 5);
  AddEdge(0, 5);
  AddEdge(1, 5);
  AddEdge(4, 2);
  AddEdge(4, 3);

  std::cout << "[Game] Generated test graph: " << nodes.size()
            << " nodes, " << edges.size() << " edges" << std::endl;
}

void GameEngine::GenerateDynamicGraph(int nodeCount) {
  if (nodeCount < 3)
    nodeCount = 3;
  if (nodeCount > 200)
    nodeCount = 200;

  // Dispatch to difficulty-specific generator
  switch (currentDifficulty) {
  case Difficulty::EASY:
    GenerateEasyGraph(nodeCount);
    break;
  case Difficulty::MEDIUM:
    GenerateMediumGraph(nodeCount);
    break;
  case Difficulty::HARD:
    GenerateHardGraph(nodeCount);
    break;
  }
}

void GameEngine::GenerateEasyGraph(int nodeCount) {
  // Easy: Cycle + Chords (low rigidity, floppy)
  ClearGraph();

  // Create nodes (positions will be set by GeneratePlanarLayout)
  for (int i = 0; i < nodeCount; ++i) {
    AddNode(Vec2(0, 0));
  }

  // Create Hamiltonian cycle (ring connectivity)
  for (int i = 0; i < nodeCount; ++i) {
    AddEdge(i, (i + 1) % nodeCount);
  }

  // Add 2-3 non-crossing chords
  std::random_device rd;
  std::mt19937 gen(rd());
  int numChords = 2 + (nodeCount > 10 ? 1 : 0); // 2 or 3 chords

  auto edgesCrossOnCircle = [](int a, int b, int c, int d) -> bool {
    if (a > b)
      std::swap(a, b);
    if (c > d)
      std::swap(c, d);
    bool c_between = (a < c && c < b);
    bool d_between = (a < d && d < b);
    return c_between != d_between;
  };

  std::uniform_int_distribution<int> nodeDist(0, nodeCount - 1);
  int added = 0;
  int attempts = 0;
  while (added < numChords && attempts < 200) {
    int u = nodeDist(gen);
    int v = nodeDist(gen);
    ++attempts;

    if (u == v)
      continue;
    int diff = std::abs(u - v);
    if (diff == 1 || diff == nodeCount - 1)
      continue; // Adjacent in cycle

    bool exists = false;
    for (const Edge &e : edges) {
      if ((e.u_id == u && e.v_id == v) || (e.u_id == v && e.v_id == u)) {
        exists = true;
        break;
      }
    }
    if (exists)
      continue;

    bool wouldCross = false;
    for (const Edge &e : edges) {
      if (e.u_id == (e.v_id + 1) % nodeCount ||
          e.v_id == (e.u_id + 1) % nodeCount)
        continue; // Skip cycle edges
      if (edgesCrossOnCircle(u, v, e.u_id, e.v_id)) {
        wouldCross = true;
        break;
      }
    }

    if (!wouldCross) {
      AddEdge(u, v);
      ++added;
    }
  }

  GeneratePlanarLayout();
  ApplyCircleScramble();
  currentPhase = GamePhase::SHOWING_UNTANGLED;
  phaseStartTime = std::chrono::steady_clock::now();

  std::cout << "[Game] Easy graph: " << nodes.size() << " nodes, "
            << edges.size() << " edges (cycle + " << added << " chords)"
            << std::endl;
}

void GameEngine::GenerateMediumGraph(int nodeCount) {
  // Medium: Grid Mesh with holes (medium rigidity)
  ClearGraph();

  // Calculate grid dimensions (try to make it roughly square)
  int cols = static_cast<int>(std::ceil(std::sqrt(nodeCount)));
  int rows = (nodeCount + cols - 1) / cols;

  // Calculate grid spacing to center it on screen
  float centerX = WINDOW_WIDTH / 2.0f;
  float centerY = WINDOW_HEIGHT / 2.0f;
  float spacing =
      std::min(WINDOW_WIDTH, WINDOW_HEIGHT) / (std::max(rows, cols) + 1.0f);
  float startX = centerX - (cols - 1) * spacing / 2.0f;
  float startY = centerY - (rows - 1) * spacing / 2.0f;

  // Create nodes at grid positions
  for (int i = 0; i < nodeCount; ++i) {
    int row = i / cols;
    int col = i % cols;
    Vec2 pos(startX + col * spacing, startY + row * spacing);
    AddNode(pos);
  }

  // Connect grid edges (horizontal and vertical)
  std::vector<std::pair<int, int>> potentialEdges;
  for (int i = 0; i < nodeCount; ++i) {
    int row = i / cols;
    int col = i % cols;

    // Right neighbor
    if (col < cols - 1 && i + 1 < nodeCount) {
      potentialEdges.push_back({i, i + 1});
    }
    // Bottom neighbor
    if (row < rows - 1 && i + cols < nodeCount) {
      potentialEdges.push_back({i, i + cols});
    }
  }

  // Add all edges first
  for (const auto &e : potentialEdges) {
    AddEdge(e.first, e.second);
  }

  // Remove 20-25% of edges randomly (but keep graph connected)
  std::random_device rd;
  std::mt19937 gen(rd());
  int edgesToRemove = static_cast<int>(edges.size() * 0.22f);

  std::vector<int> edgeIndices(edges.size());
  std::iota(edgeIndices.begin(), edgeIndices.end(), 0);
  std::shuffle(edgeIndices.begin(), edgeIndices.end(), gen);

  std::vector<int> degree(nodeCount, 0);
  for (const Edge &e : edges) {
    degree[e.u_id]++;
    degree[e.v_id]++;
  }

  int removed = 0;
  for (int idx : edgeIndices) {
    if (removed >= edgesToRemove)
      break;
    Edge &e = edges[idx];
    if (degree[e.u_id] > 2 && degree[e.v_id] > 2) {
      degree[e.u_id]--;
      degree[e.v_id]--;
      e.u_id = e.v_id = -1; // Mark for removal
      ++removed;
    }
  }

  // Remove marked edges
  edges.erase(std::remove_if(edges.begin(), edges.end(),
                             [](const Edge &e) { return e.u_id == -1; }),
              edges.end());

  // Store grid positions as start positions for animation
  startPositions.resize(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    startPositions[i] = nodes[i].position;
  }

  ApplyCircleScramble();
  currentPhase = GamePhase::SHOWING_UNTANGLED;
  phaseStartTime = std::chrono::steady_clock::now();

  std::cout << "[Game] Medium graph: " << nodes.size() << " nodes, "
            << edges.size() << " edges (grid mesh)" << std::endl;
}

void GameEngine::GenerateHardGraph(int nodeCount) {
  // Hard: Triangulation (high rigidity, maximal planar)
  // Build by incrementally splitting faces - track positions during
  // construction
  ClearGraph();

  if (nodeCount < 3)
    nodeCount = 3;

  float centerX = WINDOW_WIDTH / 2.0f;
  float centerY = WINDOW_HEIGHT / 2.0f;
  float radius = std::min(WINDOW_WIDTH, WINDOW_HEIGHT) / 2.8f;

  // Start with initial triangle - place at outer vertices
  Vec2 p0(centerX, centerY - radius);                          // Top
  Vec2 p1(centerX - radius * 0.866f, centerY + radius * 0.5f); // Bottom-left
  Vec2 p2(centerX + radius * 0.866f, centerY + radius * 0.5f); // Bottom-right

  AddNode(p0);
  AddNode(p1);
  AddNode(p2);
  AddEdge(0, 1);
  AddEdge(1, 2);
  AddEdge(2, 0);

  // Track triangular faces
  struct Face {
    int a, b, c;
  };
  std::vector<Face> faces;
  faces.push_back({0, 1, 2});

  std::random_device rd;
  std::mt19937 gen(rd());

  // Incrementally add nodes by splitting faces
  for (int newNode = 3; newNode < nodeCount; ++newNode) {
    // Pick a random face to split
    std::uniform_int_distribution<int> faceDist(
        0, static_cast<int>(faces.size()) - 1);
    int faceIdx = faceDist(gen);
    Face &face = faces[faceIdx];

    // Calculate centroid of the face for new node position
    Vec2 pa = nodes[face.a].position;
    Vec2 pb = nodes[face.b].position;
    Vec2 pc = nodes[face.c].position;

    // Add small jitter to avoid visual overlap
    std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
    Vec2 centroid((pa.x + pb.x + pc.x) / 3.0f + jitter(gen) * 20.0f,
                  (pa.y + pb.y + pc.y) / 3.0f + jitter(gen) * 20.0f);

    AddNode(centroid);

    // Connect new node to all corners of the face
    AddEdge(newNode, face.a);
    AddEdge(newNode, face.b);
    AddEdge(newNode, face.c);

    // Replace the old face with 3 new faces
    int a = face.a, b = face.b, c = face.c;
    faces.erase(faces.begin() + faceIdx);
    faces.push_back({a, b, newNode});
    faces.push_back({b, c, newNode});
    faces.push_back({c, a, newNode});
  }

  // Store planar positions as start positions for animation
  startPositions.resize(nodes.size());
  for (size_t i = 0; i < nodes.size(); ++i) {
    startPositions[i] = nodes[i].position;
  }

  // Setup scrambled target positions
  ApplyCircleScramble();

  currentPhase = GamePhase::SHOWING_UNTANGLED;
  phaseStartTime = std::chrono::steady_clock::now();

  std::cout << "[Game] Hard graph: " << nodes.size() << " nodes, "
            << edges.size() << " edges (triangulation)" << std::endl;
}

void GameEngine::ApplyCircleScramble() {
  // Tangle by placing nodes in random order around a circle
  // This creates maximum visual chaos (the "hairball" effect)
  // Only sets targetPositions - startPositions should already be set from
  // planar layout

  std::random_device rd;
  std::mt19937 gen(rd());

  // Create shuffled indices
  std::vector<int> order(nodes.size());
  std::iota(order.begin(), order.end(), 0);
  std::shuffle(order.begin(), order.end(), gen);

  // Generate random target positions on a circle
  float centerX = WINDOW_WIDTH / 2.0f;
  float centerY = WINDOW_HEIGHT / 2.0f;
  float radius = std::min(WINDOW_WIDTH, WINDOW_HEIGHT) / 2.5f;

  size_t n = nodes.size();
  targetPositions.resize(n);

  // Only resize startPositions if not already set (for Easy/Medium that call
  // GeneratePlanarLayout)
  if (startPositions.size() != n) {
    startPositions.resize(n);
    for (size_t i = 0; i < n; ++i) {
      startPositions[i] = nodes[i].position;
    }
  }

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
  for (size_t i = 0; i < n; ++i) {
    // Place node order[i] at position i on circle
    float angle = 2.0f * M_PI * static_cast<float>(i) / n - M_PI / 2.0f;
    targetPositions[order[i]] = Vec2(centerX + radius * std::cos(angle),
                                     centerY + radius * std::sin(angle));
  }
}

void GameEngine::GeneratePlanarLayout() {
  // Arrange nodes in a circle for guaranteed planar layout
  float centerX = WINDOW_WIDTH / 2.0f;
  float centerY = WINDOW_HEIGHT / 2.0f;
  float radius = std::min(WINDOW_WIDTH, WINDOW_HEIGHT) / 2.5f;

  size_t n = nodes.size();
  for (size_t i = 0; i < n; ++i) {
    float angle =
        (2.0f * 3.14159265f * static_cast<float>(i)) / static_cast<float>(n);
    angle -= 3.14159265f / 2.0f; // Start from top
    nodes[i].position.x = centerX + radius * std::cos(angle);
    nodes[i].position.y = centerY + radius * std::sin(angle);
  }
}

void GameEngine::GenerateTangledTargets() {
  std::random_device rd;
  std::mt19937 gen(rd());

  const float margin = 80.0f;
  std::uniform_real_distribution<float> xDist(margin, WINDOW_WIDTH - margin);
  std::uniform_real_distribution<float> yDist(margin, WINDOW_HEIGHT - margin);

  startPositions.clear();
  targetPositions.clear();

  for (const Node &node : nodes) {
    startPositions.push_back(node.position);
    targetPositions.push_back(Vec2(xDist(gen), yDist(gen)));
  }
}

float GameEngine::EaseOutCubic(float t) {
  // Smooth deceleration curve
  return 1.0f - std::pow(1.0f - t, 3.0f);
}

void GameEngine::UpdatePhase() {
  auto now = std::chrono::steady_clock::now();
  float elapsed = std::chrono::duration<float>(now - phaseStartTime).count();

  switch (currentPhase) {
  case GamePhase::MAIN_MENU:
    return;
  case GamePhase::SHOWING_UNTANGLED:
    if (elapsed >= UNTANGLED_DISPLAY_DURATION) {
      // Transition to tangling phase
      GenerateTangledTargets();
      currentPhase = GamePhase::TANGLING;
      phaseStartTime = now;
      animationProgress = 0.0f;
      std::cout << "[Game] Starting tangle animation..." << std::endl;
    }
    break;

  case GamePhase::TANGLING:
    animationProgress = elapsed / TANGLE_ANIMATION_DURATION;
    if (animationProgress >= 1.0f) {
      animationProgress = 1.0f;
      currentPhase = GamePhase::PLAYING;
      gameStartTime = std::chrono::steady_clock::now();
      moveCount = 0;

      // Start CPU race mode
      StartCPURace();

      std::cout << "[Game] Race started! Untangle the graph."
                << std::endl;
    }

    // Interpolate node positions with easing
    {
      float t = EaseOutCubic(animationProgress);
      for (size_t i = 0; i < nodes.size(); ++i) {
        nodes[i].position.x = startPositions[i].x +
                              t * (targetPositions[i].x - startPositions[i].x);
        nodes[i].position.y = startPositions[i].y +
                              t * (targetPositions[i].y - startPositions[i].y);
      }
    }
    break;

  case GamePhase::PLAYING:
    // Victory check is done in Update() - no phase updates here
    break;

  case GamePhase::VICTORY_BLINK:
    // Flash animation - alternate between bright and normal
    {
      float blinkElapsed =
          std::chrono::duration<float>(now - victoryStartTime).count();
      int currentBlink = static_cast<int>(blinkElapsed / BLINK_DURATION);
      if (currentBlink >= TOTAL_BLINKS * 2) {
        // Blinks complete, show victory screen
        currentPhase = GamePhase::VICTORY;
        std::cout << "[Game] Victory! Showing analytics." << std::endl;
      }
    }
    break;

  case GamePhase::VICTORY:
    // Stay on victory screen until player starts new game
    break;

  case GamePhase::GAME_ENDED:
    // Stay on game ended screen until player starts new game
    break;

  case GamePhase::REPLAY_VIEWER:
    // Auto-play: advance step periodically
    if (replayPlaying_ && cpuReplayLogger_ &&
        replayCurrentStep_ < cpuReplayLogger_->GetTotalMoves()) {
      auto now = std::chrono::steady_clock::now();
      float elapsed =
          std::chrono::duration<float>(now - replayLastStepTime_).count();
      if (elapsed >= REPLAY_STEP_INTERVAL) {
        ReplayGoToStep(replayCurrentStep_ + 1);
        replayLastStepTime_ = now;
      }
    } else if (replayPlaying_ && cpuReplayLogger_ &&
               replayCurrentStep_ >= cpuReplayLogger_->GetTotalMoves()) {
      replayPlaying_ = false; // Stop at end
    }
    break;

  case GamePhase::BENCHMARK_RESULTS:
    // Static screen, no updates needed
    break;
  case GamePhase::SCALABILITY_RESULTS:
    // Static screen, no updates needed
    break;
  case GamePhase::HOW_IT_WORKS:
    // Static screen, no updates needed
    break;
  }
}

void GameEngine::SetupMenus() {
  if (!menuBar)
    return;

  // Game menu
  std::vector<MenuItem> gameMenu = {
      MenuItem("New Game", [this]() { StartNewGame(); }),
      MenuItem("Restart", [this]() { StartNewGame(); }),
      MenuItem(), // Separator
      MenuItem("Auto Solve (Forfeit)", [this]() { StartAutoSolve(); }),
      MenuItem(
          "Toggle Heatmap (H)", [this]() { ToggleHeatmap(); }, true,
          heatmapEnabled_),
      MenuItem("View CPU Replay", [this]() { StartReplayViewer(); }),
      MenuItem("Run Benchmark", [this]() { StartComputingBenchmark(); }),
      MenuItem("Complexity Analysis", [this]() { StartComputingScalability(); }),
      MenuItem("How It Works", [this]() {
        howItWorksTab_ = 0;
        currentPhase = GamePhase::HOW_IT_WORKS;
      }),
      MenuItem(), // Separator
      MenuItem("Exit", [this]() { isRunning = false; })};
  menuBar->AddMenu("Game", gameMenu);

  std::vector<MenuItem> modeMenu = {
      MenuItem(
          "Greedy", [this]() { SetGameMode(GameMode::GREEDY); }, true,
          currentMode == GameMode::GREEDY),
      MenuItem(
          "D&C + DP", [this]() { SetGameMode(GameMode::DIVIDE_AND_CONQUER_DP); }, true,
          currentMode == GameMode::DIVIDE_AND_CONQUER_DP),
      MenuItem(
          "Backtracking", [this]() { SetGameMode(GameMode::BACKTRACKING); }, true,
          currentMode == GameMode::BACKTRACKING)};
  menuBar->AddMenu("Mode", modeMenu);

  // Settings menu - Node counts: 10, 15, 20, Custom (max 200)
  std::vector<MenuItem> settingsMenu = {
      MenuItem(
          "10 Nodes", [this]() { SetNodeCount(10); }, true,
          currentNodeCount == 10),
      MenuItem(
          "15 Nodes", [this]() { SetNodeCount(15); }, true,
          currentNodeCount == 15),
      MenuItem(
          "20 Nodes", [this]() { SetNodeCount(20); }, true,
          currentNodeCount == 20),
      MenuItem(
          "Custom...", [this]() { ShowCustomNodeDialog(); }, false, false),
      MenuItem(), // Separator
      // Difficulty
      MenuItem(
          "Easy", [this]() { SetDifficulty(Difficulty::EASY); }, true,
          currentDifficulty == Difficulty::EASY),
      MenuItem(
          "Medium", [this]() { SetDifficulty(Difficulty::MEDIUM); }, true,
          currentDifficulty == Difficulty::MEDIUM),
      MenuItem(
          "Hard", [this]() { SetDifficulty(Difficulty::HARD); }, true,
          currentDifficulty == Difficulty::HARD)};
  menuBar->AddMenu("Settings", settingsMenu);

  // Help menu
  std::vector<MenuItem> helpMenu = {
      MenuItem("Controls",
               []() {
                 std::cout << "\n=== Controls ===" << std::endl;
                 std::cout << "Left Click + Drag: Move nodes" << std::endl;
                 std::cout << "ESC: Quit" << std::endl;
                 std::cout << "Goal: Make all edges green!\n" << std::endl;
               }),
      MenuItem("About", []() {
        std::cout << "\n=== Greedy Tangle ===" << std::endl;
        std::cout << "A Graph Theory Puzzle Game" << std::endl;
        std::cout << "Version 0.2.0\n" << std::endl;
      })};
  menuBar->AddMenu("Help", helpMenu);
}

void GameEngine::StartNewGame() { GenerateDynamicGraph(currentNodeCount); }

void GameEngine::SetNodeCount(int count) {
  // Clamp to valid range (3-200)
  if (count < 3)
    count = 3;
  if (count > 200)
    count = 200;
  currentNodeCount = count;

  // Menu layout: 0: 10 Nodes, 1: 15 Nodes, 2: 20 Nodes, 3: Custom...
  // 4: separator, 5: Easy, 6: Medium, 7: Hard
  if (menuBar) {
    // Settings is now at index 2
    menuBar->SetItemChecked(2, 0, count == 10);
    menuBar->SetItemChecked(2, 1, count == 15);
    menuBar->SetItemChecked(2, 2, count == 20);
    // Custom option stays unchecked (non-checkable)
  }

  if (currentPhase != GamePhase::MAIN_MENU) {
    StartNewGame();
  }
}

void GameEngine::SetDifficulty(Difficulty diff) {
  currentDifficulty = diff;

  // Difficulty at indices 5, 6, 7
  if (menuBar) {
    // Settings is now at index 2
    menuBar->SetItemChecked(2, 5, diff == Difficulty::EASY);
    menuBar->SetItemChecked(2, 6, diff == Difficulty::MEDIUM);
    menuBar->SetItemChecked(2, 7, diff == Difficulty::HARD);
  }

  if (currentPhase != GamePhase::MAIN_MENU) {
    StartNewGame();
  }
}

void GameEngine::SetGameMode(GameMode mode) {
  // Wait for any pending CPU task to finish before destroying the solver
  if (cpuSolving_ && cpuFuture_.valid()) {
    std::cout << "[Game] Waiting for pending CPU move to finish before switching mode..." << std::endl;
    cpuFuture_.wait();
    cpuSolving_ = false;
  }

  currentMode = mode;

  currentSolver_ = CreateSolver(static_cast<SolverMode>(mode));

  if (menuBar) {
    menuBar->SetItemChecked(1, 0, mode == GameMode::GREEDY);
    menuBar->SetItemChecked(1, 1, mode == GameMode::DIVIDE_AND_CONQUER_DP);
    menuBar->SetItemChecked(1, 2, mode == GameMode::BACKTRACKING);
  }

  // Restart game to apply new mode from scratch
  // Restart game to apply new mode from scratch
  if (currentPhase != GamePhase::MAIN_MENU) {
    StartNewGame();
  }
}

void GameEngine::CheckVictory() {
  // Only check during gameplay
  if (currentPhase != GamePhase::PLAYING)
    return;

  // Victory when no intersections
  if (intersectionCount == 0 && !edges.empty()) {
    // Record time
    auto now = std::chrono::steady_clock::now();
    gameDuration = std::chrono::duration<float>(now - gameStartTime).count();

    // Determine winner based on lowest time
    if (cpuFinished_ && cpuGameDuration_ > 0.0f) {
      if (gameDuration < cpuGameDuration_) {
        winner_ = "human";
        std::cout << "[Game] YOU WIN! " << gameDuration << "s vs CPU "
                  << cpuGameDuration_ << "s" << std::endl;
      } else {
        winner_ = "cpu";
        std::cout << "[Game] CPU WINS! " << cpuGameDuration_ << "s vs YOU "
                  << gameDuration << "s" << std::endl;
      }
    } else {
      winner_ = "human";
      std::cout << "[Game] YOU WIN! CPU didn't finish." << std::endl;
    }

    // Start victory blink animation
    victoryStartTime = now;
    blinkCount = 0;
    currentPhase = GamePhase::VICTORY_BLINK;

    std::cout << "[Game] Congratulations! Graph untangled in "
              << gameDuration << "s with " << moveCount << " moves!"
              << std::endl;
  }
}

void GameEngine::RenderVictoryScreen() {
  // Semi-transparent dark overlay
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
  SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
  SDL_RenderFillRect(renderer, &overlay);

  // Victory panel
  int panelW = 400;
  int panelH = 300;
  int panelX = (WINDOW_WIDTH - panelW) / 2;
  int panelY = (WINDOW_HEIGHT - panelH) / 2;

  // Panel background (slightly lighter)
  SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
  SDL_Rect panel = {panelX, panelY, panelW, panelH};
  SDL_RenderFillRect(renderer, &panel);

  // Panel border (green for victory)
  SDL_SetRenderDrawColor(renderer, 50, 205, 50, 255);
  SDL_RenderDrawRect(renderer, &panel);

  // Draw victory text boxes with statistics
  int textY = panelY + 25;
  int lineHeight = 40;
  int boxHeight = 32;

  // Title bar - show winner
  SDL_SetRenderDrawColor(renderer, 50, 205, 50, 255);
  SDL_Rect titleBar = {panelX + 20, textY, panelW - 40, boxHeight};
  SDL_RenderFillRect(renderer, &titleBar);
  if (menuBar) {
    std::string title = "VICTORY!";
    if (winner_ == "cpu") {
      title = "CPU WINS!";
      SDL_SetRenderDrawColor(renderer, 220, 50, 50, 255);
      SDL_Rect titleBarRed = {panelX + 20, textY, panelW - 40, boxHeight};
      SDL_RenderFillRect(renderer, &titleBarRed);
    } else if (winner_ == "human") {
      title = "YOU WIN!";
    } else if (winner_ == "tie") {
      title = "TIE!";
    }
    menuBar->RenderTextCentered(title, titleBar, {20, 20, 25, 255});
  }

  textY += lineHeight + 15;

  // Helper to draw stat box with text
  auto drawStatWithText = [&](int y, SDL_Color boxColor,
                              const std::string &text) {
    SDL_SetRenderDrawColor(renderer, boxColor.r, boxColor.g, boxColor.b,
                           boxColor.a);
    SDL_Rect box = {panelX + 30, y, panelW - 60, boxHeight};
    SDL_RenderDrawRect(renderer, &box);
    if (menuBar) {
      menuBar->RenderTextCentered(text, box, boxColor);
    }
  };

  // Format time string
  std::ostringstream timeStr;
  timeStr << "Time: " << std::fixed << std::setprecision(2) << gameDuration
          << "s";
  if (cpuGameDuration_ > 0.0f) {
    timeStr << " | CPU: " << cpuGameDuration_ << "s";
  } else {
    timeStr << " | CPU: --";
  }

  // Format moves string
  // Format moves string
  std::string movesStr = "Moves: " + std::to_string(moveCount);
  if (cpuMoveCount_ > 0) {
    movesStr += " | CPU: " + std::to_string(cpuMoveCount_);
  }

  // Format nodes string
  std::string nodesStr = "Nodes: " + std::to_string(nodes.size());

  // Format edges string
  std::string edgesStr = "Edges: " + std::to_string(edges.size());

  // Time stat
  drawStatWithText(textY, {100, 180, 255, 255}, timeStr.str());
  textY += lineHeight;

  // Moves stat
  drawStatWithText(textY, {255, 180, 100, 255}, movesStr);
  textY += lineHeight;

  // Nodes stat
  drawStatWithText(textY, {180, 255, 100, 255}, nodesStr);
  textY += lineHeight;

  // Edges stat
  drawStatWithText(textY, {255, 100, 180, 255}, edgesStr);
  textY += lineHeight + 10;

  // "New Game" hint box
  SDL_SetRenderDrawColor(renderer, 80, 80, 85, 255);
  SDL_Rect hintBox = {panelX + 40, textY, panelW - 80, boxHeight};
  SDL_RenderFillRect(renderer, &hintBox);
  SDL_SetRenderDrawColor(renderer, 150, 150, 155, 255);
  SDL_RenderDrawRect(renderer, &hintBox);
  if (menuBar) {
    menuBar->RenderTextCentered("Game > New Game to play again", hintBox,
                                {180, 180, 185, 255});
  }

  // Print analytics to console (visible confirmation)
  static bool printed = false;
  if (currentPhase == GamePhase::VICTORY && !printed) {
    std::cout << "\n=== VICTORY! ===" << std::endl;
    std::cout << "Time:  " << std::fixed << std::setprecision(2) << gameDuration
              << " seconds" << std::endl;
    std::cout << "Moves: " << moveCount << std::endl;
    std::cout << "Nodes: " << nodes.size() << std::endl;
    std::cout << "Edges: " << edges.size() << std::endl;
    std::cout << "================\n" << std::endl;
    printed = true;
  }
}

void GameEngine::ShowCustomNodeDialog() {
  inputBuffer.clear();
  showInputDialog = true;
  inputCursorBlink = std::chrono::steady_clock::now();
  SDL_StartTextInput(); // Enable text input mode
}

void GameEngine::RenderInputDialog() {
  if (!showInputDialog)
    return;

  // Semi-transparent overlay
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
  SDL_Rect overlay = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
  SDL_RenderFillRect(renderer, &overlay);

  // Dialog panel
  int panelW = 320;
  int panelH = 150;
  int panelX = (WINDOW_WIDTH - panelW) / 2;
  int panelY = (WINDOW_HEIGHT - panelH) / 2;

  SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
  SDL_Rect panel = {panelX, panelY, panelW, panelH};
  SDL_RenderFillRect(renderer, &panel);

  SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
  SDL_RenderDrawRect(renderer, &panel);

  // Title
  SDL_Rect titleRect = {panelX + 10, panelY + 15, panelW - 20, 25};
  if (menuBar) {
    menuBar->RenderTextCentered("Enter Node Count (3-200)", titleRect,
                                {200, 200, 210, 255});
  }

  // Input field
  SDL_SetRenderDrawColor(renderer, 30, 30, 35, 255);
  SDL_Rect inputField = {panelX + 40, panelY + 55, panelW - 80, 35};
  SDL_RenderFillRect(renderer, &inputField);
  SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
  SDL_RenderDrawRect(renderer, &inputField);

  // Display typed text with blinking cursor
  std::string displayText = inputBuffer;
  auto now = std::chrono::steady_clock::now();
  float elapsed = std::chrono::duration<float>(now - inputCursorBlink).count();
  if (static_cast<int>(elapsed * 2) % 2 == 0) {
    displayText += "_"; // Blinking cursor
  }

  if (menuBar && !displayText.empty()) {
    menuBar->RenderTextCentered(displayText, inputField, {255, 255, 255, 255});
  }

  // Instructions
  SDL_Rect instrRect = {panelX + 10, panelY + 105, panelW - 20, 25};
  if (menuBar) {
    menuBar->RenderTextCentered("Enter to confirm, ESC to cancel", instrRect,
                                {128, 128, 135, 255});
  }
}

// ============== RACE MODE IMPLEMENTATION ==============

void GameEngine::StartCPURace() {
  // Copy human's graph state for CPU to solve independently
  cpuNodes_ = nodes;
  cpuIntersectionCount_ = intersectionCount;
  cpuSolving_ = false;
  cpuFinished_ = false;
  cpuPaused_ = false;
  cpuMoveCount_ = 0;
  cpuGameDuration_ = 0.0f;
  winner_ = "";

  // Initialize delay timer so first move also respects delay
  cpuLastMoveTime_ = std::chrono::steady_clock::now();

  // Initialize replay logger with initial state
  cpuReplayLogger_->StartMatch(cpuNodes_, edges, cpuIntersectionCount_);

  std::cout << "[Game] Starting race mode! H: " << intersectionCount
            << " | CPU: " << cpuIntersectionCount_ << std::endl;

  // Note: Don't call StartNextCPUMove() immediately - let the delay timer work
  // The UpdateCPURace() loop will trigger it after the delay
}

void GameEngine::StartNextCPUMove() {
  if (cpuFinished_ || cpuSolving_) {
    return;
  }

  // Check if CPU already won
  // Check if CPU already won
  if (cpuIntersectionCount_ == 0) {
    if (!cpuFinished_) {
      cpuFinished_ = true;
      auto now = std::chrono::steady_clock::now();
      cpuGameDuration_ =
          std::chrono::duration<float>(now - gameStartTime).count();

      if (winner_.empty()) {
        std::cout << "[Game] CPU finished in " << cpuMoveCount_ << " moves (" 
                  << std::fixed << std::setprecision(2) << cpuGameDuration_ << "s)."
                  << std::endl;
      }
    }
    return;
  }

  // Apply difficulty-based delay between moves
  float delay = GetCPUDelay();
  if (delay > 0.0f) {
    auto now = std::chrono::steady_clock::now();
    float elapsed =
        std::chrono::duration<float>(now - cpuLastMoveTime_).count();
    if (elapsed < delay) {
      return; // Wait for delay to pass
    }
  }

  cpuSolving_ = true;
  cpuCancelFlag_.store(false);
  currentSolver_->SetCancelFlag(&cpuCancelFlag_);

  // Deep copy for thread safety
  auto nodes_copy = cpuNodes_;
  auto edges_copy = edges;

  cpuFuture_ = std::async(std::launch::async, [this, nodes_copy, edges_copy]() {
    return currentSolver_->FindBestMove(nodes_copy, edges_copy);
  });
}

float GameEngine::GetCPUDelay() const {
  switch (currentDifficulty) {
  case Difficulty::EASY:
    return CPU_DELAY_EASY;
  case Difficulty::MEDIUM:
    return CPU_DELAY_MEDIUM;
  case Difficulty::HARD:
  default:
    return CPU_DELAY_HARD;
  }
}

void GameEngine::UpdateCPURace() {
  // Only active during PLAYING phase
  if (currentPhase != GamePhase::PLAYING) {
    return;
  }

  // Skip if CPU is paused by user
  if (cpuPaused_) {
    return;
  }

  // Check if human just solved - compare moves with CPU
  if (intersectionCount == 0 && !edges.empty() && winner_.empty()) {
    // Wait for CPU to finish if still solving
    if (!cpuFinished_) {
      // CPU is still solving - let it finish first
      // (This frame we'll process CPU, next frame check again)
    } else {
      // Both finished - compare move counts
      // Both finished (or User finished and CPU strictly slower/still working)
        
      // Capture User time
      auto now = std::chrono::steady_clock::now();
      gameDuration = std::chrono::duration<float>(now - gameStartTime).count();
      
      if (cpuFinished_) {
          if (cpuGameDuration_ < gameDuration) {
              winner_ = "cpu";
              std::cout << "[Game] CPU WINS! Time: " << cpuGameDuration_ << "s vs " << gameDuration << "s" << std::endl;
          } else {
              winner_ = "human";
              std::cout << "[Game] YOU WIN! Time: " << gameDuration << "s vs " << cpuGameDuration_ << "s" << std::endl;
          }
      } else {
          winner_ = "human";
           std::cout << "[Game] YOU WIN! CPU didn't finish." << std::endl;
      }
      
      // Trigger Victory Animation
      victoryStartTime = now;
      blinkCount = 0;
      currentPhase = GamePhase::VICTORY_BLINK;
      return;
    }
  }

  // === CPU Background Solving (runs as fast as possible) ===

  // Check if CPU move computation completed
  if (cpuSolving_ && cpuFuture_.valid()) {
    if (cpuFuture_.wait_for(std::chrono::milliseconds(0)) ==
        std::future_status::ready) {
      CPUMove move = cpuFuture_.get();
      cpuSolving_ = false;

      if (move.isValid()) {
        // Apply the move to CPU's graph
        cpuNodes_[move.node_id].position = move.to_position;
        cpuIntersectionCount_ = move.intersections_after;
        ++cpuMoveCount_;

        // Log the move
        cpuReplayLogger_->RecordMove(move);

        std::cout << "[CPU] Move #" << cpuMoveCount_ << ": Node "
                  << move.node_id
                  << " | Intersections: " << cpuIntersectionCount_ << std::endl;

        // Check if CPU finished
        if (cpuIntersectionCount_ == 0) {
          cpuFinished_ = true;
          std::cout << "[CPU] Solved in " << cpuMoveCount_ << " moves!"
                    << std::endl;

          // CPU finished - just mark it and log
          if (intersectionCount > 0 && winner_.empty()) {
             // Do not declare winner yet - let user play until they finish
             std::cout << "[Game] CPU finished! Keep going to beat the time!" << std::endl;
          }
        }
      } else {
        // CPU is stuck
        cpuFinished_ = true;
        std::cout << "[CPU] Stuck in local minimum at " << cpuIntersectionCount_
                  << " intersections" << std::endl;
      }
    }
  }

  // Start next CPU move if not done
  if (!cpuSolving_ && !cpuFinished_) {
    cpuSolving_ = true;
    auto nodes_copy = cpuNodes_;
    auto edges_copy = edges;

    cpuFuture_ =
        std::async(std::launch::async, [this, nodes_copy, edges_copy]() {
          return currentSolver_->FindBestMove(nodes_copy, edges_copy);
        });
  }
}

void GameEngine::RenderScoreboard() {
  if (currentPhase != GamePhase::PLAYING) {
    return;
  }

  // Scoreboard background - use runtime window size
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);
  int scoreW = 450;
  int scoreH = 40;
  int scoreX = (winW - scoreW) / 2;
  int scoreY = winH - scoreH - 10;

  SDL_SetRenderDrawColor(renderer, 30, 30, 35, 220);
  SDL_Rect scoreRect = {scoreX, scoreY, scoreW, scoreH};
  SDL_RenderFillRect(renderer, &scoreRect);

  // Border color based on who's ahead (fewer intersections)
  SDL_Color borderColor;
  if (intersectionCount < cpuIntersectionCount_) {
    borderColor = {50, 205, 50, 255}; // Green - human ahead
  } else if (cpuIntersectionCount_ < intersectionCount) {
    borderColor = {220, 50, 50, 255}; // Red - CPU ahead
  } else {
    borderColor = {255, 255, 100, 255}; // Yellow - tied
  }

  SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b,
                         borderColor.a);
  SDL_RenderDrawRect(renderer, &scoreRect);

  // Render score text with move counts
  if (menuBar) {
    std::string solverName = currentSolver_ ? currentSolver_->GetName() : "CPU";
    std::string cpuStatus =
        cpuFinished_
            ? solverName + ": Solved (" + std::to_string(cpuMoveCount_) +
                  " moves)"
            : solverName + ": " + std::to_string(cpuIntersectionCount_) +
                  " left";
    std::string scoreText =
        "Human: " + std::to_string(intersectionCount) + " left  |  " + cpuStatus;
    menuBar->RenderTextCentered(scoreText, scoreRect, {255, 255, 255, 255});
  }

  // === End Game and Pause/Continue buttons above scoreboard ===
  int btnY = scoreY - 35;

  // End Game button (left)
  SDL_Rect endGameBtn = {scoreX, btnY, 120, 30};
  SDL_SetRenderDrawColor(renderer, 180, 40, 40, 255); // Red
  SDL_RenderFillRect(renderer, &endGameBtn);
  SDL_SetRenderDrawColor(renderer, 220, 80, 80, 255);
  SDL_RenderDrawRect(renderer, &endGameBtn);
  if (menuBar) {
    menuBar->RenderTextCentered("End Game", endGameBtn, {255, 255, 255, 255});
  }

  // Pause / Continue button (right)
  SDL_Rect pauseBtn = {scoreX + scoreW - 140, btnY, 140, 30};
  if (cpuPaused_) {
    SDL_SetRenderDrawColor(renderer, 40, 160, 80, 255); // Green when paused
    SDL_RenderFillRect(renderer, &pauseBtn);
    SDL_SetRenderDrawColor(renderer, 80, 200, 120, 255);
    SDL_RenderDrawRect(renderer, &pauseBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Continue", pauseBtn, {255, 255, 255, 255});
    }
  } else {
    SDL_SetRenderDrawColor(renderer, 40, 100, 180, 255); // Blue when active
    SDL_RenderFillRect(renderer, &pauseBtn);
    SDL_SetRenderDrawColor(renderer, 80, 140, 220, 255);
    SDL_RenderDrawRect(renderer, &pauseBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Pause CPU", pauseBtn, {255, 255, 255, 255});
    }
  }
}

// ============== END GAME / PAUSE CPU ==============

void GameEngine::EndGame() {
  if (currentPhase != GamePhase::PLAYING) {
    return;
  }

  // Cancel any in-flight CPU async task
  cpuCancelFlag_.store(true);
  if (cpuSolving_ && cpuFuture_.valid()) {
    cpuFuture_.wait();
  }

  cpuSolving_ = false;
  cpuFinished_ = true;
  cpuPaused_ = false;
  autoSolveActive_ = false;

  // Record player time
  auto now = std::chrono::steady_clock::now();
  gameDuration = std::chrono::duration<float>(now - gameStartTime).count();

  currentPhase = GamePhase::GAME_ENDED;
  std::cout << "[Game] Game ended by player. You lost!" << std::endl;
}

void GameEngine::TogglePauseCPU() {
  if (currentPhase != GamePhase::PLAYING) {
    return;
  }

  cpuPaused_ = !cpuPaused_;

  if (cpuPaused_) {
    // Cancel any in-flight solver computation
    cpuCancelFlag_.store(true);
    if (cpuSolving_ && cpuFuture_.valid()) {
      cpuFuture_.wait(); // Wait for cancelled solver to return
      cpuSolving_ = false;
    }
    std::cout << "[Game] CPU paused." << std::endl;
  } else {
    // Reset delay timer so CPU resumes cleanly
    cpuCancelFlag_.store(false);
    cpuLastMoveTime_ = std::chrono::steady_clock::now();
    std::cout << "[Game] CPU resumed." << std::endl;
  }
}

void GameEngine::RenderGameEndedScreen() {
  // Semi-transparent dark overlay
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
  SDL_Rect overlay = {0, 0, winW, winH};
  SDL_RenderFillRect(renderer, &overlay);

  // Panel
  int panelW = 400;
  int panelH = 260;
  int panelX = (winW - panelW) / 2;
  int panelY = (winH - panelH) / 2;

  SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
  SDL_Rect panel = {panelX, panelY, panelW, panelH};
  SDL_RenderFillRect(renderer, &panel);

  // Red border for loss
  SDL_SetRenderDrawColor(renderer, 220, 50, 50, 255);
  SDL_RenderDrawRect(renderer, &panel);

  int textY = panelY + 25;
  int lineHeight = 40;
  int boxHeight = 32;

  // Title bar - red "GAME ENDED"
  SDL_SetRenderDrawColor(renderer, 220, 50, 50, 255);
  SDL_Rect titleBar = {panelX + 20, textY, panelW - 40, boxHeight};
  SDL_RenderFillRect(renderer, &titleBar);
  if (menuBar) {
    menuBar->RenderTextCentered("GAME ENDED", titleBar, {255, 255, 255, 255});
  }

  textY += lineHeight + 10;

  // "You Lost!" subtitle
  SDL_Rect lostRect = {panelX + 20, textY, panelW - 40, boxHeight};
  if (menuBar) {
    menuBar->RenderTextCentered("You Lost!", lostRect, {220, 80, 80, 255});
  }

  textY += lineHeight + 5;

  // Stats helper
  auto drawStat = [&](int y, SDL_Color color, const std::string &text) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect box = {panelX + 30, y, panelW - 60, boxHeight};
    SDL_RenderDrawRect(renderer, &box);
    if (menuBar) {
      menuBar->RenderTextCentered(text, box, color);
    }
  };

  // Time
  std::ostringstream timeStr;
  timeStr << "Time: " << std::fixed << std::setprecision(2) << gameDuration << "s";
  drawStat(textY, {100, 180, 255, 255}, timeStr.str());
  textY += lineHeight;

  // Moves
  drawStat(textY, {255, 180, 100, 255}, "Moves: " + std::to_string(moveCount));
  textY += lineHeight + 10;

  // Hint
  SDL_SetRenderDrawColor(renderer, 80, 80, 85, 255);
  SDL_Rect hintBox = {panelX + 40, textY, panelW - 80, boxHeight};
  SDL_RenderFillRect(renderer, &hintBox);
  SDL_SetRenderDrawColor(renderer, 150, 150, 155, 255);
  SDL_RenderDrawRect(renderer, &hintBox);
  if (menuBar) {
    menuBar->RenderTextCentered("Game > New Game to play again", hintBox,
                                {180, 180, 185, 255});
  }
}

// ============== AUTO-SOLVE IMPLEMENTATION ==============

void GameEngine::StartAutoSolve() {
  if (currentPhase != GamePhase::PLAYING || autoSolveActive_) {
    return;
  }

  // Forfeit the game
  winner_ = "forfeit";
  cpuFinished_ = true;
  autoSolveActive_ = true;
  autoSolveAnimating_ = false;

  std::cout << "[AutoSolve] Human forfeited. Showing CPU solution..."
            << std::endl;
}

void GameEngine::UpdateAutoSolve() {
  if (!autoSolveActive_ || currentPhase != GamePhase::PLAYING) {
    return;
  }

  // If currently animating a move
  if (autoSolveAnimating_) {
    auto now = std::chrono::steady_clock::now();
    float elapsed =
        std::chrono::duration<float>(now - cpuLastMoveTime_).count();
    autoSolveAnimProgress_ = elapsed / AUTO_SOLVE_ANIM_DURATION;

    if (autoSolveAnimProgress_ >= 1.0f) {
      // Animation complete - apply final position
      nodes[autoSolveCurrentMove_.node_id].position =
          autoSolveCurrentMove_.to_position;
      autoSolveAnimating_ = false;
      ++moveCount; // Count as human move for display
    } else {
      // Interpolate position
      float t = autoSolveAnimProgress_;
      Vec2 &pos = nodes[autoSolveCurrentMove_.node_id].position;
      pos.x = autoSolveCurrentMove_.from_position.x +
              t * (autoSolveCurrentMove_.to_position.x -
                   autoSolveCurrentMove_.from_position.x);
      pos.y = autoSolveCurrentMove_.from_position.y +
              t * (autoSolveCurrentMove_.to_position.y -
                   autoSolveCurrentMove_.from_position.y);
    }
    return;
  }

  // Check if solved
  if (intersectionCount == 0) {
    autoSolveActive_ = false;
    std::cout << "[AutoSolve] Complete! Solution shown." << std::endl;
    return;
  }

  // Find and start next move
  CPUMove move = currentSolver_->FindBestMove(nodes, edges);
  if (move.isValid()) {
    autoSolveCurrentMove_ = move;
    autoSolveAnimating_ = true;
    autoSolveAnimProgress_ = 0.0f;
    cpuLastMoveTime_ = std::chrono::steady_clock::now();

    std::cout << "[AutoSolve] Move: Node " << move.node_id
              << " | Reduction: " << move.intersection_reduction << std::endl;
  } else {
    // Stuck - can't solve further
    autoSolveActive_ = false;
    std::cout << "[AutoSolve] Stuck in local minimum." << std::endl;
  }
}

// UI Helpers
void GameEngine::DrawTextCentered(int x, int y, const std::string &text,
                                  SDL_Color color, int fontSize) {
  TTF_Font *font = (fontSize > 40) ? titleFont : uiFont;
  if (!font)
    return;

  SDL_Surface *surf = TTF_RenderText_Blended(font, text.c_str(), color);
  if (surf) {
    SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_Rect dst = {x - surf->w / 2, y - surf->h / 2, surf->w, surf->h};
    SDL_RenderCopy(renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
  }
}

void GameEngine::DrawButton(const SDL_Rect &rect, const std::string &text,
                            bool isSelected, bool isHovered) {
  SDL_Color bg;
  if (isSelected) {
    bg = {46, 204, 113, 255}; // Green
  } else if (isHovered) {
    bg = {52, 73, 94, 255}; // Dark Blue Hover
  } else {
    bg = {44, 62, 80, 255}; // Dark Blue
  }

  SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
  SDL_RenderFillRect(renderer, &rect);

  SDL_SetRenderDrawColor(renderer, 236, 240, 241, 255); // White border
  SDL_RenderDrawRect(renderer, &rect);

  DrawTextCentered(rect.x + rect.w / 2, rect.y + rect.h / 2, text,
                   {236, 240, 241, 255}, 24);
}

void GameEngine::DrawSlider(const SDL_Rect &rect, int min, int max, int &value,
                            const std::string &label) {
  (void)rect; (void)min; (void)max; (void)value; (void)label;
  // Not implemented, using buttons for now
}

void GameEngine::RenderHomeScreen() {
  int w = WINDOW_WIDTH;
  int cx = w / 2;

  int mx, my;
  SDL_GetMouseState(&mx, &my);
  SDL_Point mousePt = {mx, my};

  // Title
  DrawTextCentered(cx, 100, "GREEDY TANGLE", {231, 76, 60, 255}, 64);

  // Difficulty
  DrawTextCentered(cx, 200, "DIFFICULTY", {189, 195, 199, 255}, 24);
  SDL_Rect diffEasy = {cx - 190, 240, 120, 50};
  SDL_Rect diffMed = {cx - 60, 240, 120, 50};
  SDL_Rect diffHard = {cx + 70, 240, 120, 50};

  DrawButton(diffEasy, "EASY", currentDifficulty == Difficulty::EASY,
             SDL_PointInRect(&mousePt, &diffEasy));
  DrawButton(diffMed, "MEDIUM", currentDifficulty == Difficulty::MEDIUM,
             SDL_PointInRect(&mousePt, &diffMed));
  DrawButton(diffHard, "HARD", currentDifficulty == Difficulty::HARD,
             SDL_PointInRect(&mousePt, &diffHard));

  // Mode
  DrawTextCentered(cx, 330, "SOLVER MODE", {189, 195, 199, 255}, 24);
  SDL_Rect modeGreedy = {cx - 240, 370, 150, 50};
  SDL_Rect modeHybrid = {cx - 75, 370, 150, 50};
  SDL_Rect modeBacktrack = {cx + 90, 370, 150, 50};

  DrawButton(modeGreedy, "GREEDY", currentMode == GameMode::GREEDY,
             SDL_PointInRect(&mousePt, &modeGreedy));
  DrawButton(modeHybrid, "D&C & DP",
             currentMode == GameMode::DIVIDE_AND_CONQUER_DP,
             SDL_PointInRect(&mousePt, &modeHybrid));
  DrawButton(modeBacktrack, "BACKTRACK",
             currentMode == GameMode::BACKTRACKING,
             SDL_PointInRect(&mousePt, &modeBacktrack));

  // Nodes
  DrawTextCentered(cx, 460, "NODES", {189, 195, 199, 255}, 24);

  // Value background and text
  SDL_Rect valRect = {cx - 40, 490, 80, 50};
  SDL_SetRenderDrawColor(renderer, 44, 62, 80, 255);
  SDL_RenderFillRect(renderer, &valRect);
  DrawTextCentered(cx, 515, std::to_string(currentNodeCount),
                   {255, 255, 255, 255}, 32);

  SDL_Rect nodeSub = {cx - 100, 490, 50, 50};
  SDL_Rect nodeAdd = {cx + 50, 490, 50, 50};
  DrawButton(nodeSub, "-", false, SDL_PointInRect(&mousePt, &nodeSub));
  DrawButton(nodeAdd, "+", false, SDL_PointInRect(&mousePt, &nodeAdd));

  // Start
  SDL_Rect startBtn = {cx - 125, 600, 250, 70};
  bool hoverStart = SDL_PointInRect(&mousePt, &startBtn);
  DrawButton(startBtn, "START GAME", false, hoverStart);
}

void GameEngine::HandleHomeScreenInput(const SDL_Event &event) {
  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int mx = event.button.x;
    int my = event.button.y;
    SDL_Point pt = {mx, my};
    int cx = WINDOW_WIDTH / 2;

    // Difficulty logic
    SDL_Rect diffEasy = {cx - 190, 240, 120, 50};
    SDL_Rect diffMed = {cx - 60, 240, 120, 50};
    SDL_Rect diffHard = {cx + 70, 240, 120, 50};

    if (SDL_PointInRect(&pt, &diffEasy)) {
      SetDifficulty(Difficulty::EASY);
      return;
    }
    if (SDL_PointInRect(&pt, &diffMed)) {
      SetDifficulty(Difficulty::MEDIUM);
      return;
    }
    if (SDL_PointInRect(&pt, &diffHard)) {
      SetDifficulty(Difficulty::HARD);
      return;
    }

    // Mode logic
    SDL_Rect modeGreedy = {cx - 240, 370, 150, 50};
    SDL_Rect modeHybrid = {cx - 75, 370, 150, 50};
    SDL_Rect modeBacktrack = {cx + 90, 370, 150, 50};

    if (SDL_PointInRect(&pt, &modeGreedy)) {
      SetGameMode(GameMode::GREEDY);
      return;
    }
    if (SDL_PointInRect(&pt, &modeHybrid)) {
      SetGameMode(GameMode::DIVIDE_AND_CONQUER_DP);
      return;
    }
    if (SDL_PointInRect(&pt, &modeBacktrack)) {
      SetGameMode(GameMode::BACKTRACKING);
      return;
    }

    // Node logic
    SDL_Rect nodeSub = {cx - 100, 490, 50, 50};
    SDL_Rect nodeAdd = {cx + 50, 490, 50, 50};
    if (SDL_PointInRect(&pt, &nodeSub)) {
      SetNodeCount(std::max(3, currentNodeCount - 1));
      return;
    }
    if (SDL_PointInRect(&pt, &nodeAdd)) {
      SetNodeCount(std::min(100, currentNodeCount + 1));
      return;
    }

    // Start logic
    SDL_Rect startBtn = {cx - 125, 600, 250, 70};
    if (SDL_PointInRect(&pt, &startBtn)) {
      currentPhase = GamePhase::SHOWING_UNTANGLED;
      
      // Initialize full game cycle
      intersectionCount = 0;
      
      // Ensure clean slate and generate graph based on current settings
      StartNewGame();
      
      phaseStartTime = std::chrono::steady_clock::now();
    }
  }
}

// ============== ALGORITHM DESCRIPTION PANEL (Feature 6) ==============

void GameEngine::RenderAlgorithmPanel() {
  if (currentPhase != GamePhase::PLAYING)
    return;

  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);

  // Panel dimensions and position (right side of screen)
  int panelW = 220;
  int panelH = 200;
  int panelX = winW - panelW - 10;
  int panelY = MenuBar::BAR_HEIGHT + 10;

  // Semi-transparent background
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
  SDL_Rect panel = {panelX, panelY, panelW, panelH};
  SDL_RenderFillRect(renderer, &panel);

  // Border color based on solver
  SDL_Color borderColor;
  std::string solverName;
  std::string strategyLine1;
  std::string strategyLine2;
  std::string complexityLine;

  switch (currentMode) {
  case GameMode::GREEDY:
    borderColor = {50, 205, 50, 255}; // Green
    solverName = "Greedy Solver";
    strategyLine1 = "Picks the move that";
    strategyLine2 = "maximally reduces crossings.";
    complexityLine = "O(N * C) per step";
    break;
  case GameMode::BACKTRACKING:
    borderColor = {255, 165, 0, 255}; // Orange
    solverName = "Backtracking Solver";
    strategyLine1 = "Explores move sequences";
    strategyLine2 = "up to depth 3, backtracks.";
    complexityLine = "Exponential worst case";
    break;
  case GameMode::DIVIDE_AND_CONQUER_DP:
    borderColor = {100, 149, 237, 255}; // Cornflower blue
    solverName = "D&C + DP Solver";
    strategyLine1 = "Spatially partitions graph,";
    strategyLine2 = "solves sub-regions with DP.";
    complexityLine = "O(P * K^2) per partition";
    break;
  }

  SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b,
                         borderColor.a);
  SDL_RenderDrawRect(renderer, &panel);

  if (!menuBar)
    return;

  // Title bar with solver name
  SDL_Rect titleBar = {panelX + 1, panelY + 1, panelW - 2, 28};
  SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b,
                         60);
  SDL_RenderFillRect(renderer, &titleBar);
  menuBar->RenderTextCentered(solverName, titleBar, {255, 255, 255, 255});

  // "Algorithm" label
  int textY = panelY + 38;
  SDL_Rect labelRect = {panelX + 10, textY, panelW - 20, 18};
  menuBar->RenderTextCentered("Strategy", labelRect, {180, 180, 185, 255});

  // Strategy line 1
  textY += 22;
  SDL_Rect strat1Rect = {panelX + 8, textY, panelW - 16, 16};
  menuBar->RenderTextCentered(strategyLine1, strat1Rect, {200, 200, 210, 255});

  // Strategy line 2
  textY += 18;
  SDL_Rect strat2Rect = {panelX + 8, textY, panelW - 16, 16};
  menuBar->RenderTextCentered(strategyLine2, strat2Rect, {200, 200, 210, 255});

  // Separator line
  textY += 24;
  SDL_SetRenderDrawColor(renderer, 72, 72, 74, 255);
  SDL_RenderDrawLine(renderer, panelX + 15, textY, panelX + panelW - 15,
                     textY);

  // Complexity label
  textY += 8;
  SDL_Rect compLabel = {panelX + 10, textY, panelW - 20, 18};
  menuBar->RenderTextCentered("Complexity", compLabel, {180, 180, 185, 255});

  // Complexity value
  textY += 22;
  SDL_Rect compRect = {panelX + 8, textY, panelW - 16, 18};
  menuBar->RenderTextCentered(complexityLine, compRect, borderColor);

  // Live stats separator
  textY += 26;
  SDL_SetRenderDrawColor(renderer, 72, 72, 74, 255);
  SDL_RenderDrawLine(renderer, panelX + 15, textY, panelX + panelW - 15,
                     textY);

  // Show candidates evaluated if solver has data
  textY += 8;
  if (currentSolver_) {
    int candidates = currentSolver_->GetLastCandidatesEvaluated();
    std::string candStr = "Last eval: " + std::to_string(candidates) + " pos";
    SDL_Rect candRect = {panelX + 8, textY, panelW - 16, 16};
    menuBar->RenderTextCentered(candStr, candRect, {150, 150, 155, 255});
  }
}

// ============== DECISION HEATMAP (Feature 5) ==============

void GameEngine::ToggleHeatmap() {
  heatmapEnabled_ = !heatmapEnabled_;
  if (heatmapEnabled_) {
    heatmapLastUpdate_ = std::chrono::steady_clock::time_point{}; // Force recalc
    std::cout << "[Heatmap] Enabled" << std::endl;
  } else {
    nodeHeatmapScores_.clear();
    std::cout << "[Heatmap] Disabled" << std::endl;
  }

  // Update menu checkbox (Game menu index 0, heatmap item index 4)
  if (menuBar) {
    menuBar->SetItemChecked(0, 4, heatmapEnabled_);
  }
}

SDL_Color GameEngine::GetHeatmapColor(float score) const {
  // Clamp to [0, 1]
  if (score < 0.0f) score = 0.0f;
  if (score > 1.0f) score = 1.0f;

  // Color gradient: dim gray (0.0) → yellow (0.5) → red (1.0)
  uint8_t r, g, b;
  if (score < 0.5f) {
    // Gray to yellow: (80,80,80) → (255,220,50)
    float t = score * 2.0f;
    r = static_cast<uint8_t>(80 + t * (255 - 80));
    g = static_cast<uint8_t>(80 + t * (220 - 80));
    b = static_cast<uint8_t>(80 + t * (50 - 80));
  } else {
    // Yellow to red: (255,220,50) → (255,50,30)
    float t = (score - 0.5f) * 2.0f;
    r = 255;
    g = static_cast<uint8_t>(220 + t * (50 - 220));
    b = static_cast<uint8_t>(50 + t * (30 - 50));
  }

  return {r, g, b, 255};
}

void GameEngine::CalculateHeatmap() {
  size_t n = nodes.size();
  nodeHeatmapScores_.assign(n, 0.0f);

  if (intersectionCount == 0 || n == 0)
    return;

  // Coarse grid for fast evaluation
  const float gridSpacing = 120.0f;
  const float margin = 60.0f;
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);

  std::vector<int> bestReduction(n, 0);
  int globalMaxReduction = 0;

  for (size_t i = 0; i < n; ++i) {
    Vec2 originalPos = nodes[i].position;
    int currentCount = intersectionCount;
    int nodeBest = 0;

    // Test coarse grid positions
    for (float x = margin; x <= winW - margin; x += gridSpacing) {
      for (float y = margin; y <= winH - margin; y += gridSpacing) {
        nodes[i].position = Vec2(x, y);
        int newCount = CountIntersections(nodes, edges);
        int reduction = currentCount - newCount;
        if (reduction > nodeBest) {
          nodeBest = reduction;
        }
      }
    }

    // Also test centroid of neighbors
    if (!nodes[i].adjacencyList.empty()) {
      Vec2 centroid(0, 0);
      for (int neighbor : nodes[i].adjacencyList) {
        if (neighbor >= 0 && neighbor < static_cast<int>(n)) {
          centroid = centroid + nodes[neighbor].position;
        }
      }
      centroid =
          centroid * (1.0f / static_cast<float>(nodes[i].adjacencyList.size()));
      nodes[i].position = centroid;
      int newCount = CountIntersections(nodes, edges);
      int reduction = currentCount - newCount;
      if (reduction > nodeBest) {
        nodeBest = reduction;
      }
    }

    // Restore original position
    nodes[i].position = originalPos;
    bestReduction[i] = nodeBest;
    if (nodeBest > globalMaxReduction) {
      globalMaxReduction = nodeBest;
    }
  }

  // Normalize scores to [0, 1]
  if (globalMaxReduction > 0) {
    for (size_t i = 0; i < n; ++i) {
      nodeHeatmapScores_[i] =
          static_cast<float>(bestReduction[i]) / globalMaxReduction;
    }
  }
}

void GameEngine::RenderHeatmapLegend() {
  if (!heatmapEnabled_ || currentPhase != GamePhase::PLAYING)
    return;

  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);

  // Legend position: bottom-left corner
  int legendW = 160;
  int legendH = 70;
  int legendX = 10;
  int legendY = winH - legendH - 60; // Above scoreboard area

  // Background
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 25, 25, 30, 200);
  SDL_Rect bg = {legendX, legendY, legendW, legendH};
  SDL_RenderFillRect(renderer, &bg);

  SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
  SDL_RenderDrawRect(renderer, &bg);

  // Title
  if (menuBar) {
    SDL_Rect titleRect = {legendX + 5, legendY + 4, legendW - 10, 16};
    menuBar->RenderTextCentered("Impact Heatmap", titleRect,
                                {200, 200, 210, 255});
  }

  // Draw gradient bar
  int barX = legendX + 12;
  int barY = legendY + 24;
  int barW = legendW - 24;
  int barH = 14;

  for (int px = 0; px < barW; ++px) {
    float score = static_cast<float>(px) / barW;
    SDL_Color c = GetHeatmapColor(score);
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 255);
    SDL_RenderDrawLine(renderer, barX + px, barY, barX + px, barY + barH);
  }

  // Border around gradient
  SDL_SetRenderDrawColor(renderer, 150, 150, 155, 255);
  SDL_Rect barRect = {barX, barY, barW, barH};
  SDL_RenderDrawRect(renderer, &barRect);

  // Labels
  if (menuBar) {
    SDL_Rect lowRect = {legendX + 5, legendY + 44, 50, 16};
    menuBar->RenderTextCentered("Low", lowRect, {120, 120, 125, 255});

    SDL_Rect highRect = {legendX + legendW - 55, legendY + 44, 50, 16};
    menuBar->RenderTextCentered("High", highRect, {255, 80, 60, 255});

    SDL_Rect hintRect = {legendX + 50, legendY + 44, 60, 16};
    menuBar->RenderTextCentered("[H]", hintRect, {100, 100, 105, 255});
  }
}

// ============== STEP-BY-STEP REPLAY VIEWER (Feature 4) ==============

void GameEngine::StartReplayViewer() {
  if (!cpuReplayLogger_ || cpuReplayLogger_->GetTotalMoves() == 0) {
    std::cout << "[Replay] No replay data available. Play a game first."
              << std::endl;
    return;
  }

  // Wait for any in-flight CPU task
  if (cpuSolving_ && cpuFuture_.valid()) {
    cpuFuture_.wait();
    cpuSolving_ = false;
  }

  currentPhase = GamePhase::REPLAY_VIEWER;
  replayCurrentStep_ = 0;
  replayPlaying_ = false;

  // Build initial graph state from replay logger
  const auto &initialPositions = cpuReplayLogger_->GetInitialPositions();
  replayNodes_.resize(initialPositions.size());
  for (size_t i = 0; i < initialPositions.size(); ++i) {
    replayNodes_[i] = Node(static_cast<int>(i), initialPositions[i]);
  }

  std::cout << "[Replay] Entering replay viewer. Total moves: "
            << cpuReplayLogger_->GetTotalMoves() << std::endl;
}

void GameEngine::ReplayGoToStep(int step) {
  if (!cpuReplayLogger_)
    return;

  int totalMoves = cpuReplayLogger_->GetTotalMoves();
  if (step < 0)
    step = 0;
  if (step > totalMoves)
    step = totalMoves;

  // Rebuild graph state from initial positions up to the given step
  const auto &initialPositions = cpuReplayLogger_->GetInitialPositions();
  replayNodes_.resize(initialPositions.size());
  for (size_t i = 0; i < initialPositions.size(); ++i) {
    replayNodes_[i] = Node(static_cast<int>(i), initialPositions[i]);
    replayNodes_[i].adjacencyList = nodes.empty()
                                        ? std::vector<int>{}
                                        : cpuNodes_[i].adjacencyList;
  }

  // Apply moves 1..step
  for (int s = 1; s <= step; ++s) {
    const CPUMove &move = cpuReplayLogger_->GetMoveAt(s);
    if (move.isValid() &&
        move.node_id < static_cast<int>(replayNodes_.size())) {
      replayNodes_[move.node_id].position = move.to_position;
    }
  }

  replayCurrentStep_ = step;
}

void GameEngine::HandleReplayInput(const SDL_Event &event) {
  if (event.type == SDL_KEYDOWN) {
    switch (event.key.keysym.sym) {
    case SDLK_ESCAPE:
      // Return to main menu
      currentPhase = GamePhase::MAIN_MENU;
      return;
    case SDLK_RIGHT:
      if (cpuReplayLogger_ &&
          replayCurrentStep_ < cpuReplayLogger_->GetTotalMoves()) {
        ReplayGoToStep(replayCurrentStep_ + 1);
      }
      return;
    case SDLK_LEFT:
      if (replayCurrentStep_ > 0) {
        ReplayGoToStep(replayCurrentStep_ - 1);
      }
      return;
    case SDLK_SPACE:
      replayPlaying_ = !replayPlaying_;
      replayLastStepTime_ = std::chrono::steady_clock::now();
      return;
    }
  }

  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int mx = event.button.x;
    int my = event.button.y;
    SDL_Point pt = {mx, my};

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    // Control buttons at the bottom
    int btnW = 90;
    int btnH = 35;
    int btnY = winH - 60;
    int cx = winW / 2;

    // Back button
    SDL_Rect backBtn = {cx - btnW * 2 - 15, btnY, btnW, btnH};
    if (SDL_PointInRect(&pt, &backBtn) && replayCurrentStep_ > 0) {
      ReplayGoToStep(replayCurrentStep_ - 1);
      replayPlaying_ = false;
      return;
    }

    // Play/Pause button
    SDL_Rect playBtn = {cx - btnW / 2, btnY, btnW, btnH};
    if (SDL_PointInRect(&pt, &playBtn)) {
      replayPlaying_ = !replayPlaying_;
      replayLastStepTime_ = std::chrono::steady_clock::now();
      return;
    }

    // Next button
    SDL_Rect nextBtn = {cx + btnW + 15, btnY, btnW, btnH};
    if (SDL_PointInRect(&pt, &nextBtn) && cpuReplayLogger_ &&
        replayCurrentStep_ < cpuReplayLogger_->GetTotalMoves()) {
      ReplayGoToStep(replayCurrentStep_ + 1);
      replayPlaying_ = false;
      return;
    }

    // Exit button
    SDL_Rect exitBtn = {winW - 110, btnY, 100, btnH};
    if (SDL_PointInRect(&pt, &exitBtn)) {
      currentPhase = GamePhase::MAIN_MENU;
      return;
    }
  }
}

void GameEngine::RenderReplayViewer() {
  if (!cpuReplayLogger_)
    return;

  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);
  int totalMoves = cpuReplayLogger_->GetTotalMoves();

  // Draw edges using replay node positions
  for (const Edge &edge : edges) {
    if (edge.u_id < 0 ||
        edge.u_id >= static_cast<int>(replayNodes_.size()) ||
        edge.v_id < 0 || edge.v_id >= static_cast<int>(replayNodes_.size()))
      continue;

    const Vec2 &p1 = replayNodes_[edge.u_id].position;
    const Vec2 &p2 = replayNodes_[edge.v_id].position;

    // Check intersection for this edge
    bool isIntersecting = false;
    for (const Edge &other : edges) {
      if (edge.sharesVertex(other))
        continue;
      if (other.u_id < 0 ||
          other.u_id >= static_cast<int>(replayNodes_.size()) ||
          other.v_id < 0 ||
          other.v_id >= static_cast<int>(replayNodes_.size()))
        continue;
      const Vec2 &p3 = replayNodes_[other.u_id].position;
      const Vec2 &p4 = replayNodes_[other.v_id].position;
      if (CheckIntersection(p1, p2, p3, p4)) {
        isIntersecting = true;
        break;
      }
    }

    SDL_Color color = isIntersecting ? Colors::EDGE_CRITICAL : Colors::EDGE_SAFE;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLine(renderer, static_cast<int>(p1.x),
                       static_cast<int>(p1.y), static_cast<int>(p2.x),
                       static_cast<int>(p2.y));
  }

  // Draw nodes - highlight the moved node at current step
  int movedNodeId = -1;
  if (replayCurrentStep_ > 0 && replayCurrentStep_ <= totalMoves) {
    movedNodeId = cpuReplayLogger_->GetMoveAt(replayCurrentStep_).node_id;
  }

  for (const Node &node : replayNodes_) {
    int cx = static_cast<int>(node.position.x);
    int cy = static_cast<int>(node.position.y);
    int r = static_cast<int>(node.radius);

    SDL_Color fillColor;
    if (node.id == movedNodeId) {
      fillColor = {255, 200, 50, 255}; // Gold for the moved node
    } else {
      fillColor = Colors::NODE_FILL;
    }

    SDL_SetRenderDrawColor(renderer, fillColor.r, fillColor.g, fillColor.b,
                           fillColor.a);
    DrawFilledCircle(cx, cy, r);

    // Border
    SDL_Color borderColor =
        (node.id == movedNodeId) ? SDL_Color{255, 255, 255, 255}
                                 : Colors::NODE_BORDER;
    SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g,
                           borderColor.b, borderColor.a);
    int bx = r, by = 0;
    int radiusError = 1 - bx;
    while (bx >= by) {
      SDL_RenderDrawPoint(renderer, cx + bx, cy + by);
      SDL_RenderDrawPoint(renderer, cx + by, cy + bx);
      SDL_RenderDrawPoint(renderer, cx - by, cy + bx);
      SDL_RenderDrawPoint(renderer, cx - bx, cy + by);
      SDL_RenderDrawPoint(renderer, cx - bx, cy - by);
      SDL_RenderDrawPoint(renderer, cx - by, cy - bx);
      SDL_RenderDrawPoint(renderer, cx + by, cy - bx);
      SDL_RenderDrawPoint(renderer, cx + bx, cy - by);
      ++by;
      if (radiusError < 0) {
        radiusError += 2 * by + 1;
      } else {
        --bx;
        radiusError += 2 * (by - bx + 1);
      }
    }
  }

  // === Move Annotation Panel (right side) ===
  int panelW = 260;
  int panelH = 180;
  int panelX = winW - panelW - 10;
  int panelY = MenuBar::BAR_HEIGHT + 10;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 25, 25, 30, 220);
  SDL_Rect panel = {panelX, panelY, panelW, panelH};
  SDL_RenderFillRect(renderer, &panel);

  SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
  SDL_RenderDrawRect(renderer, &panel);

  if (menuBar) {
    // Title
    SDL_Rect titleRect = {panelX + 1, panelY + 1, panelW - 2, 24};
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 60);
    SDL_RenderFillRect(renderer, &titleRect);

    std::string titleText = "Step " + std::to_string(replayCurrentStep_) +
                            " / " + std::to_string(totalMoves);
    menuBar->RenderTextCentered(titleText, titleRect, {255, 255, 255, 255});

    int textY = panelY + 32;

    if (replayCurrentStep_ == 0) {
      // Initial state
      SDL_Rect initRect = {panelX + 10, textY, panelW - 20, 18};
      menuBar->RenderTextCentered("Initial State", initRect,
                                  {180, 180, 185, 255});
      textY += 24;

      std::string intStr = "Intersections: " +
                           std::to_string(cpuReplayLogger_->GetInitialIntersections());
      SDL_Rect intRect = {panelX + 10, textY, panelW - 20, 18};
      menuBar->RenderTextCentered(intStr, intRect, {200, 200, 210, 255});
    } else {
      const CPUMove &move = cpuReplayLogger_->GetMoveAt(replayCurrentStep_);

      // Node info
      std::ostringstream nodeStr;
      nodeStr << "Node " << move.node_id;
      SDL_Rect nodeRect = {panelX + 10, textY, panelW - 20, 18};
      menuBar->RenderTextCentered(nodeStr.str(), nodeRect,
                                  {255, 200, 50, 255});
      textY += 22;

      // Position change
      std::ostringstream posStr;
      posStr << std::fixed << std::setprecision(0) << "(" << move.from_position.x
             << "," << move.from_position.y << ") -> ("
             << move.to_position.x << "," << move.to_position.y << ")";
      SDL_Rect posRect = {panelX + 5, textY, panelW - 10, 16};
      menuBar->RenderTextCentered(posStr.str(), posRect,
                                  {200, 200, 210, 255});
      textY += 22;

      // Intersection change
      std::ostringstream intStr;
      intStr << "Intersections: " << move.intersections_before << " -> "
             << move.intersections_after << " (-"
             << move.intersection_reduction << ")";
      SDL_Rect intRect = {panelX + 5, textY, panelW - 10, 16};
      SDL_Color intColor = move.intersection_reduction > 0
                               ? SDL_Color{50, 205, 50, 255}
                               : SDL_Color{200, 200, 210, 255};
      menuBar->RenderTextCentered(intStr.str(), intRect, intColor);
      textY += 22;

      // Computation time
      std::string timeStr =
          "Time: " + std::to_string(move.computation_time_ms) + "ms";
      SDL_Rect timeRect = {panelX + 10, textY, panelW - 20, 16};
      menuBar->RenderTextCentered(timeStr, timeRect, {150, 150, 155, 255});
    }
  }

  // === Progress bar ===
  int barX = 20;
  int barY = winH - 100;
  int barW = winW - 40;
  int barH = 8;

  SDL_SetRenderDrawColor(renderer, 50, 50, 55, 255);
  SDL_Rect barBg = {barX, barY, barW, barH};
  SDL_RenderFillRect(renderer, &barBg);

  if (totalMoves > 0) {
    int fillW = static_cast<int>(
        barW * (static_cast<float>(replayCurrentStep_) / totalMoves));
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_Rect barFill = {barX, barY, fillW, barH};
    SDL_RenderFillRect(renderer, &barFill);
  }

  SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
  SDL_RenderDrawRect(renderer, &barBg);

  // === Control buttons ===
  int btnW = 90;
  int btnH = 35;
  int btnY = winH - 60;
  int centerX = winW / 2;

  // Back button
  SDL_Rect backBtn = {centerX - btnW * 2 - 15, btnY, btnW, btnH};
  bool canGoBack = replayCurrentStep_ > 0;
  SDL_Color backBg = canGoBack ? SDL_Color{44, 62, 80, 255}
                               : SDL_Color{30, 30, 35, 255};
  SDL_SetRenderDrawColor(renderer, backBg.r, backBg.g, backBg.b, backBg.a);
  SDL_RenderFillRect(renderer, &backBtn);
  SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
  SDL_RenderDrawRect(renderer, &backBtn);
  if (menuBar) {
    SDL_Color textCol = canGoBack ? SDL_Color{255, 255, 255, 255}
                                  : SDL_Color{80, 80, 85, 255};
    menuBar->RenderTextCentered("<< Back", backBtn, textCol);
  }

  // Play/Pause button
  SDL_Rect playBtn = {centerX - btnW / 2, btnY, btnW, btnH};
  SDL_Color playBg =
      replayPlaying_ ? SDL_Color{180, 40, 40, 255} : SDL_Color{40, 160, 80, 255};
  SDL_SetRenderDrawColor(renderer, playBg.r, playBg.g, playBg.b, playBg.a);
  SDL_RenderFillRect(renderer, &playBtn);
  SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
  SDL_RenderDrawRect(renderer, &playBtn);
  if (menuBar) {
    std::string playText = replayPlaying_ ? "Pause" : "Play";
    menuBar->RenderTextCentered(playText, playBtn, {255, 255, 255, 255});
  }

  // Next button
  SDL_Rect nextBtn = {centerX + btnW + 15, btnY, btnW, btnH};
  bool canGoNext = cpuReplayLogger_ &&
                   replayCurrentStep_ < cpuReplayLogger_->GetTotalMoves();
  SDL_Color nextBg = canGoNext ? SDL_Color{44, 62, 80, 255}
                               : SDL_Color{30, 30, 35, 255};
  SDL_SetRenderDrawColor(renderer, nextBg.r, nextBg.g, nextBg.b, nextBg.a);
  SDL_RenderFillRect(renderer, &nextBtn);
  SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
  SDL_RenderDrawRect(renderer, &nextBtn);
  if (menuBar) {
    SDL_Color textCol = canGoNext ? SDL_Color{255, 255, 255, 255}
                                  : SDL_Color{80, 80, 85, 255};
    menuBar->RenderTextCentered("Next >>", nextBtn, textCol);
  }

  // Exit button
  SDL_Rect exitBtn = {winW - 110, btnY, 100, btnH};
  SDL_SetRenderDrawColor(renderer, 180, 40, 40, 255);
  SDL_RenderFillRect(renderer, &exitBtn);
  SDL_SetRenderDrawColor(renderer, 220, 80, 80, 255);
  SDL_RenderDrawRect(renderer, &exitBtn);
  if (menuBar) {
    menuBar->RenderTextCentered("Exit", exitBtn, {255, 255, 255, 255});
  }

  // Keyboard hint
  if (menuBar) {
    SDL_Rect hintRect = {10, btnY + 5, 200, 20};
    menuBar->RenderTextCentered("Arrow keys / Space", hintRect,
                                {100, 100, 105, 255});
  }
}

// ============== ALGORITHM COMPARISON / BENCHMARK MODE (Feature 1) ==============

void GameEngine::RunBenchmark() {
  // Wait for any in-flight CPU task
  if (cpuSolving_ && cpuFuture_.valid()) {
    cpuFuture_.wait();
    cpuSolving_ = false;
  }

  // If we're at main menu or have no graph, generate a fresh one
  if (nodes.empty() || currentPhase == GamePhase::MAIN_MENU ||
      currentPhase == GamePhase::BENCHMARK_RESULTS) {
    ClearGraph();
    for (int i = 0; i < currentNodeCount; ++i) {
      AddNode(Vec2(0, 0));
    }
    for (int i = 0; i < currentNodeCount; ++i) {
      AddEdge(i, (i + 1) % currentNodeCount);
    }
    // Add chord edges for complexity
    std::mt19937 rng(42);
    int chords = std::max(1, currentNodeCount / 3);
    for (int c = 0; c < chords; ++c) {
      int a = rng() % currentNodeCount;
      int b = rng() % currentNodeCount;
      if (a != b && std::abs(a - b) > 1) {
        AddEdge(a, b);
      }
    }
    GeneratePlanarLayout();
    ApplyCircleScramble();
    for (size_t i = 0; i < nodes.size(); ++i) {
      nodes[i].position = targetPositions[i];
    }
  }

  // Snapshot the current tangled graph (use cpuNodes_ if available, else nodes)
  std::vector<Node> snapshotNodes =
      cpuNodes_.empty() ? nodes : cpuNodes_;

  int initialIntersections = CountIntersections(snapshotNodes, edges);
  if (initialIntersections == 0) {
    std::cout << "[Benchmark] Graph has no intersections. Nothing to solve."
              << std::endl;
    return;
  }

  benchmarkResults_.clear();
  benchmarkShowPlot_ = false;
  std::cout << "[Benchmark] Starting comparison on " << snapshotNodes.size()
            << " nodes, " << edges.size() << " edges, "
            << initialIntersections << " intersections..." << std::endl;

  // Run each solver
  SolverMode modes[] = {SolverMode::GREEDY, SolverMode::BACKTRACKING,
                        SolverMode::DIVIDE_AND_CONQUER_DP};

  for (SolverMode mode : modes) {

    auto solver = CreateSolver(mode);
    BenchmarkResult result;
    result.solverName = solver->GetName();
    result.initialIntersections = initialIntersections;
    result.intersectionHistory.push_back(initialIntersections);

    // Clone graph for this solver
    std::vector<Node> solverNodes = snapshotNodes;
    int currentCount = initialIntersections;

    auto startTime = std::chrono::steady_clock::now();

    for (int moveNum = 0; moveNum < BENCHMARK_MAX_MOVES; ++moveNum) {
      // Check time limit
      auto now = std::chrono::steady_clock::now();
      float elapsed =
          std::chrono::duration<float>(now - startTime).count();
      if (elapsed >= BENCHMARK_MAX_TIME)
        break;

      if (currentCount == 0)
        break;

      CPUMove move = solver->FindBestMove(solverNodes, edges);
      result.totalCandidatesEvaluated += solver->GetLastCandidatesEvaluated();

      if (!move.isValid() || move.intersection_reduction <= 0)
        break;

      // Apply move
      solverNodes[move.node_id].position = move.to_position;
      currentCount = CountIntersections(solverNodes, edges);
      ++result.totalMoves;
      result.intersectionHistory.push_back(currentCount);
    }

    auto endTime = std::chrono::steady_clock::now();
    result.totalTimeMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                              startTime)
            .count();
    result.finalIntersections = currentCount;
    result.solved = (currentCount == 0);

    std::cout << "[Benchmark] " << result.solverName << ": "
              << result.totalMoves << " moves, " << result.totalTimeMs
              << "ms, final=" << result.finalIntersections
              << (result.solved ? " (SOLVED)" : "") << std::endl;
    benchmarkResults_.push_back(result);
  }

  // Restore original graph
  nodes = snapshotNodes;

  std::cout << "[Benchmark] Complete. Showing results." << std::endl;
}

void GameEngine::HandleBenchmarkInput(const SDL_Event &event) {
  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
    if (benchmarkShowPlot_) {
      benchmarkShowPlot_ = false; // Go back to cards view
    } else {
      currentPhase = GamePhase::MAIN_MENU;
    }
    return;
  }

  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int mx = event.button.x;
    int my = event.button.y;
    SDL_Point pt = {mx, my};

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    // "Back to Menu" button (on both views)
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    if (SDL_PointInRect(&pt, &backBtn)) {
      if (benchmarkShowPlot_) {
        benchmarkShowPlot_ = false; // Go back to cards
      } else {
        currentPhase = GamePhase::MAIN_MENU;
      }
      return;
    }

    if (!benchmarkShowPlot_) {
      // Cards view buttons
      // "View Plot" button
      SDL_Rect plotBtn = {winW / 2 - 80, winH - 135, 160, 35};
      if (SDL_PointInRect(&pt, &plotBtn)) {
        benchmarkShowPlot_ = true;
        return;
      }

      // "Re-run" button
      SDL_Rect rerunBtn = {winW / 2 - 80, winH - 95, 160, 35};
      if (SDL_PointInRect(&pt, &rerunBtn)) {
        benchmarkShowPlot_ = false;
        RunBenchmark();
        return;
      }
    } else {
      // Plot view buttons
      // "Back to Cards" button (at back position)
      // Already handled by backBtn above
    }
  }
}

void GameEngine::RenderBenchmarkResults() {
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);
  int numSolvers = static_cast<int>(benchmarkResults_.size());
  if (numSolvers == 0)
    return;

  // If showing convergence plot, delegate to that renderer
  if (benchmarkShowPlot_) {
    RenderConvergencePlot();
    return;
  }

  // Title
  DrawTextCentered(winW / 2, 55, "ALGORITHM COMPARISON",
                   {231, 76, 60, 255}, 48);

  // Graph info subtitle
  std::string graphInfo =
      "Nodes: " +
      std::to_string(benchmarkResults_[0].initialIntersections > 0
                         ? static_cast<int>(nodes.size())
                         : 0) +
      "  |  Edges: " + std::to_string(edges.size()) +
      "  |  Initial Crossings: " +
      std::to_string(benchmarkResults_[0].initialIntersections);
  DrawTextCentered(winW / 2, 90, graphInfo, {180, 180, 185, 255}, 24);

  // Column layout for solver cards
  int cardW = 280;
  int cardH = 350;
  int totalW = numSolvers * cardW + (numSolvers - 1) * 20;
  int startX = (winW - totalW) / 2;
  int cardY = 120;

  // Solver colors
  SDL_Color solverColors[] = {
      {50, 205, 50, 255},   // Greedy: green
      {255, 165, 0, 255},   // Backtracking: orange
      {100, 149, 237, 255}  // D&C+DP: blue
  };

  // Find best values for highlighting
  int bestMoves = INT_MAX, bestTime = INT_MAX, bestFinal = INT_MAX;
  for (const auto &r : benchmarkResults_) {
    if (r.totalMoves > 0 && r.totalMoves < bestMoves)
      bestMoves = r.totalMoves;
    if (r.totalTimeMs < bestTime)
      bestTime = static_cast<int>(r.totalTimeMs);
    if (r.finalIntersections < bestFinal)
      bestFinal = r.finalIntersections;
  }

  for (int i = 0; i < numSolvers; ++i) {
    const BenchmarkResult &r = benchmarkResults_[i];
    int cx = startX + i * (cardW + 20);
    SDL_Color color = solverColors[i % 3];

    // Card background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 30, 30, 35, 230);
    SDL_Rect card = {cx, cardY, cardW, cardH};
    SDL_RenderFillRect(renderer, &card);

    // Card border
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &card);

    // Solver name header
    SDL_Rect header = {cx + 1, cardY + 1, cardW - 2, 36};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 80);
    SDL_RenderFillRect(renderer, &header);
    if (menuBar) {
      menuBar->RenderTextCentered(r.solverName, header, {255, 255, 255, 255});
    }

    // Solved badge
    int textY = cardY + 46;
    if (menuBar) {
      if (r.solved) {
        SDL_Rect badge = {cx + cardW / 2 - 40, textY, 80, 22};
        SDL_SetRenderDrawColor(renderer, 50, 205, 50, 255);
        SDL_RenderFillRect(renderer, &badge);
        menuBar->RenderTextCentered("SOLVED", badge, {20, 20, 25, 255});
      } else {
        SDL_Rect badge = {cx + cardW / 2 - 50, textY, 100, 22};
        SDL_SetRenderDrawColor(renderer, 180, 40, 40, 255);
        SDL_RenderFillRect(renderer, &badge);
        menuBar->RenderTextCentered("NOT SOLVED", badge, {255, 255, 255, 255});
      }
    }

    textY += 32;

    // Stats helper lambda
    auto drawStat = [&](int y, const std::string &label,
                        const std::string &value, bool isBest) {
      if (!menuBar)
        return;
      SDL_Rect labelRect = {cx + 10, y, cardW - 20, 18};
      menuBar->RenderTextCentered(label, labelRect, {150, 150, 155, 255});

      SDL_Color valColor =
          isBest ? SDL_Color{50, 255, 50, 255} : SDL_Color{220, 220, 225, 255};
      SDL_Rect valRect = {cx + 10, y + 20, cardW - 20, 22};
      menuBar->RenderTextCentered(value, valRect, valColor);
    };

    // Total Moves
    drawStat(textY, "Total Moves", std::to_string(r.totalMoves),
             r.totalMoves == bestMoves && r.totalMoves > 0);
    textY += 50;

    // Total Time
    std::string timeStr;
    if (r.totalTimeMs < 1000) {
      timeStr = std::to_string(r.totalTimeMs) + " ms";
    } else {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(2)
          << (r.totalTimeMs / 1000.0) << " s";
      timeStr = oss.str();
    }
    drawStat(textY, "Total Time", timeStr,
             static_cast<int>(r.totalTimeMs) == bestTime);
    textY += 50;

    // Final Intersections
    drawStat(textY, "Final Crossings",
             std::to_string(r.finalIntersections),
             r.finalIntersections == bestFinal);
    textY += 50;

    // Candidates Evaluated
    std::string candStr;
    if (r.totalCandidatesEvaluated > 1000) {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(1)
          << (r.totalCandidatesEvaluated / 1000.0) << "K";
      candStr = oss.str();
    } else {
      candStr = std::to_string(r.totalCandidatesEvaluated);
    }
    drawStat(textY, "Candidates Evaluated", candStr, false);
    textY += 50;

    // Avg time per move
    if (r.totalMoves > 0) {
      float avgMs =
          static_cast<float>(r.totalTimeMs) / r.totalMoves;
      std::ostringstream avgStr;
      avgStr << std::fixed << std::setprecision(1) << avgMs << " ms/move";
      drawStat(textY, "Avg Time/Move", avgStr.str(), false);
    } else {
      drawStat(textY, "Avg Time/Move", "N/A", false);
    }
  }

  // View Convergence Plot button
  {
    SDL_Rect plotBtn = {winW / 2 - 80, winH - 135, 160, 35};
    SDL_SetRenderDrawColor(renderer, 40, 120, 80, 255);
    SDL_RenderFillRect(renderer, &plotBtn);
    SDL_SetRenderDrawColor(renderer, 80, 200, 120, 255);
    SDL_RenderDrawRect(renderer, &plotBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("View Plot", plotBtn,
                                  {255, 255, 255, 255});
    }
  }

  // Re-run button
  {
    SDL_Rect rerunBtn = {winW / 2 - 80, winH - 95, 160, 35};
    SDL_SetRenderDrawColor(renderer, 44, 62, 80, 255);
    SDL_RenderFillRect(renderer, &rerunBtn);
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_RenderDrawRect(renderer, &rerunBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Re-run Benchmark", rerunBtn,
                                  {255, 255, 255, 255});
    }
  }

  // Back button
  {
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    SDL_SetRenderDrawColor(renderer, 180, 40, 40, 255);
    SDL_RenderFillRect(renderer, &backBtn);
    SDL_SetRenderDrawColor(renderer, 220, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &backBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Back to Menu", backBtn,
                                  {255, 255, 255, 255});
    }
  }
}

// ============== CONVERGENCE PLOT (Feature 2) ==============

void GameEngine::RenderConvergencePlot() {
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);
  int numSolvers = static_cast<int>(benchmarkResults_.size());
  if (numSolvers == 0)
    return;

  // Title
  DrawTextCentered(winW / 2, 45, "CONVERGENCE PLOT",
                   {231, 76, 60, 255}, 48);
  DrawTextCentered(winW / 2, 75, "Intersections vs. Move Number",
                   {180, 180, 185, 255}, 24);

  // Chart area
  int chartLeft = 80;
  int chartTop = 100;
  int chartRight = winW - 40;
  int chartBottom = winH - 120;
  int chartW = chartRight - chartLeft;
  int chartH = chartBottom - chartTop;

  // Chart background
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 30, 30, 35, 230);
  SDL_Rect chartBg = {chartLeft, chartTop, chartW, chartH};
  SDL_RenderFillRect(renderer, &chartBg);
  SDL_SetRenderDrawColor(renderer, 60, 60, 65, 255);
  SDL_RenderDrawRect(renderer, &chartBg);

  // Determine data ranges
  int maxMoves = 0;
  int maxIntersections = 0;
  for (const auto &r : benchmarkResults_) {
    int histSize = static_cast<int>(r.intersectionHistory.size());
    if (histSize > maxMoves)
      maxMoves = histSize;
    for (int val : r.intersectionHistory) {
      if (val > maxIntersections)
        maxIntersections = val;
    }
  }

  if (maxMoves <= 1 || maxIntersections == 0) {
    DrawTextCentered(winW / 2, winH / 2, "No data to plot",
                     {180, 180, 185, 255}, 24);
    // Back button
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    SDL_SetRenderDrawColor(renderer, 180, 40, 40, 255);
    SDL_RenderFillRect(renderer, &backBtn);
    SDL_SetRenderDrawColor(renderer, 220, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &backBtn);
    if (menuBar)
      menuBar->RenderTextCentered("Back", backBtn, {255, 255, 255, 255});
    return;
  }

  // Draw grid lines
  int numGridY = 5;
  for (int i = 0; i <= numGridY; ++i) {
    int y = chartTop + (chartH * i) / numGridY;
    SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
    SDL_RenderDrawLine(renderer, chartLeft, y, chartRight, y);

    // Y-axis label
    int val = maxIntersections - (maxIntersections * i) / numGridY;
    if (menuBar) {
      std::string label = std::to_string(val);
      SDL_Rect labelRect = {chartLeft - 50, y - 8, 45, 16};
      menuBar->RenderTextCentered(label, labelRect, {150, 150, 155, 255});
    }
  }

  int numGridX = std::min(maxMoves - 1, 10);
  if (numGridX > 0) {
    for (int i = 0; i <= numGridX; ++i) {
      int x = chartLeft + (chartW * i) / numGridX;
      SDL_SetRenderDrawColor(renderer, 45, 45, 50, 255);
      SDL_RenderDrawLine(renderer, x, chartTop, x, chartBottom);

      // X-axis label
      int val = ((maxMoves - 1) * i) / numGridX;
      if (menuBar) {
        std::string label = std::to_string(val);
        SDL_Rect labelRect = {x - 15, chartBottom + 4, 30, 16};
        menuBar->RenderTextCentered(label, labelRect, {150, 150, 155, 255});
      }
    }
  }

  // Axis labels
  if (menuBar) {
    SDL_Rect yLabel = {5, chartTop + chartH / 2 - 8, 60, 16};
    menuBar->RenderTextCentered("Crossings", yLabel, {180, 180, 185, 255});

    SDL_Rect xLabel = {chartLeft + chartW / 2 - 30, chartBottom + 22, 60, 16};
    menuBar->RenderTextCentered("Moves", xLabel, {180, 180, 185, 255});
  }

  // Solver colors
  SDL_Color solverColors[] = {
      {50, 205, 50, 255},   // Greedy: green
      {255, 165, 0, 255},   // Backtracking: orange
      {100, 149, 237, 255}  // D&C+DP: blue
  };

  // Draw lines for each solver
  for (int s = 0; s < numSolvers; ++s) {
    const auto &hist = benchmarkResults_[s].intersectionHistory;
    if (hist.size() < 2)
      continue;

    SDL_Color color = solverColors[s % 3];
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    for (size_t i = 1; i < hist.size(); ++i) {
      // Map data point to pixel coordinates
      float x1Frac = static_cast<float>(i - 1) / (maxMoves - 1);
      float y1Frac = 1.0f - static_cast<float>(hist[i - 1]) / maxIntersections;
      float x2Frac = static_cast<float>(i) / (maxMoves - 1);
      float y2Frac = 1.0f - static_cast<float>(hist[i]) / maxIntersections;

      int px1 = chartLeft + static_cast<int>(x1Frac * chartW);
      int py1 = chartTop + static_cast<int>(y1Frac * chartH);
      int px2 = chartLeft + static_cast<int>(x2Frac * chartW);
      int py2 = chartTop + static_cast<int>(y2Frac * chartH);

      // Draw thick line (3 parallel lines)
      SDL_RenderDrawLine(renderer, px1, py1, px2, py2);
      SDL_RenderDrawLine(renderer, px1, py1 - 1, px2, py2 - 1);
      SDL_RenderDrawLine(renderer, px1, py1 + 1, px2, py2 + 1);
    }

    // Draw data points as small circles
    for (size_t i = 0; i < hist.size(); ++i) {
      float xFrac = static_cast<float>(i) / (maxMoves - 1);
      float yFrac = 1.0f - static_cast<float>(hist[i]) / maxIntersections;
      int px = chartLeft + static_cast<int>(xFrac * chartW);
      int py = chartTop + static_cast<int>(yFrac * chartH);

      // Small filled circle (radius 3)
      for (int dy = -3; dy <= 3; ++dy) {
        int halfW = static_cast<int>(std::sqrt(9 - dy * dy));
        SDL_RenderDrawLine(renderer, px - halfW, py + dy, px + halfW, py + dy);
      }
    }
  }

  // Legend
  int legendX = chartRight - 200;
  int legendY = chartTop + 10;
  SDL_SetRenderDrawColor(renderer, 25, 25, 30, 220);
  SDL_Rect legendBg = {legendX, legendY, 190, 20 * numSolvers + 10};
  SDL_RenderFillRect(renderer, &legendBg);
  SDL_SetRenderDrawColor(renderer, 80, 80, 85, 255);
  SDL_RenderDrawRect(renderer, &legendBg);

  for (int s = 0; s < numSolvers; ++s) {
    SDL_Color color = solverColors[s % 3];
    int ly = legendY + 5 + s * 20;

    // Color swatch
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
    SDL_Rect swatch = {legendX + 8, ly + 2, 20, 12};
    SDL_RenderFillRect(renderer, &swatch);

    // Solver name
    if (menuBar) {
      SDL_Rect nameRect = {legendX + 35, ly, 145, 16};
      menuBar->RenderTextCentered(benchmarkResults_[s].solverName, nameRect,
                                  {220, 220, 225, 255});
    }
  }

  // Back to Cards button
  {
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    SDL_SetRenderDrawColor(renderer, 44, 62, 80, 255);
    SDL_RenderFillRect(renderer, &backBtn);
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_RenderDrawRect(renderer, &backBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Back to Cards", backBtn,
                                  {255, 255, 255, 255});
    }
  }
}

void GameEngine::RunScalabilityTest() {
  // Wait for any in-flight CPU task
  if (cpuSolving_ && cpuFuture_.valid()) {
    cpuFuture_.wait();
    cpuSolving_ = false;
  }

  scalabilityResults_.clear();
  std::cout << "[Complexity] Starting empirical complexity analysis..."
            << std::endl;

  SolverMode modes[] = {SolverMode::GREEDY, SolverMode::BACKTRACKING,
                        SolverMode::DIVIDE_AND_CONQUER_DP};

  // Save current graph state
  auto savedNodes = nodes;
  auto savedEdges = edges;

  for (int sizeIdx = 0; sizeIdx < SCALABILITY_NUM_SIZES; ++sizeIdx) {
    int N = SCALABILITY_SIZES[sizeIdx];
    std::cout << "[Complexity] Testing N=" << N << "..." << std::endl;

    // Generate a fresh graph of size N
    ClearGraph();
    for (int i = 0; i < N; ++i) {
      AddNode(Vec2(0, 0));
    }
    // Build cycle edges
    for (int i = 0; i < N; ++i) {
      AddEdge(i, (i + 1) % N);
    }
    // Add some chord edges for complexity
    std::mt19937 rng(42 + N); // Deterministic seed per size for fairness
    int chords = std::max(1, N / 3);
    for (int c = 0; c < chords; ++c) {
      int a = rng() % N;
      int b = rng() % N;
      if (a != b && std::abs(a - b) > 1 && !(a == 0 && b == N - 1) &&
          !(b == 0 && a == N - 1)) {
        // Check if edge already exists
        bool exists = false;
        for (const auto &e : edges) {
          if ((e.u_id == a && e.v_id == b) || (e.u_id == b && e.v_id == a)) {
            exists = true;
            break;
          }
        }
        if (!exists) {
          AddEdge(a, b);
        }
      }
    }
    GeneratePlanarLayout();
    ApplyCircleScramble();
    // Apply tangled positions
    for (size_t i = 0; i < nodes.size(); ++i) {
      nodes[i].position = targetPositions[i];
    }

    int initialIntersections = CountIntersections(nodes, edges);
    if (initialIntersections == 0) {
      // Graph isn't tangled enough, still record zero times
      for (SolverMode mode : modes) {
        auto solver = CreateSolver(mode);
        ScalabilityDataPoint dp;
        dp.solverName = solver->GetName();
        dp.nodeCount = N;
        dp.timeMs = 0;
        dp.moves = 0;
        dp.solved = true;
        scalabilityResults_.push_back(dp);
      }
      continue;
    }

    // Snapshot the tangled graph
    std::vector<Node> snapshotNodes = nodes;
    std::vector<Edge> snapshotEdges = edges;

    for (SolverMode mode : modes) {

      auto solver = CreateSolver(mode);
      ScalabilityDataPoint dp;
      dp.solverName = solver->GetName();
      dp.nodeCount = N;

      // Clone graph for this solver
      std::vector<Node> solverNodes = snapshotNodes;
      int currentCount = initialIntersections;

      auto startTime = std::chrono::steady_clock::now();

      for (int moveNum = 0; moveNum < SCALABILITY_MAX_MOVES; ++moveNum) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - startTime).count();
        if (elapsed >= SCALABILITY_MAX_TIME)
          break;
        if (currentCount == 0)
          break;

        CPUMove move = solver->FindBestMove(solverNodes, snapshotEdges);
        if (!move.isValid() || move.intersection_reduction <= 0)
          break;

        solverNodes[move.node_id].position = move.to_position;
        currentCount = CountIntersections(solverNodes, snapshotEdges);
        ++dp.moves;
      }

      auto endTime = std::chrono::steady_clock::now();
      dp.timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      endTime - startTime)
                      .count();
      dp.solved = (currentCount == 0);

      std::cout << "[Complexity] " << dp.solverName << " N=" << N << ": "
                << dp.moves << " moves, " << dp.timeMs << "ms"
                << (dp.solved ? " (SOLVED)" : "") << std::endl;

      scalabilityResults_.push_back(dp);
    }
  }

  // Restore original graph state
  nodes = savedNodes;
  edges = savedEdges;

  std::cout << "[Complexity] Complete. Showing results." << std::endl;
}

void GameEngine::HandleScalabilityInput(const SDL_Event &event) {
  if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
    currentPhase = GamePhase::MAIN_MENU;
    return;
  }

  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int mx = event.button.x;
    int my = event.button.y;
    SDL_Point pt = {mx, my};

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    // "Back to Menu" button
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    if (SDL_PointInRect(&pt, &backBtn)) {
      currentPhase = GamePhase::MAIN_MENU;
      return;
    }

    // "Re-run" button
    SDL_Rect rerunBtn = {winW / 2 - 80, winH - 95, 160, 35};
    if (SDL_PointInRect(&pt, &rerunBtn)) {
      RunScalabilityTest();
      return;
    }
  }
}

void GameEngine::RenderScalabilityResults() {
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);

  if (scalabilityResults_.empty())
    return;

  // Background
  SDL_SetRenderDrawColor(renderer, 20, 20, 25, 255);
  SDL_RenderClear(renderer);

  // Title
  if (menuBar) {
    SDL_Rect titleRect = {0, 30, winW, 40};
    menuBar->RenderTextCentered("Complexity Analysis: Runtime vs Graph Size",
                                titleRect, {255, 255, 255, 255});
  }

  // Chart area
  int chartLeft = 100;
  int chartRight = winW - 40;
  int chartTop = 80;
  int chartBottom = winH - 160;
  int chartWidth = chartRight - chartLeft;
  int chartHeight = chartBottom - chartTop;

  // Draw chart background
  SDL_Rect chartBg = {chartLeft - 5, chartTop - 5, chartWidth + 10,
                      chartHeight + 10};
  SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
  SDL_RenderFillRect(renderer, &chartBg);
  SDL_SetRenderDrawColor(renderer, 60, 60, 80, 255);
  SDL_RenderDrawRect(renderer, &chartBg);

  // Find max time for Y-axis scaling
  int64_t maxTime = 1;
  for (const auto &dp : scalabilityResults_) {
    maxTime = std::max(maxTime, dp.timeMs);
  }
  // Round up to nice number
  int64_t yMax = maxTime;
  if (yMax < 10) yMax = 10;
  else if (yMax < 100) yMax = ((yMax / 10) + 1) * 10;
  else if (yMax < 1000) yMax = ((yMax / 100) + 1) * 100;
  else yMax = ((yMax / 1000) + 1) * 1000;

  // Draw grid lines
  int numGridLines = 5;
  SDL_SetRenderDrawColor(renderer, 40, 40, 55, 255);
  for (int i = 0; i <= numGridLines; ++i) {
    int y = chartBottom - (i * chartHeight / numGridLines);
    SDL_RenderDrawLine(renderer, chartLeft, y, chartRight, y);

    // Y-axis label
    if (menuBar) {
      int64_t val = (yMax * i) / numGridLines;
      std::string label;
      if (yMax >= 1000) {
        label = std::to_string(val / 1000) + "." +
                std::to_string((val % 1000) / 100) + "s";
      } else {
        label = std::to_string(val) + "ms";
      }
      SDL_Rect labelRect = {chartLeft - 90, y - 8, 80, 16};
      menuBar->RenderTextCentered(label, labelRect, {150, 150, 170, 255});
    }
  }

  // Draw vertical grid lines for each N value
  for (int i = 0; i < SCALABILITY_NUM_SIZES; ++i) {
    int x = chartLeft + (i * chartWidth) / (SCALABILITY_NUM_SIZES - 1);
    SDL_SetRenderDrawColor(renderer, 40, 40, 55, 255);
    SDL_RenderDrawLine(renderer, x, chartTop, x, chartBottom);

    // X-axis label
    if (menuBar) {
      std::string label = "N=" + std::to_string(SCALABILITY_SIZES[i]);
      SDL_Rect labelRect = {x - 25, chartBottom + 5, 50, 16};
      menuBar->RenderTextCentered(label, labelRect, {150, 150, 170, 255});
    }
  }

  // Solver colors (same as convergence plot)
  SDL_Color solverColors[] = {
      {50, 205, 50, 255},   // Green - Greedy
      {255, 165, 0, 255},   // Orange - Backtracking
      {100, 180, 255, 255}  // Blue - D&C+DP
  };
  std::string solverNames[] = {"Greedy", "Backtracking", "D&C + DP"};

  // Group data by solver
  for (int s = 0; s < 3; ++s) {
    SDL_Color color = solverColors[s];
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    // Collect data points for this solver
    std::vector<std::pair<int, int64_t>> points; // (sizeIdx, timeMs)
    for (const auto &dp : scalabilityResults_) {
      if (dp.solverName == solverNames[s]) {
        // Find size index
        for (int i = 0; i < SCALABILITY_NUM_SIZES; ++i) {
          if (SCALABILITY_SIZES[i] == dp.nodeCount) {
            points.emplace_back(i, dp.timeMs);
            break;
          }
        }
      }
    }

    // Draw connecting lines (thick: 3 pixel offsets)
    for (size_t i = 1; i < points.size(); ++i) {
      int x1 =
          chartLeft + (points[i - 1].first * chartWidth) / (SCALABILITY_NUM_SIZES - 1);
      int y1 = chartBottom -
               static_cast<int>((static_cast<double>(points[i - 1].second) /
                                 static_cast<double>(yMax)) *
                                chartHeight);
      int x2 =
          chartLeft + (points[i].first * chartWidth) / (SCALABILITY_NUM_SIZES - 1);
      int y2 = chartBottom -
               static_cast<int>((static_cast<double>(points[i].second) /
                                 static_cast<double>(yMax)) *
                                chartHeight);

      // Clamp to chart area
      y1 = std::max(chartTop, std::min(chartBottom, y1));
      y2 = std::max(chartTop, std::min(chartBottom, y2));

      // Draw thick line (3 offsets)
      for (int offset = -1; offset <= 1; ++offset) {
        SDL_RenderDrawLine(renderer, x1, y1 + offset, x2, y2 + offset);
      }
    }

    // Draw data points (filled circles)
    for (const auto &pt : points) {
      int x =
          chartLeft + (pt.first * chartWidth) / (SCALABILITY_NUM_SIZES - 1);
      int y = chartBottom -
              static_cast<int>((static_cast<double>(pt.second) /
                                static_cast<double>(yMax)) *
                               chartHeight);
      y = std::max(chartTop, std::min(chartBottom, y));

      // Draw filled circle (radius 4)
      for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
          if (dx * dx + dy * dy <= 16) {
            SDL_RenderDrawPoint(renderer, x + dx, y + dy);
          }
        }
      }

      // Draw time label near each point
      if (menuBar) {
        std::string timeStr;
        if (pt.second >= 1000) {
          timeStr = std::to_string(pt.second / 1000) + "." +
                    std::to_string((pt.second % 1000) / 100) + "s";
        } else {
          timeStr = std::to_string(pt.second) + "ms";
        }
        SDL_Rect labelRect = {x - 20, y - 18, 40, 14};
        menuBar->RenderTextCentered(timeStr, labelRect, color);
      }
    }
  }

  // Draw axes
  SDL_SetRenderDrawColor(renderer, 200, 200, 210, 255);
  SDL_RenderDrawLine(renderer, chartLeft, chartTop, chartLeft, chartBottom);
  SDL_RenderDrawLine(renderer, chartLeft, chartBottom, chartRight, chartBottom);

  // Axis labels
  if (menuBar) {
    SDL_Rect xLabel = {winW / 2 - 80, chartBottom + 25, 160, 20};
    menuBar->RenderTextCentered("Graph Size (N)", xLabel,
                                {200, 200, 210, 255});

    // Y-axis label (rendered horizontally due to SDL limitations)
    SDL_Rect yLabel = {5, chartTop + chartHeight / 2 - 10, 80, 20};
    menuBar->RenderTextCentered("Runtime", yLabel, {200, 200, 210, 255});
  }

  // Legend
  int legendX = chartRight - 200;
  int legendY = chartTop + 10;
  SDL_Rect legendBg = {legendX - 5, legendY - 5, 195, 80};
  SDL_SetRenderDrawColor(renderer, 30, 30, 40, 220);
  SDL_RenderFillRect(renderer, &legendBg);
  SDL_SetRenderDrawColor(renderer, 80, 80, 100, 255);
  SDL_RenderDrawRect(renderer, &legendBg);

  for (int s = 0; s < 3; ++s) {
    SDL_Color color = solverColors[s];
    int ly = legendY + s * 22;

    // Color swatch line
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int offset = -1; offset <= 1; ++offset) {
      SDL_RenderDrawLine(renderer, legendX + 5, ly + 8 + offset,
                         legendX + 30, ly + 8 + offset);
    }
    // Dot
    for (int dy = -3; dy <= 3; ++dy) {
      for (int dx = -3; dx <= 3; ++dx) {
        if (dx * dx + dy * dy <= 9) {
          SDL_RenderDrawPoint(renderer, legendX + 17 + dx, ly + 8 + dy);
        }
      }
    }

    if (menuBar) {
      SDL_Rect nameRect = {legendX + 35, ly, 150, 16};
      menuBar->RenderTextCentered(solverNames[s], nameRect, color);
    }
  }

  // Buttons
  // "Re-run" button
  {
    SDL_Rect rerunBtn = {winW / 2 - 80, winH - 95, 160, 35};
    SDL_SetRenderDrawColor(renderer, 44, 62, 80, 255);
    SDL_RenderFillRect(renderer, &rerunBtn);
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_RenderDrawRect(renderer, &rerunBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Re-run Analysis", rerunBtn,
                                  {255, 255, 255, 255});
    }
  }

  // "Back to Menu" button
  {
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    SDL_SetRenderDrawColor(renderer, 44, 62, 80, 255);
    SDL_RenderFillRect(renderer, &backBtn);
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_RenderDrawRect(renderer, &backBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Back to Menu", backBtn,
                                  {255, 255, 255, 255});
    }
  }
}

void GameEngine::HandleHowItWorksInput(const SDL_Event &event) {
  if (event.type == SDL_KEYDOWN) {
    switch (event.key.keysym.sym) {
    case SDLK_ESCAPE:
      currentPhase = GamePhase::MAIN_MENU;
      return;
    case SDLK_LEFT:
    case SDLK_1:
      howItWorksTab_ = 0;
      return;
    case SDLK_DOWN:
    case SDLK_2:
      howItWorksTab_ = 1;
      return;
    case SDLK_RIGHT:
    case SDLK_3:
      howItWorksTab_ = 2;
      return;
    default:
      break;
    }
  }

  if (event.type == SDL_MOUSEBUTTONDOWN &&
      event.button.button == SDL_BUTTON_LEFT) {
    int mx = event.button.x;
    int my = event.button.y;
    SDL_Point pt = {mx, my};

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    // Tab buttons at top
    int tabWidth = 200;
    int tabGap = 15;
    int totalTabWidth = 3 * tabWidth + 2 * tabGap;
    int tabStartX = (winW - totalTabWidth) / 2;
    int tabY = 55;

    for (int i = 0; i < 3; ++i) {
      SDL_Rect tabRect = {tabStartX + i * (tabWidth + tabGap), tabY, tabWidth,
                          36};
      if (SDL_PointInRect(&pt, &tabRect)) {
        howItWorksTab_ = i;
        return;
      }
    }

    // Back button
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    if (SDL_PointInRect(&pt, &backBtn)) {
      currentPhase = GamePhase::MAIN_MENU;
      return;
    }
  }
}

void GameEngine::RenderHowItWorks() {
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);

  // Background
  SDL_SetRenderDrawColor(renderer, 20, 20, 25, 255);
  SDL_RenderClear(renderer);

  // Title
  if (menuBar) {
    SDL_Rect titleRect = {0, 15, winW, 35};
    menuBar->RenderTextCentered("How It Works - Algorithm Explainer",
                                titleRect, {255, 255, 255, 255});
  }

  // Tab buttons
  std::string tabNames[] = {"Greedy", "Backtracking", "D&C + DP"};
  SDL_Color tabColors[] = {
      {50, 205, 50, 255},   // Green
      {255, 165, 0, 255},   // Orange
      {100, 180, 255, 255}  // Blue
  };

  int tabWidth = 200;
  int tabGap = 15;
  int totalTabWidth = 3 * tabWidth + 2 * tabGap;
  int tabStartX = (winW - totalTabWidth) / 2;
  int tabY = 55;

  for (int i = 0; i < 3; ++i) {
    SDL_Rect tabRect = {tabStartX + i * (tabWidth + tabGap), tabY, tabWidth,
                        36};
    if (i == howItWorksTab_) {
      SDL_SetRenderDrawColor(renderer, tabColors[i].r, tabColors[i].g,
                             tabColors[i].b, 60);
      SDL_RenderFillRect(renderer, &tabRect);
      SDL_SetRenderDrawColor(renderer, tabColors[i].r, tabColors[i].g,
                             tabColors[i].b, 255);
      SDL_RenderDrawRect(renderer, &tabRect);
    } else {
      SDL_SetRenderDrawColor(renderer, 40, 40, 55, 255);
      SDL_RenderFillRect(renderer, &tabRect);
      SDL_SetRenderDrawColor(renderer, 70, 70, 90, 255);
      SDL_RenderDrawRect(renderer, &tabRect);
    }
    if (menuBar) {
      SDL_Color textColor =
          (i == howItWorksTab_) ? tabColors[i] : SDL_Color{150, 150, 170, 255};
      menuBar->RenderTextCentered(tabNames[i], tabRect, textColor);
    }
  }

  // Content area
  int contentLeft = 50;
  int contentTop = 105;
  int contentWidth = winW - 100;
  int lineHeight = 28;

  SDL_Color accent = tabColors[howItWorksTab_];
  SDL_Color white = {255, 255, 255, 255};
  SDL_Color dim = {160, 160, 180, 255};
  SDL_Color highlight = {255, 255, 100, 255};

  auto renderLine = [&](int y, const std::string &text, SDL_Color color) {
    if (menuBar) {
      SDL_Rect r = {contentLeft, y, contentWidth, lineHeight - 4};
      // Left-align by using a rect starting at contentLeft
      menuBar->RenderTextCentered(text, r, color);
    }
  };

  auto renderSectionHeader = [&](int y, const std::string &text) {
    // Draw accent bar
    SDL_SetRenderDrawColor(renderer, accent.r, accent.g, accent.b, 255);
    SDL_Rect bar = {contentLeft, y + 2, 4, lineHeight - 6};
    SDL_RenderFillRect(renderer, &bar);
    if (menuBar) {
      SDL_Rect r = {contentLeft + 12, y, contentWidth - 12, lineHeight - 4};
      menuBar->RenderTextCentered(text, r, accent);
    }
  };

  int y = contentTop;

  if (howItWorksTab_ == 0) {
    // ==================== GREEDY ====================
    renderSectionHeader(y, "What is it?");
    y += lineHeight + 4;
    renderLine(y, "A greedy algorithm always picks the BEST option available RIGHT NOW.", white);
    y += lineHeight;
    renderLine(y, "It never looks ahead or reconsiders past choices.", dim);
    y += lineHeight + 10;

    renderSectionHeader(y, "Real-Life Analogy");
    y += lineHeight + 4;
    renderLine(y, "Imagine you're in a parking lot. You grab the FIRST close spot you see.", highlight);
    y += lineHeight;
    renderLine(y, "You don't drive around to check if there's a closer one further ahead.", dim);
    y += lineHeight;
    renderLine(y, "Fast decision, but maybe not the best overall.", dim);
    y += lineHeight + 10;

    renderSectionHeader(y, "How it works in this game");
    y += lineHeight + 4;
    renderLine(y, "1. Look at every node in the graph", white);
    y += lineHeight;
    renderLine(y, "2. For each node, try many possible new positions", white);
    y += lineHeight;
    renderLine(y, "3. Count how many crossings each position removes", white);
    y += lineHeight;
    renderLine(y, "4. Pick the move that removes the MOST crossings", highlight);
    y += lineHeight;
    renderLine(y, "5. Repeat until no crossings remain (or it gets stuck)", white);
    y += lineHeight + 10;

    renderSectionHeader(y, "Speed vs Quality");
    y += lineHeight + 4;

    // Speed bar
    renderLine(y, "Speed:    [==========] Very Fast", {50, 205, 50, 255});
    y += lineHeight;
    renderLine(y, "Quality:  [======    ] Good, but can get stuck", {220, 180, 50, 255});
    y += lineHeight + 10;

    renderSectionHeader(y, "Time Complexity (How it scales)");
    y += lineHeight + 4;
    renderLine(y, "O(N^2 * C) per move  --  N = nodes, C = candidate positions", white);
    y += lineHeight;
    renderLine(y, "In plain English: If you double the nodes, it takes ~4x longer per move.", highlight);
    y += lineHeight;
    renderLine(y, "It's fast, but it can get STUCK in local minima (no single move helps).", dim);

  } else if (howItWorksTab_ == 1) {
    // ==================== BACKTRACKING ====================
    renderSectionHeader(y, "What is it?");
    y += lineHeight + 4;
    renderLine(y, "Backtracking tries a move, then tries ANOTHER move after that, and so on.", white);
    y += lineHeight;
    renderLine(y, "If a sequence doesn't work, it UNDOES everything and tries a different path.", dim);
    y += lineHeight + 10;

    renderSectionHeader(y, "Real-Life Analogy");
    y += lineHeight + 4;
    renderLine(y, "Imagine solving a maze. You walk down a path until you hit a dead end.", highlight);
    y += lineHeight;
    renderLine(y, "Then you walk BACK to the last fork and try a different direction.", dim);
    y += lineHeight;
    renderLine(y, "You explore deeper possibilities that greedy would never see.", dim);
    y += lineHeight + 10;

    renderSectionHeader(y, "How it works in this game");
    y += lineHeight + 4;
    renderLine(y, "1. Try moving a node to a new position (depth 1)", white);
    y += lineHeight;
    renderLine(y, "2. If crossings decrease, try ANOTHER move on top of that (depth 2)", white);
    y += lineHeight;
    renderLine(y, "3. Keep going up to 3 moves deep (depth limit = 3)", white);
    y += lineHeight;
    renderLine(y, "4. Remember the BEST first move from any sequence that worked", highlight);
    y += lineHeight;
    renderLine(y, "5. Undo all trial moves and apply only the best first move", white);
    y += lineHeight + 10;

    renderSectionHeader(y, "Speed vs Quality");
    y += lineHeight + 4;
    renderLine(y, "Speed:    [===       ] Slow (tries many combinations)", {220, 50, 50, 255});
    y += lineHeight;
    renderLine(y, "Quality:  [=========]  Excellent (sees further ahead)", {50, 205, 50, 255});
    y += lineHeight + 10;

    renderSectionHeader(y, "Time Complexity (How it scales)");
    y += lineHeight + 4;
    renderLine(y, "O((N * C)^D) per move  --  D = depth limit (3)", white);
    y += lineHeight;
    renderLine(y, "In plain English: Time EXPLODES as nodes increase. 10 nodes is fine,", highlight);
    y += lineHeight;
    renderLine(y, "30 nodes is slow, 50+ nodes might take minutes per move.", highlight);

  } else {
    // ==================== D&C + DP ====================
    renderSectionHeader(y, "What is it?");
    y += lineHeight + 4;
    renderLine(y, "Divide & Conquer splits the big problem into SMALLER sub-problems.", white);
    y += lineHeight;
    renderLine(y, "Dynamic Programming remembers solutions to avoid re-doing work.", dim);
    y += lineHeight + 10;

    renderSectionHeader(y, "Real-Life Analogy");
    y += lineHeight + 4;
    renderLine(y, "Imagine organizing a messy room. Instead of tackling everything at once:", highlight);
    y += lineHeight;
    renderLine(y, "Split the room into quadrants. Organize each quadrant separately.", dim);
    y += lineHeight;
    renderLine(y, "Then fix anything that's between quadrants. Divide and conquer!", dim);
    y += lineHeight + 10;

    renderSectionHeader(y, "How it works in this game");
    y += lineHeight + 4;
    renderLine(y, "1. DIVIDE: Split the graph into spatial regions (left/right or grid)", white);
    y += lineHeight;
    renderLine(y, "2. CONQUER: Find the best node placement within each region (using DP)", white);
    y += lineHeight;
    renderLine(y, "3. DP stores results so it doesn't recalculate the same sub-problem", highlight);
    y += lineHeight;
    renderLine(y, "4. COMBINE: Fix crossings at region boundaries", white);
    y += lineHeight;
    renderLine(y, "5. Pick the overall best move from all regions", white);
    y += lineHeight + 10;

    renderSectionHeader(y, "Speed vs Quality");
    y += lineHeight + 4;
    renderLine(y, "Speed:    [=======   ] Moderate (partition overhead)", {255, 165, 0, 255});
    y += lineHeight;
    renderLine(y, "Quality:  [========  ] Very Good (structured approach)", {50, 205, 50, 255});
    y += lineHeight + 10;

    renderSectionHeader(y, "Time Complexity (How it scales)");
    y += lineHeight + 4;
    renderLine(y, "O(N log N * C) per move  --  splits problem logarithmically", white);
    y += lineHeight;
    renderLine(y, "In plain English: Scales much better than backtracking.", highlight);
    y += lineHeight;
    renderLine(y, "Doubling nodes only adds a small constant, not a multiplier.", highlight);
  }

  // Keyboard hint
  if (menuBar) {
    SDL_Rect hint = {0, winH - 95, winW, 20};
    menuBar->RenderTextCentered("Press 1 / 2 / 3 or click tabs to switch algorithms",
                                hint, {100, 100, 120, 255});
  }

  // Back button
  {
    SDL_Rect backBtn = {winW / 2 - 80, winH - 55, 160, 35};
    SDL_SetRenderDrawColor(renderer, 44, 62, 80, 255);
    SDL_RenderFillRect(renderer, &backBtn);
    SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
    SDL_RenderDrawRect(renderer, &backBtn);
    if (menuBar) {
      menuBar->RenderTextCentered("Back to Menu", backBtn,
                                  {255, 255, 255, 255});
    }
  }
}

void GameEngine::PushLog(const std::string& message) {
  std::lock_guard<std::mutex> lock(logMutex_);
  if (!message.empty()) {
    computingLogs_.push_back(message);
    if (computingLogs_.size() > 20) {
      computingLogs_.erase(computingLogs_.begin());
    }
  }
}

void GameEngine::RenderComputingScreen(const std::string& title) {
  int winW, winH;
  SDL_GetWindowSize(window, &winW, &winH);

  if (menuBar) {
    SDL_Rect titleRect = {winW / 2 - 250, 60, 500, 40};
    menuBar->RenderTextCentered(title, titleRect, {255, 255, 255, 255});
  }

  // Draw logs
  {
    std::lock_guard<std::mutex> lock(logMutex_);
    int y = 140;
    for (const auto& log : computingLogs_) {
      if (menuBar) {
        SDL_Rect textRect = {winW / 2 - 400, y, 800, 20};
        menuBar->RenderTextCentered(log, textRect, {180, 180, 200, 255});
      }
      y += 22;
    }
  }

  // Draw Spinner
  computingSpinnerAngle_ += 6.0f;
  if (computingSpinnerAngle_ >= 360.0f) computingSpinnerAngle_ -= 360.0f;
  float rad = computingSpinnerAngle_ * M_PI / 180.0f;
  int cx = winW / 2;
  int cy = winH - 80;
  int r = 24;
  
  SDL_SetRenderDrawColor(renderer, 100, 180, 255, 255);
  for (int i = 0; i < 4; ++i) {
      float a = rad + i * M_PI / 2.0f;
      int x = cx + static_cast<int>(std::cos(a) * r);
      int y2 = cy + static_cast<int>(std::sin(a) * r);
      for(int dy=-3; dy<=3; ++dy) {
         for(int dx=-3; dx<=3; ++dx) {
            if (dx*dx + dy*dy <= 9) {
               SDL_RenderDrawPoint(renderer, x+dx, y2+dy);
            }
         }
      }
  }

  SDL_RenderPresent(renderer);
}

void GameEngine::StartComputingBenchmark() {
  currentPhase = GamePhase::COMPUTING_BENCHMARK;
  {
    std::lock_guard<std::mutex> lock(logMutex_);
    computingLogs_.clear();
  }
  
  if (logRedirector_) {
    delete static_cast<StreamRedirector*>(logRedirector_);
  }
  logRedirector_ = new StreamRedirector(std::cout, [this](const std::string& msg) {
      this->PushLog(msg);
  });
  
  backgroundTask_ = std::async(std::launch::async, [this]() {
      this->RunBenchmark();
  });
}

void GameEngine::StartComputingScalability() {
  currentPhase = GamePhase::COMPUTING_SCALABILITY;
  {
    std::lock_guard<std::mutex> lock(logMutex_);
    computingLogs_.clear();
  }
  
  if (logRedirector_) {
    delete static_cast<StreamRedirector*>(logRedirector_);
  }
  logRedirector_ = new StreamRedirector(std::cout, [this](const std::string& msg) {
      this->PushLog(msg);
  });
  
  backgroundTask_ = std::async(std::launch::async, [this]() {
      this->RunScalabilityTest();
  });
}

} // namespace GreedyTangle
