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

#define _USE_MATH_DEFINES

namespace GreedyTangle {

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
    std::cerr << "[GameEngine] MenuBar init failed, continuing without menu"
              << std::endl;
    menuBar.reset();
  } else {
    SetupMenus();
  }

  isRunning = true;

  currentSolver_ = CreateSolver(static_cast<SolverMode>(currentMode));
  cpuReplayLogger_ = std::make_unique<ReplayLogger>();

  std::cout << "[GameEngine] Initialized successfully" << std::endl;
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
  SDL_Quit();
  std::cout << "[GameEngine] Cleanup complete" << std::endl;
}

void GameEngine::HandleInput() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    // Let menu handle events first
    if (menuBar && menuBar->HandleEvent(event)) {
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

  // Render menu bar on top
  if (menuBar) {
    menuBar->Render();
  }

  // Render input dialog on top of everything
  RenderInputDialog();

  // Render live scoreboard during gameplay
  RenderScoreboard();

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

  std::cout << "[GameEngine] Generated random graph: " << nodes.size()
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

  std::cout << "[GameEngine] Generated test graph: " << nodes.size()
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

  auto edgesCrossOnCircle = [nodeCount](int a, int b, int c, int d) -> bool {
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

  std::cout << "[GameEngine] Easy graph: " << nodes.size() << " nodes, "
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

  std::cout << "[GameEngine] Medium graph: " << nodes.size() << " nodes, "
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

  std::cout << "[GameEngine] Hard graph: " << nodes.size() << " nodes, "
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
  case GamePhase::SHOWING_UNTANGLED:
    if (elapsed >= UNTANGLED_DISPLAY_DURATION) {
      // Transition to tangling phase
      GenerateTangledTargets();
      currentPhase = GamePhase::TANGLING;
      phaseStartTime = now;
      animationProgress = 0.0f;
      std::cout << "[GameEngine] Starting tangle animation..." << std::endl;
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

      std::cout << "[GameEngine] Race started! Untangle the graph."
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
        std::cout << "[GameEngine] Victory! Showing analytics." << std::endl;
      }
    }
    break;

  case GamePhase::VICTORY:
    // Stay on victory screen until player starts new game
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
      MenuItem(), // Separator
      MenuItem("Exit", [this]() { isRunning = false; })};
  menuBar->AddMenu("Game", gameMenu);

  std::vector<MenuItem> modeMenu = {
      MenuItem(
          "Greedy", [this]() { SetGameMode(GameMode::GREEDY); }, true,
          currentMode == GameMode::GREEDY),
      MenuItem(
          "D&C + DP", [this]() { SetGameMode(GameMode::DIVIDE_AND_CONQUER_DP); }, true,
          currentMode == GameMode::DIVIDE_AND_CONQUER_DP)};
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
        std::cout << "Version 0.1.0\n" << std::endl;
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

  StartNewGame();
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

  StartNewGame();
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
  }

  // Restart game to apply new mode from scratch
  StartNewGame();
}

void GameEngine::CheckVictory() {
  // Only check during gameplay
  if (currentPhase != GamePhase::PLAYING)
    return;

  // If CPU already won, don't give human a victory
  if (winner_ == "cpu")
    return;

  // Victory when no intersections
  if (intersectionCount == 0 && !edges.empty()) {
    // Record time
    auto now = std::chrono::steady_clock::now();
    gameDuration = std::chrono::duration<float>(now - gameStartTime).count();

    if (winner_.empty()) {
      winner_ = "human";
    }

    // Start victory blink animation
    victoryStartTime = now;
    blinkCount = 0;
    currentPhase = GamePhase::VICTORY_BLINK;

    std::cout << "[GameEngine] Congratulations! Graph untangled in "
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
          << " seconds";

  // Format moves string
  std::string movesStr = "Moves: " + std::to_string(moveCount);

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
  cpuMoveCount_ = 0;
  winner_ = "";

  // Initialize delay timer so first move also respects delay
  cpuLastMoveTime_ = std::chrono::steady_clock::now();

  // Initialize replay logger with initial state
  cpuReplayLogger_->StartMatch(cpuNodes_, edges, cpuIntersectionCount_);

  std::cout << "[Race] Starting race mode! H: " << intersectionCount
            << " | CPU: " << cpuIntersectionCount_ << std::endl;

  // Note: Don't call StartNextCPUMove() immediately - let the delay timer work
  // The UpdateCPURace() loop will trigger it after the delay
}

void GameEngine::StartNextCPUMove() {
  if (cpuFinished_ || cpuSolving_) {
    return;
  }

  // Check if CPU already won
  if (cpuIntersectionCount_ == 0) {
    cpuFinished_ = true;
    if (winner_.empty()) {
      winner_ = "cpu";
      std::cout << "[Race] CPU WINS! Solved in " << cpuMoveCount_ << " moves."
                << std::endl;
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

  // Check if human just solved - compare moves with CPU
  if (intersectionCount == 0 && !edges.empty() && winner_.empty()) {
    // Wait for CPU to finish if still solving
    if (!cpuFinished_) {
      // CPU is still solving - let it finish first
      // (This frame we'll process CPU, next frame check again)
    } else {
      // Both finished - compare move counts
      if (moveCount < cpuMoveCount_) {
        winner_ = "human";
        std::cout << "[Game] HUMAN WINS! Human: " << moveCount
                  << " moves vs CPU: " << cpuMoveCount_ << " moves"
                  << std::endl;
      } else if (cpuMoveCount_ < moveCount) {
        winner_ = "cpu";
        std::cout << "[Game] CPU WINS! CPU: " << cpuMoveCount_
                  << " moves vs Human: " << moveCount << " moves" << std::endl;
      } else {
        winner_ = "tie";
        std::cout << "[Game] TIE! Both solved in " << moveCount << " moves"
                  << std::endl;
      }
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

          // CPU wins if human hasn't solved yet
          if (intersectionCount > 0 && winner_.empty()) {
            winner_ = "cpu";
            auto now = std::chrono::steady_clock::now();
            gameDuration = std::chrono::duration<float>(now - gameStartTime).count();
            victoryStartTime = now;
            blinkCount = 0;
            currentPhase = GamePhase::VICTORY_BLINK;
            std::cout << "[Game] CPU WINS! Solved in " << cpuMoveCount_
                      << " moves" << std::endl;
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

  // Scoreboard background
  int scoreW = 450;
  int scoreH = 40;
  int scoreX = (WINDOW_WIDTH - scoreW) / 2;
  int scoreY = WINDOW_HEIGHT - scoreH - 10;

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
            ? solverName + ": " + std::to_string(cpuMoveCount_) + " moves"
            : solverName + ": " + std::to_string(cpuIntersectionCount_) + " left";
    std::string scoreText =
        "Human: " + std::to_string(intersectionCount) + " left  |  " + cpuStatus;
    menuBar->RenderTextCentered(scoreText, scoreRect, {255, 255, 255, 255});
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

} // namespace GreedyTangle
