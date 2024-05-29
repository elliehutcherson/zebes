#pragma once

#include <cstdint>
#include <string>

#include "engine/polygon.h"

namespace zebes {

enum Rotation : uint8_t {
  kRotationZero = 0,
  kRotationNinety = 1,
  kRotationOneEighty = 2,
  kRotationTwoSeventy = 3,
  kRotationLast
};

enum ShapeType : uint8_t {
  kSquare = 0,
  kTriangle_30_60_90 = 1,
  kTriangle_60_90_30 = 2,
  kTriangle_90_30_60 = 3,
  kTriangle_30_90_60 = 4,
  kTriangleRight = 5,
  kShapeLast
};

class Shape {
public:
  struct State {
    union {
      struct {
        Rotation rotation : 2 = kRotationZero;
        uint8_t primary0 : 1 = 0;
        uint8_t primary1 : 1 = 0;
        uint8_t primary2 : 1 = 0;
        uint8_t primary3 : 1 = 0;
        uint8_t reserved : 2;
        ShapeType type = kSquare;
      };
      uint8_t raw_eight[2];
      uint16_t raw_sixteen;
    };
    State() : rotation(kRotationZero), type(kSquare){};
  };

  static int render_width();
  static void set_render_width(int render_width);

  static int render_height();
  static void set_render_height(int render_height);

  static std::string TypeToString(ShapeType type);
  static std::string RotationToString(Rotation rotation);

  static Rotation RotationMinusOne(Rotation rotation);
  static Rotation RotationPlusOne(Rotation rotation);

  static ShapeType TypeMinusOne(ShapeType type);
  static ShapeType TypePlusOne(ShapeType type);

  static Point RotatePoint(Point point, Rotation rotation);

  explicit Shape() : state_(){};
  explicit Shape(State state) : state_(state){};
  explicit Shape(Rotation rotation, ShapeType type) {
    state_.rotation = rotation;
    state_.type = type;
  };

  State state() const;
  void set_state(State state);

  Rotation rotation() const;
  void set_rotation(Rotation rotation);

  ShapeType type() const;
  void set_type(ShapeType type);

  Point position() const;
  void set_position(Point position); 

  Polygon* polygon() const;

private:
  static int render_width_;
  static int render_height_;

  std::unique_ptr<Polygon> CreatePolygon(Point position) const;

  State state_;
  std::unique_ptr<Point> position_;
  std::unique_ptr<Polygon> polygon_;
};

}  // namespace zebes
