#include "CPUController.hpp"
#include <iostream>
#include <sstream>

namespace GreedyTangle {


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
