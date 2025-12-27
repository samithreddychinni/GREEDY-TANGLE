#pragma once

#include "GraphData.hpp"
#include <cmath>

namespace GreedyTangle {

/**
 * Epsilon for floating-point comparisons
 * Prevents flickering from numerical instability
 */
constexpr float EPSILON = 1e-5f;

/**
 * CheckIntersection - Detect if two line segments intersect strictly internally
 *
 * Mathematical basis:
 * Given segments AB and CD:
 *   P(t) = A + t(B - A) for 0 ≤ t ≤ 1
 *   Q(u) = C + u(D - C) for 0 ≤ u ≤ 1
 *
 * Using cross product approach:
 *   (B - A) × (D - C) ≠ 0  implies non-parallel lines
 *
 * Intersection exists iff: 0 < t < 1 AND 0 < u < 1
 * (Strict inequalities: endpoint sharing is NOT an intersection)
 *
 * @param a First endpoint of segment 1
 * @param b Second endpoint of segment 1
 * @param c First endpoint of segment 2
 * @param d Second endpoint of segment 2
 * @return true if segments intersect strictly internally
 */
inline bool CheckIntersection(const Vec2 &a, const Vec2 &b, const Vec2 &c,
                              const Vec2 &d) {
  // Direction vectors
  Vec2 ab = b - a; // B - A
  Vec2 cd = d - c; // D - C
  Vec2 ac = c - a; // C - A

  // Calculate cross product denominator: (B - A) × (D - C)
  float denom = ab.cross(cd);

  // If denominator is ~0, lines are parallel (no intersection)
  if (std::fabs(denom) < EPSILON) {
    return false;
  }

  // Solve for t and u using Cramer's rule
  // t = (C - A) × (D - C) / ((B - A) × (D - C))
  // u = (C - A) × (B - A) / ((B - A) × (D - C))
  float t = ac.cross(cd) / denom;
  float u = ac.cross(ab) / denom;

  // Strict inequality check: 0 < t < 1 AND 0 < u < 1
  // Endpoints don't count as intersections in this puzzle
  return (t > EPSILON && t < (1.0f - EPSILON)) &&
         (u > EPSILON && u < (1.0f - EPSILON));
}

/**
 * Distance from point to line segment
 * Useful for edge selection/highlighting
 */
inline float PointToSegmentDistance(const Vec2 &point, const Vec2 &segA,
                                    const Vec2 &segB) {
  Vec2 ab = segB - segA;
  Vec2 ap = point - segA;

  float abLenSq = ab.magnitudeSquared();
  if (abLenSq < EPSILON) {
    // Degenerate segment (point)
    return ap.magnitude();
  }

  // Project point onto line, clamped to segment
  float t = std::max(0.0f, std::min(1.0f, ap.dot(ab) / abLenSq));
  Vec2 projection = segA + ab * t;

  return (point - projection).magnitude();
}

/**
 * Count total intersections in a graph
 * Victory condition: |I| = 0
 */
inline int CountIntersections(const std::vector<Node> &nodes,
                              const std::vector<Edge> &edges) {
  int count = 0;
  size_t numEdges = edges.size();

  for (size_t i = 0; i < numEdges; ++i) {
    for (size_t j = i + 1; j < numEdges; ++j) {
      const Edge &e1 = edges[i];
      const Edge &e2 = edges[j];

      // Skip edges that share a vertex
      if (e1.sharesVertex(e2)) {
        continue;
      }

      // Get node positions
      const Vec2 &a = nodes[e1.u_id].position;
      const Vec2 &b = nodes[e1.v_id].position;
      const Vec2 &c = nodes[e2.u_id].position;
      const Vec2 &d = nodes[e2.v_id].position;

      if (CheckIntersection(a, b, c, d)) {
        ++count;
      }
    }
  }

  return count;
}

} // namespace GreedyTangle
