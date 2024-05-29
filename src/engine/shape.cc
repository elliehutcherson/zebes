#include "shape.h"

#include <memory>

#include "polygon.h"

namespace zebes {

std::string Shape::TypeToString(ShapeType type) {
  switch (type) {
  case kSquare:
    return "Square";
  case kTriangle_30_60_90:
    return "Triangle_30_60_90";
  case kTriangle_60_90_30:
    return "Triangle_60_90_30";
  case kTriangle_90_30_60:
    return "Triangle_90_30_60";
  case kTriangle_30_90_60:
    return "Triangle_30_90_60";
  case kTriangleRight:
    return "TriangleRight";
  default:
    return "Unknown";
  }
};

std::string Shape::RotationToString(Rotation rotation) {
  switch (rotation) {
  case kRotationZero:
    return "Zero";
  case kRotationNinety:
    return "Ninety";
  case kRotationOneEighty:
    return "OneEighty";
  case kRotationTwoSeventy:
    return "TwoSeventy";
  default:
    return "Unknown";
  }
};

int Shape::render_width_ = 0;
int Shape::render_width() { return render_width_; }
void Shape::set_render_width(int render_width) {
  render_width_ = render_width;
};

int Shape::render_height_ = 0;
int Shape::render_height() { return render_height_; }
void Shape::set_render_height(int render_height) {
  render_height_ = render_height;
};

Rotation Shape::RotationMinusOne(Rotation rotation) {
  return static_cast<Rotation>(
      static_cast<uint8_t>(static_cast<uint8_t>(rotation) - 1) %
      static_cast<uint8_t>(Rotation::kRotationLast));
};

Rotation Shape::RotationPlusOne(Rotation rotation) {
  return static_cast<Rotation>(
      static_cast<uint8_t>(static_cast<uint8_t>(rotation) + 1) %
      static_cast<uint8_t>(Rotation::kRotationLast));
};

ShapeType Shape::TypeMinusOne(ShapeType type) {
  return static_cast<ShapeType>(
      static_cast<uint8_t>(static_cast<uint8_t>(type) - 1) %
      static_cast<uint8_t>(kShapeLast));
};

ShapeType Shape::TypePlusOne(ShapeType type) {
  return static_cast<ShapeType>(
      static_cast<uint8_t>(static_cast<uint8_t>(type) + 1) %
      static_cast<uint8_t>(kShapeLast));
};

Point Shape::RotatePoint(Point point, Rotation rotation) {
  switch (rotation) {
  case kRotationZero:
    return point;
  case kRotationNinety:
    return {.x = 1.0f - point.y, .y = point.x};
  case kRotationOneEighty:
    return {.x = 1.0f - point.x, .y = 1.0f - point.y};
  case kRotationTwoSeventy:
    return {.x = point.y, .y = 1.0f - point.x};
  default:
    return point;
  }
};

Shape::State Shape::state() const { return state_; }
void Shape::set_state(State state) {
  state_ = state;
  if (position_ != nullptr)
    polygon_ = CreatePolygon(*position_);
}

Rotation Shape::rotation() const { return state_.rotation; }
void Shape::set_rotation(Rotation rotation) {
  state_.rotation = rotation;
  if (position_ != nullptr)
    polygon_ = CreatePolygon(*position_);
}

ShapeType Shape::type() const { return state_.type; }
void Shape::set_type(ShapeType type) {
  state_.type = type;
  if (position_ != nullptr)
    polygon_ = CreatePolygon(*position_);
}

Point Shape::position() const { return *position_; }
void Shape::set_position(Point position) {
  position_ = std::unique_ptr<Point>(new Point(position));
  polygon_ = CreatePolygon(position);
}

Polygon *Shape::polygon() const { return polygon_.get(); }

std::unique_ptr<Polygon> Shape::CreatePolygon(Point position) const {
  constexpr float short_width_ratio = 1.0 / 4.0;
  constexpr float long_width_ratio = 3.0 / 4.0;
  static const float short_height_ratio = sqrt(3.0) / 4.0;
  static const float long_height_ratio = (4.0 - sqrt(3.0)) / 4.0;

  std::vector<Point> ratios;
  switch (state_.type) {
  case kSquare:
    ratios = {
        {.x = 0, .y = 0}, {.x = 1, .y = 0}, {.x = 1, .y = 1}, {.x = 0, .y = 1}};
    break;
  case kTriangle_30_60_90:
    ratios = {{.x = 0, .y = 0},
              {.x = 1, .y = 0},
              {.x = long_width_ratio, .y = short_height_ratio}};
    break;
  case kTriangle_60_90_30:
    ratios = {{.x = 0, .y = 0},
              {.x = 1, .y = 0},
              {.x = short_width_ratio, .y = short_height_ratio}};
    break;
  case kTriangle_90_30_60:
    ratios = {{.x = 0, .y = 0}, {.x = 1, .y = 0}, {.x = 0, .y = 0.5f}};
    break;
  case kTriangle_30_90_60:
    ratios = {{.x = 0, .y = 0}, {.x = 1, .y = 0}, {.x = 1, .y = 0.5f}};
    break;
  case kTriangleRight:
    ratios = {{.x = 0, .y = 0}, {.x = 1, .y = 0}, {.x = 1, .y = 1}};
    break;
  default:
    ratios = {};
  }

  std::vector<Point> vertices;
  for (auto &ratio : ratios) {
    Point rotated_ratio = RotatePoint(ratio, state_.rotation);
    vertices.push_back({.x = position.x + (rotated_ratio.x * render_width()),
                        .y = position.y + (rotated_ratio.y * render_height())});
  }
  return std::unique_ptr<Polygon>(new Polygon(vertices));
}

} // namespace zebes
