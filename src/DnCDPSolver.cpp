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

CPUMove DnCDPSolver::SolveBaseCase(std::vector<Node> &nodes,
                                    const std::vector<Edge> &edges,
                                    const Partition &partition) {
  CPUMove best_move;
  int current_intersections = CountIntersections(nodes, edges);
  best_move.intersections_before = current_intersections;
  int best_reduction = 0;

  for (int nodeIdx : partition.nodeIndices) {
    Vec2 original = nodes[nodeIdx].position;

    float stepX = (partition.xMax - partition.xMin) / 6.0f;
    float stepY = (partition.yMax - partition.yMin) / 6.0f;
    if (stepX < 20.0f) stepX = 20.0f;
    if (stepY < 20.0f) stepY = 20.0f;

    for (float x = MARGIN; x <= WINDOW_WIDTH - MARGIN; x += stepX) {
      for (float y = MARGIN; y <= WINDOW_HEIGHT - MARGIN; y += stepY) {
        ++lastCandidatesEvaluated_;

        nodes[nodeIdx].position = Vec2(x, y);
        int newCount = CountIntersections(nodes, edges);
        int reduction = current_intersections - newCount;

        if (reduction > best_reduction) {
          best_reduction = reduction;
          best_move.node_id = nodeIdx;
          best_move.from_position = original;
          best_move.to_position = Vec2(x, y);
          best_move.intersections_after = newCount;
          best_move.intersection_reduction = reduction;
        }
      }
    }

    nodes[nodeIdx].position = original;
  }

  return best_move;
}

std::vector<Vec2> DnCDPSolver::GenerateDPCandidates(const Partition &partition) {
  std::vector<Vec2> candidates;

  float pxMin = std::max(MARGIN, partition.xMin - 50.0f);
  float pxMax = std::min(WINDOW_WIDTH - MARGIN, partition.xMax + 50.0f);
  float pyMin = std::max(MARGIN, partition.yMin - 50.0f);
  float pyMax = std::min(WINDOW_HEIGHT - MARGIN, partition.yMax + 50.0f);

  float spanX = pxMax - pxMin;
  float spanY = pyMax - pyMin;
  float step = std::max(40.0f, std::min(spanX, spanY) / 8.0f);

  for (float x = pxMin; x <= pxMax; x += step) {
    for (float y = pyMin; y <= pyMax; y += step) {
      candidates.emplace_back(x, y);
    }
  }

  float cx = (pxMin + pxMax) / 2.0f;
  float cy = (pyMin + pyMax) / 2.0f;
  candidates.emplace_back(cx, cy);

  return candidates;
}

std::vector<int> DnCDPSolver::OrderNodesByDegree(
    const std::vector<int> &nodeIndices,
    const std::vector<Node> &nodes) {

  std::vector<std::pair<int, int>> degreeList;
  for (int idx : nodeIndices) {
    int degree = static_cast<int>(nodes[idx].adjacencyList.size());
    degreeList.emplace_back(degree, idx);
  }

  std::sort(degreeList.begin(), degreeList.end(),
            [](const auto &a, const auto &b) { return a.first > b.first; });

  std::vector<int> ordered;
  for (const auto &p : degreeList) {
    ordered.push_back(p.second);
  }

  return ordered;
}

}
