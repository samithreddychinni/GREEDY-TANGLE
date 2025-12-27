#pragma once

#include "GraphData.hpp"
#include <string>
#include <vector>

namespace GreedyTangle {

/**
 * CPUMove - Represents a single move by the CPU
 * Used for both execution and replay logging
 */
struct CPUMove {
  int node_id = -1;
  Vec2 from_position;
  Vec2 to_position;
  int intersections_before = 0;
  int intersections_after = 0;
  int intersection_reduction = 0;
  int64_t computation_time_ms = 0;

  bool isValid() const { return node_id >= 0; }
};

/**
 * CPUController - Greedy algorithm for CPU opponent
 *
 * Algorithm: O(N × K × E²)
 *   N = number of nodes
 *   K = number of candidate positions (~50-100)
 *   E = number of edges
 *
 * The CPU evaluates every possible single-node move and selects
 * the one that maximizes immediate intersection reduction.
 */
class CPUController {
public:
  // Configuration constants
  static constexpr float GRID_SPACING = 80.0f;   // Pixels between sample points
  static constexpr float MARGIN = 60.0f;         // Border margin
  static constexpr float WINDOW_WIDTH = 1024.0f; // Match GameEngine
  static constexpr float WINDOW_HEIGHT = 768.0f;

  CPUController() = default;

  /**
   * Find the best greedy move for the current graph state
   * @param nodes Current node positions (will be modified temporarily)
   * @param edges Graph edges (read-only)
   * @return CPUMove with best node and position, or invalid move if stuck
   */
  CPUMove FindBestMove(std::vector<Node> nodes, const std::vector<Edge> &edges);

  /**
   * Get statistics about the last move computation
   */
  int GetLastCandidatesEvaluated() const { return lastCandidatesEvaluated_; }

private:
  /**
   * Generate candidate positions using grid sampling
   * Also includes neighbor-relative positions for smarter placement
   */
  std::vector<Vec2> GenerateCandidatePositions(int node_id,
                                               const std::vector<Node> &nodes);

  /**
   * Count intersections with a node temporarily moved
   * @param nodes Copy of nodes (will modify position of node_id)
   * @param edges Graph edges
   * @param node_id Node to move
   * @param new_position Target position
   * @return Number of intersections after the move
   */
  int CountIntersectionsWithMove(std::vector<Node> nodes,
                                 const std::vector<Edge> &edges, int node_id,
                                 Vec2 new_position);

  int lastCandidatesEvaluated_ = 0;
};

/**
 * ReplayLogger - Records CPU game history for replay
 *
 * Stores all moves made by the CPU so the game can be replayed
 * step-by-step using Next/Back/Play controls.
 */
class ReplayLogger {
public:
  ReplayLogger() = default;

  /**
   * Start a new match recording
   */
  void StartMatch(const std::vector<Node> &initial_nodes,
                  const std::vector<Edge> &edges, int initial_intersections);

  /**
   * Record a CPU move
   */
  void RecordMove(const CPUMove &move);

  /**
   * Get move at specific step (1-indexed)
   */
  const CPUMove &GetMoveAt(int step) const;

  /**
   * Get total number of moves recorded
   */
  int GetTotalMoves() const { return static_cast<int>(moves_.size()); }

  /**
   * Check if the game was solved (final intersections = 0)
   */
  bool IsSolved() const;

  /**
   * Get final intersection count
   */
  int GetFinalIntersections() const;

  /**
   * Export replay data as JSON string
   */
  std::string ExportJSON() const;

  /**
   * Clear all recorded data
   */
  void Clear();

  // Accessors for initial state
  const std::vector<Vec2> &GetInitialPositions() const {
    return initialPositions_;
  }
  int GetInitialIntersections() const { return initialIntersections_; }

private:
  std::vector<Vec2> initialPositions_;
  std::vector<std::pair<int, int>> edges_; // Edge pairs for JSON export
  int initialIntersections_ = 0;
  std::vector<CPUMove> moves_;
};

} // namespace GreedyTangle
