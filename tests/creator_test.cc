#include <cstdint>
#include <iostream>

#include "absl/strings/str_format.h"

#include "engine/shape.h"

using zebes::Shape;
using zebes::ShapeType;

int main(int argc, char *argv[]) {
  Shape shape;

  // uint8_t type = static_cast<uint8_t>(shape.type());
  // std::string type_string = Shape::TypeToString(static_cast<ShapeType>(type));
  // std::cout << absl::StrFormat("type = %u, type_string = %s\n", type,
  //                              type_string);

  // uint8_t type_minus_one = static_cast<uint8_t>(shape.type()) - 1;
  // std::string type_minus_one_string =
  //     Shape::TypeToString(static_cast<ShapeType>(type_minus_one));
  // std::cout << absl::StrFormat(
  //     "type_minus_one = %u, type_minus_one_string = %s\n", type_minus_one,
  //     type_minus_one_string);

  // uint8_t type_last = static_cast<uint8_t>(ShapeType::kShapeLast);
  // std::string type_last_string =
  //     Shape::TypeToString(static_cast<ShapeType>(type_last));
  // std::cout << absl::StrFormat("type_last = %u, type_last_string = %s\n",
  //                              type_last, type_last_string);

  // uint8_t type_return = (type_minus_one) % type_last;
  // std::string type_return_string =
  //     Shape::TypeToString(static_cast<ShapeType>(type_return));
  // std::cout << absl::StrFormat("type_return = %u, type_return_string = %s\n",
  //                              type_return, type_return_string);

  // uint8_t type_real = static_cast<uint8_t>(static_cast<uint8_t>(type) - 1) % 4;
  // std::string type_real_string =
  //     Shape::TypeToString(static_cast<ShapeType>(type_real));
  // std::cout << absl::StrFormat("type_return = %u, type_return_string = %s\n",
  //                              static_cast<uint8_t>(type_real), type_real_string);

  return 0;
}