// Standalone experiment: no engine, renderer, provider, or third-party dependency.
// Each input row names a leg and supplies hip, knee, ankle, toe, source ankle,
// source heel, source toe, shaft length and shaft width. All rows are validated
// before JSON is written, so a malformed sequence cannot yield partial output.
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Point {
  double x = 0;
  double y = 0;
};

struct Leg {
  std::string name;
  std::string side;
  Point hip;
  Point knee;
  Point ankle;
  Point toe;
  Point source_ankle;
  Point source_heel;
  Point source_toe;
  double shaft_length = 0;
  double shaft_width = 0;
  std::string view;
  std::string contact;
  double heel_drop = 0;
};

Point Add(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
Point Subtract(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
Point Scale(Point a, double scale) { return {a.x * scale, a.y * scale}; }
double Length(Point a) { return std::hypot(a.x, a.y); }
double Dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
double Cross(Point a, Point b) { return a.x * b.y - a.y * b.x; }
Point Normal(Point a) { return {-a.y, a.x}; }

bool ReadPoint(std::istream& stream, Point& point) {
  return static_cast<bool>(stream >> point.x >> point.y) && std::isfinite(point.x) &&
         std::isfinite(point.y) && std::abs(point.x) < 10000 && std::abs(point.y) < 10000;
}

bool ReadLeg(const std::string& row, Leg& leg, bool atlas) {
  std::istringstream stream(row);
  if (!(stream >> leg.name >> leg.side)) return false;
  if (leg.name.empty() ||
      leg.name.find_first_not_of("abcdefghijklmnopqrstuvwxyz_0123456789") != std::string::npos) {
    return false;
  }
  if (leg.side != "near" && leg.side != "far") return false;
  for (Point* point : {&leg.hip, &leg.knee, &leg.ankle, &leg.toe, &leg.source_ankle,
                       &leg.source_heel, &leg.source_toe}) {
    if (!ReadPoint(stream, *point)) return false;
  }
  if (!(stream >> leg.shaft_length >> leg.shaft_width)) return false;
  if (atlas &&
      (!(stream >> leg.view >> leg.contact >> leg.heel_drop) || !std::isfinite(leg.heel_drop) ||
       std::abs(leg.heel_drop) > 5 || (leg.view != "profile" && leg.view != "sole_visible") ||
       (leg.contact != "none" && leg.contact != "heel" && leg.contact != "flat" &&
        leg.contact != "toe"))) {
    return false;
  }
  stream >> std::ws;
  if (!stream.eof()) return false;
  if (!std::isfinite(leg.shaft_length) || !std::isfinite(leg.shaft_width) ||
      leg.shaft_length <= 0 || leg.shaft_width <= 0 || leg.shaft_length > 100 ||
      leg.shaft_width > 100) {
    return false;
  }
  const Point source_foot = Subtract(leg.source_toe, leg.source_heel);
  if (Length(Subtract(leg.knee, leg.hip)) < 1 || Length(Subtract(leg.ankle, leg.knee)) < 1 ||
      Length(Subtract(leg.toe, leg.ankle)) < 1 ||
      Length(Subtract(leg.source_toe, leg.source_ankle)) < 1 || Length(source_foot) < 1) {
    return false;
  }
  // A heel-to-toe vector alone cannot distinguish the boot's upper and sole.
  return std::abs(Cross(source_foot, Subtract(leg.source_ankle, leg.source_heel))) /
             Length(source_foot) >
         0.5;
}

Point TransferHeel(const Leg& leg) {
  const Point source = Subtract(leg.source_toe, leg.source_ankle);
  const Point target = Subtract(leg.toe, leg.ankle);
  const Point heel = Subtract(leg.source_heel, leg.source_ankle);
  const double cosine_scale = Dot(source, target) / Dot(source, source);
  const double sine_scale = Cross(source, target) / Dot(source, source);
  return Add(leg.ankle, {heel.x * cosine_scale - heel.y * sine_scale,
                         heel.x * sine_scale + heel.y * cosine_scale});
}

void WritePoint(std::ostream& stream, Point point) {
  stream << '[' << point.x << ',' << point.y << ']';
}

template <typename Polygon>
void WritePolygon(std::ostream& stream, const Polygon& polygon) {
  stream << '[';
  for (size_t i = 0; i < polygon.size(); ++i) {
    if (i != 0) stream << ',';
    WritePoint(stream, polygon[i]);
  }
  stream << ']';
}

// The atlas mode is a separate experiment contract. The original profile output
// remains reproducible. A surface owns its outline and affine texture basis;
// the rasterizer must not infer shape, view, contact or limb assignment.
struct Surface {
  std::string material;
  std::vector<Point> outline;
  Point origin;
  Point u;
  Point v;
};

struct AtlasLeg {
  Leg input;
  Point heel;
  Point cuff;
  Point contact;
  std::vector<Surface> surfaces;
};

Point Unit(Point point) { return Scale(point, 1 / Length(point)); }

Point MapPoint(const Surface& surface, Point uv) {
  return Add(surface.origin, Add(Scale(surface.u, uv.x), Scale(surface.v, uv.y)));
}

void Curve(std::vector<Point>& points, Point control, Point end) {
  const Point start = points.back();
  for (int step = 1; step <= 8; ++step) {
    const double t = step / 8.0;
    points.push_back(Add(Add(Scale(start, (1 - t) * (1 - t)), Scale(control, 2 * t * (1 - t))),
                         Scale(end, t * t)));
  }
}

Surface Segment(const std::string& material, Point a, Point b, double width) {
  const Point along = Unit(Subtract(b, a));
  const Point across = Normal(along);
  const double cap = width * 0.22;
  Surface surface{material,
                  {{0.2, 0}},
                  Subtract(Subtract(a, Scale(across, width / 2)), Scale(along, cap)),
                  Scale(across, width),
                  Scale(along, Length(Subtract(b, a)) + 2 * cap)};
  Curve(surface.outline, {0, 0}, {0, 0.12});
  Curve(surface.outline, {0.03, 0.5}, {0.12, 0.91});
  Curve(surface.outline, {0.5, 1.08}, {0.88, 0.91});
  Curve(surface.outline, {0.97, 0.5}, {1, 0.12});
  Curve(surface.outline, {1, 0}, {0.8, 0});
  return surface;
}

void TrimAtSole(Surface& surface, Point heel, Point upper) {
  std::vector<Point> clipped;
  Point previous = surface.outline.back();
  double previous_height = Dot(Subtract(MapPoint(surface, previous), heel), upper);
  for (Point current : surface.outline) {
    const double height = Dot(Subtract(MapPoint(surface, current), heel), upper);
    if ((height >= 0) != (previous_height >= 0)) {
      const double t = previous_height / (previous_height - height);
      clipped.push_back(Add(previous, Scale(Subtract(current, previous), t)));
    }
    if (height >= 0) clipped.push_back(current);
    previous = current;
    previous_height = height;
  }
  surface.outline = clipped;
}

AtlasLeg BuildAtlasLeg(const Leg& leg, bool painted) {
  AtlasLeg result;
  result.input = leg;
  result.heel = TransferHeel(leg);
  result.heel.y += leg.heel_drop;
  // Flat support explicitly replaces the uncertain heel height, preserving the
  // traced ankle/toe and every limb direction. Recovery retains the accepted
  // heel triangle unchanged. Heel and toe here denote the bottom sole anchors.
  if (leg.contact == "flat") result.heel.y = leg.toe.y;
  if (painted) {
    // The trace supplies no heel. Bound this authored estimate so the ankle
    // enters the rear quarter of the sole, rather than the rejected middle.
    const Point axis = Unit(Subtract(leg.toe, result.heel));
    const double forward = Dot(Subtract(leg.toe, leg.ankle), axis);
    result.heel = Subtract(leg.toe, Scale(axis, forward / 0.78));
  }
  const Point shin = Subtract(leg.knee, leg.ankle);
  const Point up_shin = Unit(shin);
  result.cuff = Add(leg.ankle, Scale(up_shin, std::min(leg.shaft_length, Length(shin) * 0.7)));
  result.surfaces.push_back(Segment("trousers", leg.hip, leg.knee, 16));
  result.surfaces.push_back(Segment("trousers", leg.knee, result.cuff, 14));
  // Foot and shaft share leather at the ankle without an internal silhouette
  // outline. Only the union of their masks receives a boot outline.
  result.surfaces.push_back(Segment("shaft", result.cuff, leg.ankle, leg.shaft_width));
  const Point sole = Subtract(leg.toe, result.heel);
  Point upper = Unit(Normal(sole));
  if (Dot(upper, Subtract(leg.ankle, result.heel)) < 0) upper = Scale(upper, -1);
  TrimAtSole(result.surfaces.back(), result.heel, upper);
  const double height =
      std::max(Length(sole) * 0.38, Dot(upper, Subtract(leg.ankle, result.heel)) + 2);
  Surface foot{leg.view, {{0, 0}}, result.heel, sole, Scale(upper, height)};
  foot.outline.push_back({1, 0});
  Curve(foot.outline, {1.045, 0.36}, {0.91, 0.58});
  Curve(foot.outline, {0.78, 0.83}, {0.55, 0.83});
  Curve(foot.outline, {0.31, 1.05}, {0.08, 0.94});
  Curve(foot.outline, {-0.055, 0.73}, {0, 0});
  result.surfaces.push_back(foot);
  result.contact = leg.contact == "toe" ? leg.toe : result.heel;
  return result;
}

bool GroundAtlas(std::vector<AtlasLeg>& legs, std::vector<double>& shifts) {
  constexpr double kGround = 214;
  if (legs.size() % 2 != 0) return false;
  for (size_t i = 0; i < legs.size(); i += 2) {
    const AtlasLeg& far = legs[i];
    const AtlasLeg& near = legs[i + 1];
    if (far.input.name != near.input.name || far.input.side != "far" || near.input.side != "near" ||
        (far.input.contact != "none" && near.input.contact != "none")) {
      return false;
    }
    const AtlasLeg& support = far.input.contact != "none" ? far : near;
    const double shift = support.input.contact == "none" ? 0 : kGround - support.contact.y;
    shifts.push_back(shift);
  }
  for (size_t frame = 0; frame < shifts.size(); ++frame) {
    const size_t i = 2 * frame;
    const bool flight = legs[i].input.contact == "none" && legs[i + 1].input.contact == "none";
    if (!flight) continue;
    const size_t before = (frame + shifts.size() - 1) % shifts.size();
    const size_t after = (frame + 1) % shifts.size();
    for (size_t neighbor : {before, after}) {
      if (legs[2 * neighbor].input.contact == "none" &&
          legs[2 * neighbor + 1].input.contact == "none") {
        std::cerr << "flight requires neighboring support frames\n";
        return false;
      }
    }
    double shift = (shifts[before] + shifts[after]) / 2;
    for (size_t side = i; side < i + 2; ++side) {
      for (const Surface& surface : legs[side].surfaces) {
        for (Point uv : surface.outline) {
          shift = std::min(shift, kGround - 6 - MapPoint(surface, uv).y);
        }
      }
    }
    shifts[frame] = shift;
  }
  for (size_t i = 0; i < legs.size(); i += 2) {
    const double shift = shifts[i / 2];
    // Reject contradictory contact annotations instead of quietly grounding on
    // silhouette bounds or moving a boot independently of its complete chain.
    for (size_t side = i; side < i + 2; ++side) {
      for (const Surface& surface : legs[side].surfaces) {
        for (Point uv : surface.outline) {
          if (MapPoint(surface, uv).y + shift > kGround + 0.15) {
            std::cerr << legs[side].input.name << ' ' << legs[side].input.side << ' '
                      << surface.material << " penetrates ground by "
                      << MapPoint(surface, uv).y + shift - kGround << "px\n";
            return false;
          }
        }
      }
    }
  }
  return true;
}

void WriteSurface(std::ostream& stream, const Surface& surface) {
  stream << "{\"material\":\"" << surface.material << "\",\"uv_outline\":";
  WritePolygon(stream, surface.outline);
  stream << ",\"origin\":";
  WritePoint(stream, surface.origin);
  stream << ",\"u\":";
  WritePoint(stream, surface.u);
  stream << ",\"v\":";
  WritePoint(stream, surface.v);
  stream << ",\"outline\":[";
  for (size_t i = 0; i < surface.outline.size(); ++i) {
    if (i != 0) stream << ',';
    WritePoint(stream, MapPoint(surface, surface.outline[i]));
  }
  stream << "]}";
}

void WriteAtlasLeg(std::ostream& stream, const AtlasLeg& leg, bool painted) {
  stream << "{\"name\":\"" << leg.input.name << "\",\"side\":\"" << leg.input.side
         << "\",\"view\":\"" << leg.input.view << "\",\"contact_mode\":\"" << leg.input.contact
         << "\",\"anchors\":{";
  const std::array<std::pair<std::string, Point>, 7> anchors = {{{"hip", leg.input.hip},
                                                                 {"knee", leg.input.knee},
                                                                 {"ankle", leg.input.ankle},
                                                                 {"toe", leg.input.toe},
                                                                 {"heel", leg.heel},
                                                                 {"cuff", leg.cuff},
                                                                 {"contact", leg.contact}}};
  for (size_t i = 0; i < anchors.size(); ++i) {
    if (i != 0) stream << ',';
    stream << '"' << anchors[i].first << "\":";
    WritePoint(stream, anchors[i].second);
  }
  stream << '}';
  if (painted) {
    stream << ",\"sole_samples\":[";
    for (int i = 1; i < 8; ++i) {
      if (i != 1) stream << ',';
      WritePoint(stream, Add(leg.heel, Scale(Subtract(leg.input.toe, leg.heel), i / 8.0)));
    }
    stream << ']';
  }
  stream << ",\"surfaces\":[";
  for (size_t i = 0; i < leg.surfaces.size(); ++i) {
    if (i != 0) stream << ',';
    WriteSurface(stream, leg.surfaces[i]);
  }
  stream << "]}";
}

void WriteCoat(std::ostream& stream, const std::vector<AtlasLeg>& legs, size_t frame) {
  const size_t count = legs.size() / 2;
  double lift = 0;
  const std::array<double, 3> weights = {0.55, 0.3, 0.15};
  for (size_t lag = 0; lag < weights.size(); ++lag) {
    const size_t index = (frame + count - lag % count) % count;
    double target = 4;
    for (size_t side = 0; side < 2; ++side) {
      const Leg& leg = legs[2 * index + side].input;
      target = std::max(target, std::clamp(28 - (leg.ankle.y - leg.hip.y), 4.0, 21.0));
    }
    lift += target * weights[lag];
  }
  const Point hip = legs[frame * 2].input.hip;
  // A cyclic three-tap lag has no warm-up discontinuity at frame 12 -> 1.
  // Only the lower coat compresses; belt/torso attachment remains unchanged.
  stream << "{\"attachment_y\":" << std::floor(hip.y - 9) << ",\"lift\":" << lift
         << ",\"column_scale\":[";
  for (int x = 0; x < 256; ++x) {
    if (x != 0) stream << ',';
    const double distance = (x - (hip.x - 10)) / 34;
    stream << 1 - lift * std::exp(-distance * distance) / 42;
  }
  stream << "]}";
}

bool WriteAtlas(std::ostream& stream, const std::vector<Leg>& inputs, bool painted) {
  std::vector<AtlasLeg> legs;
  for (const Leg& leg : inputs) {
    AtlasLeg built = BuildAtlasLeg(leg, painted);
    const Point sole = Subtract(leg.toe, built.heel);
    if (painted && (Length(sole) < 2 || Dot(Subtract(leg.toe, leg.ankle), sole) <= 0 ||
                    std::abs(Cross(sole, Subtract(leg.ankle, built.heel))) / Length(sole) < 0.5)) {
      std::cerr << "painted boot requires a forward toe and distinct sole below ankle\n";
      return false;
    }
    legs.push_back(std::move(built));
  }
  std::vector<double> shifts;
  if (!GroundAtlas(legs, shifts)) return false;
  stream << "{\"version\":1,\"ground_y\":214,\"frames\":[";
  for (size_t i = 0; i < shifts.size(); ++i) {
    if (i != 0) stream << ',';
    stream << "{\"name\":\"" << legs[2 * i].input.name << "\",\"translation\":[0," << shifts[i]
           << "],\"legs\":[";
    WriteAtlasLeg(stream, legs[2 * i], painted);
    stream << ',';
    WriteAtlasLeg(stream, legs[2 * i + 1], painted);
    stream << "],\"coat\":";
    WriteCoat(stream, legs, i);
    stream << '}';
  }
  stream << "]}\n";
  return true;
}

void WriteLeg(std::ostream& stream, const Leg& leg) {
  const Point heel = TransferHeel(leg);
  const Point shin = Subtract(leg.knee, leg.ankle);
  const Point up_shin = Scale(shin, 1 / Length(shin));
  const Point across_shin = Normal(up_shin);
  const Point cuff = Add(leg.ankle, Scale(up_shin, std::min(leg.shaft_length, Length(shin) * 0.7)));
  const Point sole = Subtract(leg.toe, heel);
  Point upper = Scale(Normal(sole), 1 / Length(sole));
  if (Dot(upper, Subtract(leg.ankle, heel)) < 0) upper = Scale(upper, -1);
  const double foot_height =
      std::max(Length(sole) * 0.26, Dot(upper, Subtract(leg.ankle, heel)) + 1.5);
  const Point half_width = Scale(across_shin, leg.shaft_width / 2);
  const std::array<Point, 4> shaft = {Add(leg.ankle, half_width), Add(cuff, half_width),
                                      Subtract(cuff, half_width), Subtract(leg.ankle, half_width)};
  const std::array<Point, 5> foot = {heel, leg.toe, Add(leg.toe, Scale(upper, foot_height * 0.65)),
                                     Add(Add(heel, Scale(sole, 0.68)), Scale(upper, foot_height)),
                                     Add(heel, Scale(upper, foot_height))};
  stream << "{\"name\":\"" << leg.name << "\",\"side\":\"" << leg.side
         << "\",\"view\":\"profile\",\"hip\":";
  WritePoint(stream, leg.hip);
  stream << ",\"knee\":";
  WritePoint(stream, leg.knee);
  stream << ",\"ankle\":";
  WritePoint(stream, leg.ankle);
  stream << ",\"toe\":";
  WritePoint(stream, leg.toe);
  stream << ",\"heel\":";
  WritePoint(stream, heel);
  stream << ",\"cuff\":";
  WritePoint(stream, cuff);
  stream << ",\"foot\":";
  WritePolygon(stream, foot);
  stream << ",\"shaft\":";
  WritePolygon(stream, shaft);
  stream << '}';
}

}  // namespace

int main(int argc, char* argv[]) {
  const bool painted = argc == 2 && std::string(argv[1]) == "--painted";
  const bool flex = argc == 2 && std::string(argv[1]) == "--atlas-flex";
  const bool atlas = painted || flex || (argc == 2 && std::string(argv[1]) == "--atlas");
  if (argc != 1 && !atlas) {
    std::cerr << "usage: sprite_sequence_geometry [--atlas|--atlas-flex|--painted]\n";
    return 1;
  }
  std::vector<Leg> legs;
  std::string row;
  while (std::getline(std::cin, row)) {
    Leg leg;
    if (!ReadLeg(row, leg, atlas)) {
      std::cerr << "invalid leg geometry at row " << legs.size() + 1
                << ": require finite, noncollapsed joints and a distinct ankle/heel/toe triangle\n";
      return 1;
    }
    if (flex && leg.contact == "none") {
      // Reuse proof: articulate each free shin by twelve degrees, preserving
      // segment lengths and the attached ankle/toe/heel triangle. No new atlas
      // cell, view estimate or model request is supplied for this second run.
      constexpr double kAngle = 12 * 3.14159265358979323846 / 180;
      for (Point* point : {&leg.ankle, &leg.toe}) {
        const Point delta = Subtract(*point, leg.knee);
        *point = Add(leg.knee, {delta.x * std::cos(kAngle) - delta.y * std::sin(kAngle),
                                delta.x * std::sin(kAngle) + delta.y * std::cos(kAngle)});
      }
    }
    for (const Leg& existing : legs) {
      if (existing.name == leg.name && existing.side == leg.side) {
        std::cerr << "duplicate frame/leg identity\n";
        return 1;
      }
    }
    legs.push_back(leg);
  }
  if (!std::cin.eof() || legs.empty()) {
    std::cerr << "missing or unreadable sequence\n";
    return 1;
  }
  std::ostringstream output;
  output << std::setprecision(15);
  if (atlas) {
    if (!WriteAtlas(output, legs, painted)) {
      std::cerr << "invalid paired sequence or contact would penetrate the ground\n";
      return 1;
    }
    std::cout << output.str();
    return std::cout.good() ? 0 : 1;
  }
  output << '[';
  for (size_t i = 0; i < legs.size(); ++i) {
    if (i != 0) output << ',';
    WriteLeg(output, legs[i]);
  }
  output << "]\n";
  std::cout << output.str();
  return std::cout.good() ? 0 : 1;
}
