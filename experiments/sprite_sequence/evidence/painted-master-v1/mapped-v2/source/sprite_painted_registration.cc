// Standalone semantic registration for retained complete paintings. The input
// owns observed raw landmarks and explicit target pins; no image bounds enter
// the solve. Output is an inverse sampling mesh in the original image pixels.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Point {
  double x = 0;
  double y = 0;
};

struct Registration {
  std::string name;
  std::vector<Point> source;
  std::vector<Point> target;
  std::vector<std::array<double, 2>> coefficients;
};

double Kernel(Point a, Point b) {
  const double r = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
  return r < 1e-15 ? 0 : r * std::log(r);
}

bool ReadPoint(std::istream& stream, Point& point) {
  return static_cast<bool>(stream >> point.x >> point.y) && std::isfinite(point.x) &&
         std::isfinite(point.y) && std::abs(point.x) < 10000 && std::abs(point.y) < 10000;
}

bool ReadRow(const std::string& line, Registration& registration) {
  std::istringstream stream(line);
  int count = 0;
  if (!(stream >> registration.name >> count) || count < 2 || count > 16 ||
      registration.name.empty() ||
      registration.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789") !=
          std::string::npos) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    Point source, target;
    if (!ReadPoint(stream, source) || !ReadPoint(stream, target)) return false;
    registration.source.push_back(source);
    registration.target.push_back(target);
  }
  stream >> std::ws;
  return stream.eof();
}

bool Solve(Registration& registration) {
  const size_t count = registration.source.size();
  for (size_t i = 0; i < count; ++i) {
    for (size_t j = 0; j < i; ++j) {
      if (std::hypot(registration.target[i].x - registration.target[j].x,
                     registration.target[i].y - registration.target[j].y) < 0.5 ||
          std::hypot(registration.source[i].x - registration.source[j].x,
                     registration.source[i].y - registration.source[j].y) < 0.5) {
        return false;
      }
    }
  }
  if (count == 2) return true;
  const size_t size = count + 3;
  std::vector<std::vector<double>> matrix(size, std::vector<double>(size + 2, 0));
  for (size_t i = 0; i < count; ++i) {
    const Point target = registration.target[i];
    for (size_t j = 0; j < count; ++j) {
      matrix[i][j] = Kernel(target, registration.target[j]);
    }
    const std::array<double, 3> affine = {1, target.x, target.y};
    for (size_t j = 0; j < 3; ++j) matrix[i][count + j] = matrix[count + j][i] = affine[j];
    matrix[i][size] = registration.source[i].x;
    matrix[i][size + 1] = registration.source[i].y;
  }
  for (size_t column = 0; column < size; ++column) {
    size_t pivot = column;
    for (size_t row = column + 1; row < size; ++row) {
      if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
    }
    if (std::abs(matrix[pivot][column]) < 1e-10) return false;
    std::swap(matrix[column], matrix[pivot]);
    const double divisor = matrix[column][column];
    for (size_t j = column; j < size + 2; ++j) matrix[column][j] /= divisor;
    for (size_t row = 0; row < size; ++row) {
      if (row == column) continue;
      const double multiplier = matrix[row][column];
      for (size_t j = column; j < size + 2; ++j) {
        matrix[row][j] -= multiplier * matrix[column][j];
      }
    }
  }
  for (size_t i = 0; i < size; ++i) {
    registration.coefficients.push_back({matrix[i][size], matrix[i][size + 1]});
  }
  return true;
}

Point Sample(const Registration& registration, Point point) {
  const size_t count = registration.source.size();
  if (count == 2) {
    const Point a = registration.target[0], b = registration.target[1];
    const Point c = registration.source[0], d = registration.source[1];
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double length = dx * dx + dy * dy;
    const double cosine = ((d.x - c.x) * dx + (d.y - c.y) * dy) / length;
    const double sine = ((d.y - c.y) * dx - (d.x - c.x) * dy) / length;
    return {c.x + cosine * (point.x - a.x) - sine * (point.y - a.y),
            c.y + sine * (point.x - a.x) + cosine * (point.y - a.y)};
  }
  const std::vector<std::array<double, 2>>& c = registration.coefficients;
  Point result{c[count][0] + c[count + 1][0] * point.x + c[count + 2][0] * point.y,
               c[count][1] + c[count + 1][1] * point.x + c[count + 2][1] * point.y};
  for (size_t i = 0; i < count; ++i) {
    const double weight = Kernel(point, registration.target[i]);
    result.x += weight * c[i][0];
    result.y += weight * c[i][1];
  }
  return result;
}

void Write(std::ostream& stream, const Registration& registration) {
  double residual = 0;
  for (size_t i = 0; i < registration.source.size(); ++i) {
    const Point mapped = Sample(registration, registration.target[i]);
    residual = std::max(residual, std::hypot(mapped.x - registration.source[i].x,
                                           mapped.y - registration.source[i].y));
  }
  stream << "{\"name\":\"" << registration.name << "\",\"landmark_residual_raw_px\":" << residual
         << ",\"mesh\":[";
  bool first = true;
  // Two master pixels per cell bound interpolation error around short boots.
  for (int y = 0; y < 512; y += 4) {
    for (int x = 0; x < 512; x += 4) {
      if (!first) stream << ',';
      first = false;
      stream << "[[" << x << ',' << y << ',' << x + 4 << ',' << y + 4 << "],[";
      const std::array<Point, 4> corners = {{{double(x), double(y)},
                                             {double(x), double(y + 4)},
                                             {double(x + 4), double(y + 4)},
                                             {double(x + 4), double(y)}}};
      for (size_t i = 0; i < corners.size(); ++i) {
        if (i != 0) stream << ',';
        const Point source = Sample(registration, corners[i]);
        stream << source.x << ',' << source.y;
      }
      stream << "]]";
    }
  }
  stream << "]}";
}

}  // namespace

int main() {
  std::vector<Registration> registrations;
  std::string line;
  while (std::getline(std::cin, line)) {
    Registration registration;
    if (!ReadRow(line, registration) || !Solve(registration)) {
      std::cerr << "invalid or singular semantic registration at row " << registrations.size() + 1
                << '\n';
      return 1;
    }
    if (std::any_of(registrations.begin(), registrations.end(), [&](const Registration& previous) {
          return previous.name == registration.name;
        })) {
      std::cerr << "duplicate registration identity\n";
      return 1;
    }
    registrations.push_back(registration);
  }
  if (!std::cin.eof() || registrations.empty()) {
    std::cerr << "missing registration rows\n";
    return 1;
  }
  std::ostringstream output;
  output << std::setprecision(12) << '[';
  for (size_t i = 0; i < registrations.size(); ++i) {
    if (i != 0) output << ',';
    Write(output, registrations[i]);
  }
  output << "]\n";
  std::cout << output.str();
  return std::cout.good() ? 0 : 1;
}
