#include "CPUController.hpp"
#include "MathUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace GreedyTangle {

CPUMove CPUController::FindBestMove(std::vector<Node> nodes,
                                    const std::vector<Edge> &edges) {
  auto start_time = std::chrono::steady_clock::now();

  int current_intersections = CountIntersections(nodes, edges);
  lastCandidatesEvaluated_ = 0;

  CPUMove best_move;
  best_move.intersections_before = current_intersections;
  int best_reduction = 0;

  // If already solved, return invalid move
  if (current_intersections == 0) {
    return best_move;
  }

  // Iterate over all nodes
  for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
    Vec2 original_position = nodes[node_idx].position;

    // Generate candidate positions for this node
    std::vector<Vec2> candidates =
        GenerateCandidatePositions(static_cast<int>(node_idx), nodes);

    // Evaluate each candidate position
    for (const Vec2 &candidate : candidates) {
      ++lastCandidatesEvaluated_;

      int new_intersections = CountIntersectionsWithMove(
          nodes, edges, static_cast<int>(node_idx), candidate);

      int reduction = current_intersections - new_intersections;

      if (reduction > best_reduction) {
        best_reduction = reduction;
        best_move.node_id = static_cast<int>(node_idx);
        best_move.from_position = original_position;
        best_move.to_position = candidate;
        best_move.intersections_after = new_intersections;
        best_move.intersection_reduction = reduction;
      }
    }
  }

  // Handle local minima: if no improvement found, try neutral moves
  if (best_reduction == 0) {
    // Find a neutral move (reduction = 0) that maximizes node spread
    float max_min_distance = 0.0f;

    for (size_t node_idx = 0; node_idx < nodes.size(); ++node_idx) {
      Vec2 original_position = nodes[node_idx].position;
      std::vector<Vec2> candidates =
          GenerateCandidatePositions(static_cast<int>(node_idx), nodes);

      for (const Vec2 &candidate : candidates) {
        int new_intersections = CountIntersectionsWithMove(
            nodes, edges, static_cast<int>(node_idx), candidate);

        int reduction = current_intersections - new_intersections;

        // Only consider neutral moves (no improvement, no regression)
        if (reduction == 0) {
          // Calculate minimum distance to other nodes at this position
          float min_dist = std::numeric_limits<float>::max();
          for (size_t other = 0; other < nodes.size(); ++other) {
            if (other != node_idx) {
              Vec2 diff = candidate - nodes[other].position;
              float dist = diff.magnitude();
              min_dist = std::min(min_dist, dist);
            }
          }

          // Prefer positions that maximize minimum distance (spread out)
          if (min_dist > max_min_distance) {
            max_min_distance = min_dist;
            best_move.node_id = static_cast<int>(node_idx);
            best_move.from_position = original_position;
            best_move.to_position = candidate;
            best_move.intersections_after = new_intersections;
            best_move.intersection_reduction = 0;
          }
        }
      }
    }
  }

  auto end_time = std::chrono::steady_clock::now();
  best_move.computation_time_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(end_time -
                                                            start_time)
          .count();

  if (best_move.isValid()) {
    std::cout << "[CPU] Found move: Node " << best_move.node_id << " -> ("
              << best_move.to_position.x << ", " << best_move.to_position.y
              << ") reduction=" << best_move.intersection_reduction
              << " time=" << best_move.computation_time_ms << "ms" << std::endl;
  } else {
    std::cout << "[CPU] No valid move found (stuck in local minimum)"
              << std::endl;
  }

  return best_move;
}

std::vector<Vec2>
CPUController::GenerateCandidatePositions(int node_id,
                                          const std::vector<Node> &nodes) {
  std::vector<Vec2> candidates;

  // Strategy 1: Grid sampling across the play area
  for (float x = MARGIN; x <= WINDOW_WIDTH - MARGIN; x += GRID_SPACING) {
    for (float y = MARGIN; y <= WINDOW_HEIGHT - MARGIN; y += GRID_SPACING) {
      candidates.emplace_back(x, y);
    }
  }

  // Strategy 2: Positions near neighbors (smarter placement)
  if (node_id >= 0 && node_id < static_cast<int>(nodes.size())) {
    const Node &target = nodes[node_id];
    float radius = 40.0f; // Distance from neighbor

    for (int neighbor_id : target.adjacencyList) {
      if (neighbor_id >= 0 && neighbor_id < static_cast<int>(nodes.size())) {
        const Vec2 &neighbor_pos = nodes[neighbor_id].position;

        // 8 positions around each neighbor
        for (int i = 0; i < 8; ++i) {
          float angle = 2.0f * M_PI * static_cast<float>(i) / 8.0f;
          Vec2 offset(std::cos(angle) * radius, std::sin(angle) * radius);
          Vec2 candidate = neighbor_pos + offset;

          // Clamp to screen bounds
          candidate.x =
              std::max(MARGIN, std::min(WINDOW_WIDTH - MARGIN, candidate.x));
          candidate.y =
              std::max(MARGIN, std::min(WINDOW_HEIGHT - MARGIN, candidate.y));

          candidates.push_back(candidate);
        }
      }
    }
  }

  // Strategy 3: Centroid of adjacent nodes
  if (node_id >= 0 && node_id < static_cast<int>(nodes.size())) {
    const Node &target = nodes[node_id];
    if (!target.adjacencyList.empty()) {
      Vec2 centroid(0, 0);
      for (int neighbor_id : target.adjacencyList) {
        if (neighbor_id >= 0 && neighbor_id < static_cast<int>(nodes.size())) {
          centroid = centroid + nodes[neighbor_id].position;
        }
      }
      centroid =
          centroid * (1.0f / static_cast<float>(target.adjacencyList.size()));
      candidates.push_back(centroid);
    }
  }

  return candidates;
}

