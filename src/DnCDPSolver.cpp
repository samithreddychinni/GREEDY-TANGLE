#include "DnCDPSolver.hpp"
#include "MathUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>

namespace GreedyTangle {

DnCDPSolver::Partition DnCDPSolver::CreatePartition(
    const std::vector<int> &nodeIndices,
    const std::vector<Node> &nodes) {

  Partition p;
  p.nodeIndices = nodeIndices;
  p.xMin = std::numeric_limits<float>::max();
  p.xMax = std::numeric_limits<float>::lowest();
  p.yMin = std::numeric_limits<float>::max();
  p.yMax = std::numeric_limits<float>::lowest();

  for (int idx : nodeIndices) {
    const Vec2 &pos = nodes[idx].position;
    p.xMin = std::min(p.xMin, pos.x);
    p.xMax = std::max(p.xMax, pos.x);
    p.yMin = std::min(p.yMin, pos.y);
    p.yMax = std::max(p.yMax, pos.y);
  }

  return p;
}

std::pair<DnCDPSolver::Partition, DnCDPSolver::Partition>
DnCDPSolver::SplitPartition(const Partition &partition,
                             const std::vector<Node> &nodes) {

  std::vector<std::pair<float, int>> xPositions;
  for (int idx : partition.nodeIndices) {
    xPositions.emplace_back(nodes[idx].position.x, idx);
  }
  std::sort(xPositions.begin(), xPositions.end());

  size_t midpoint = xPositions.size() / 2;

  std::vector<int> leftIndices, rightIndices;
  for (size_t i = 0; i < xPositions.size(); ++i) {
    if (i < midpoint) {
      leftIndices.push_back(xPositions[i].second);
    } else {
      rightIndices.push_back(xPositions[i].second);
    }
  }

  Partition left = CreatePartition(leftIndices, nodes);
  Partition right = CreatePartition(rightIndices, nodes);

  return {left, right};
}

std::vector<Edge> DnCDPSolver::GetRelevantEdges(
    const std::vector<int> &nodeIndices,
    const std::vector<Edge> &edges) {

  std::vector<bool> inPartition(1000, false);
  for (int idx : nodeIndices) {
    if (idx >= 0 && idx < 1000) {
      inPartition[idx] = true;
    }
  }

  std::vector<Edge> relevant;
  for (const Edge &e : edges) {
    if (inPartition[e.u_id] || inPartition[e.v_id]) {
      relevant.push_back(e);
    }
  }

  return relevant;
}

}
