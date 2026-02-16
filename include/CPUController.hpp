#pragma once

#include "GraphData.hpp"
#include <string>
#include <vector>
#include <cstdint>

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