int CPUController::CountIntersectionsWithMove(std::vector<Node> nodes,
                                              const std::vector<Edge> &edges,
                                              int node_id, Vec2 new_position) {
  // Temporarily move the node
  if (node_id >= 0 && node_id < static_cast<int>(nodes.size())) {
    nodes[node_id].position = new_position;
  }

  // Count intersections with the modified positions
  return CountIntersections(nodes, edges);
}

// ReplayLogger implementation

void ReplayLogger::StartMatch(const std::vector<Node> &initial_nodes,
                              const std::vector<Edge> &edges,
                              int initial_intersections) {
  Clear();
  initialIntersections_ = initial_intersections;

  for (const Node &node : initial_nodes) {
    initialPositions_.push_back(node.position);
  }

  for (const Edge &edge : edges) {
    edges_.emplace_back(edge.u_id, edge.v_id);
  }
}

void ReplayLogger::RecordMove(const CPUMove &move) { moves_.push_back(move); }

const CPUMove &ReplayLogger::GetMoveAt(int step) const {
  static CPUMove invalid;
  if (step >= 1 && step <= static_cast<int>(moves_.size())) {
    return moves_[step - 1];
  }
  return invalid;
}

bool ReplayLogger::IsSolved() const {
  if (moves_.empty()) {
    return initialIntersections_ == 0;
  }
  return moves_.back().intersections_after == 0;
}

int ReplayLogger::GetFinalIntersections() const {
  if (moves_.empty()) {
    return initialIntersections_;
  }
  return moves_.back().intersections_after;
}

std::string ReplayLogger::ExportJSON() const {
  std::ostringstream json;
  json << "{\n";
  json << "  \"initial_intersections\": " << initialIntersections_ << ",\n";
  json << "  \"total_moves\": " << moves_.size() << ",\n";
  json << "  \"solved\": " << (IsSolved() ? "true" : "false") << ",\n";

  // Initial node positions
  json << "  \"initial_positions\": [\n";
  for (size_t i = 0; i < initialPositions_.size(); ++i) {
    json << "    {\"id\": " << i << ", \"x\": " << initialPositions_[i].x
         << ", \"y\": " << initialPositions_[i].y << "}";
    if (i < initialPositions_.size() - 1)
      json << ",";
    json << "\n";
  }
  json << "  ],\n";

  // Moves array
  json << "  \"moves\": [\n";
  for (size_t i = 0; i < moves_.size(); ++i) {
    const CPUMove &m = moves_[i];
    json << "    {\n";
    json << "      \"step\": " << (i + 1) << ",\n";
    json << "      \"node_id\": " << m.node_id << ",\n";
    json << "      \"from\": {\"x\": " << m.from_position.x
         << ", \"y\": " << m.from_position.y << "},\n";
    json << "      \"to\": {\"x\": " << m.to_position.x
         << ", \"y\": " << m.to_position.y << "},\n";
    json << "      \"intersections_before\": " << m.intersections_before
         << ",\n";
    json << "      \"intersections_after\": " << m.intersections_after << ",\n";
    json << "      \"intersection_reduction\": " << m.intersection_reduction
         << ",\n";
    json << "      \"computation_time_ms\": " << m.computation_time_ms << "\n";
    json << "    }";
    if (i < moves_.size() - 1)
      json << ",";
    json << "\n";
  }
  json << "  ]\n";
  json << "}\n";

  return json.str();
}

void ReplayLogger::Clear() {
  initialPositions_.clear();
  edges_.clear();
  moves_.clear();
  initialIntersections_ = 0;
}

} // namespace GreedyTangle
