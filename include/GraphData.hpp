#pragma once

#include <cmath>
#include <vector>

namespace GreedyTangle {

/**
 * Vec2 - 2D Vector for Euclidean plane operations
 * Represents a point P ∈ ℝ²
 */
struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}

  // Vector addition: A + B
  Vec2 operator+(const Vec2 &other) const {
    return Vec2(x + other.x, y + other.y);
  }

  // Vector subtraction: A - B
  Vec2 operator-(const Vec2 &other) const {
    return Vec2(x - other.x, y - other.y);
  }

  // Scalar multiplication: t * V
  Vec2 operator*(float scalar) const { return Vec2(x * scalar, y * scalar); }

  // Cross product (2D): returns scalar z-component
  // (B - A) × (D - C) for intersection detection
  float cross(const Vec2 &other) const { return x * other.y - y * other.x; }

  // Dot product
  float dot(const Vec2 &other) const { return x * other.x + y * other.y; }

  // Magnitude squared (avoid sqrt for performance)
  float magnitudeSquared() const { return x * x + y * y; }

  // Magnitude
  float magnitude() const { return std::sqrt(magnitudeSquared()); }
};

// Scalar multiplication from left: scalar * Vec2
inline Vec2 operator*(float scalar, const Vec2 &v) {
  return Vec2(v.x * scalar, v.y * scalar);
}

/**
 * Node - Vertex in graph G = (V, E)
 * Each node v_i has spatial coordinate P ∈ ℝ²
 */
struct Node {
  int id;                         // Unique identifier
  Vec2 position;                  // Current screen coordinates
  float radius = 15.0f;           // Hitbox radius for click detection
  std::vector<int> adjacencyList; // IDs of connected neighbors

  // Input state machine flags (render-time only, no SDL dependencies)
  bool isDragging = false; // Currently being dragged by mouse
  bool isHovered = false;  // Mouse cursor is over this node

  Node()
      : id(-1), position(), radius(15.0f), isDragging(false), isHovered(false) {
  }
  Node(int id_, Vec2 pos_, float radius_ = 15.0f)
      : id(id_), position(pos_), radius(radius_), isDragging(false),
        isHovered(false) {}

  // Check if a point is within the node's hitbox
  bool containsPoint(const Vec2 &point) const {
    Vec2 diff = point - position;
    return diff.magnitudeSquared() <= (radius * radius);
  }
};

/**
 * Edge - Connection e_ij between nodes v_i and v_j
 */
struct Edge {
  int u_id;            // First node ID
  int v_id;            // Second node ID
  bool isIntersecting; // Visual feedback flag (red if true, green if false)

  Edge() : u_id(-1), v_id(-1), isIntersecting(false) {}
  Edge(int u, int v) : u_id(u), v_id(v), isIntersecting(false) {}

  // Check if edge shares a vertex with another edge
  bool sharesVertex(const Edge &other) const {
    return u_id == other.u_id || u_id == other.v_id || v_id == other.u_id ||
           v_id == other.v_id;
  }
};

} // namespace GreedyTangle
