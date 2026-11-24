#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <tuple>
#include <utility>

#include "vr_math.h"
#include "vr_scope_finder.h"

namespace dxvk {

  namespace {

    /// Vertices closer than this along the barrel lie in one plane
    constexpr float PlaneGap         = 0.02f;
    /// Widest spread of a plane along the barrel
    constexpr float PlaneSpan        = 0.1f;
    /// Fewest distinct vertices of a circle
    constexpr size_t MinRingPoints   = 6;
    /// Smallest radius of a circle worth fitting
    constexpr float MinRingRadius    = 0.15f;
    /// Circles whose centres are this close share an axis
    constexpr float AxisTolerance    = 0.25f;
    /// Rings kept in a tube once its axis is known
    constexpr float AxisStrict       = 0.15f;
    /// A gap this long along the axis splits a tube
    constexpr float TubeGap          = 14.0f;
    /// Shortest tube
    constexpr float MinTubeLength    = 3.0f;
    /// Fewest rings in a tube
    constexpr size_t MinTubeRings    = 3;
    /// Where a scope's axis may be relative to the barrel
    constexpr float MinAbove         = 0.5f;
    constexpr float MaxAbove         = 15.0f;
    constexpr float MaxSide          = 3.0f;
    /// Smallest eyepiece
    constexpr float MinRadius        = 0.45f;
    /// Lens plane inside the rim, and in front of the glass
    constexpr float RimInset         = 0.3f;
    constexpr float GlassClearance   = 0.035f;
    /// Glass this near the rim takes the lens with it
    constexpr float NearGlass        = 1.0f;
    /// The line of sight behind the eyepiece that must be clear
    constexpr float SightLength      = 15.0f;
    constexpr float SightRadius      = 0.6f;
    /// Two candidates this close are one scope in two parts
    constexpr float SameLine         = 0.5f;

    constexpr float Pi = 3.14159265358979f;

    /// A vertex in the barrel's frame: along it, above it, to its right
    struct Point {
      float    a;
      float    u;
      float    s;
      uint32_t shape;
    };

    struct Ring {
      float    a;
      float    u;
      float    s;
      float    r;
      uint32_t count;
      uint32_t shape;
      bool     cap;
      bool     scope;
    };

    struct Circle {
      double u;
      double s;
      double r;
      double error;
    };

    using Point2 = std::pair<double, double>;

    /// Least squares circle (Kåsa), with its largest radial error
    bool fitCircle(const std::vector<Point2>& points, Circle& circle) {
      double n = double(points.size());
      double mu = 0.0, ms = 0.0;

      for (const Point2& p : points) {
        mu += p.first;
        ms += p.second;
      }

      mu /= n;
      ms /= n;

      double suu = 0.0, suv = 0.0, svv = 0.0;
      double suuu = 0.0, svvv = 0.0, suvv = 0.0, svuu = 0.0;

      for (const Point2& p : points) {
        double u = p.first - mu;
        double v = p.second - ms;
        suu  += u * u;
        suv  += u * v;
        svv  += v * v;
        suuu += u * u * u;
        svvv += v * v * v;
        suvv += u * v * v;
        svuu += v * u * u;
      }

      double det = suu * svv - suv * suv;

      if (std::fabs(det) < 1e-9)
        return false;

      double a  = 0.5 * (suuu + suvv);
      double b  = 0.5 * (svvv + svuu);
      double uc = (a * svv - b * suv) / det;
      double vc = (b * suu - a * suv) / det;

      circle.u = mu + uc;
      circle.s = ms + vc;
      circle.r = std::sqrt(std::max(uc * uc + vc * vc + (suu + svv) / n, 0.0));
      circle.error = 0.0;

      for (const Point2& p : points) {
        double d = std::hypot(p.first - circle.u, p.second - circle.s);
        circle.error = std::max(circle.error, std::fabs(d - circle.r));
      }

      return true;
    }

    double radialDistance(const Point2& p, const Circle& c) {
      return std::hypot(p.first - c.u, p.second - c.s);
    }

    /// Finds a circle among a plane's points, peeling off what lies off it
    bool findCircle(const std::vector<Point2>& pool, Circle& circle, std::vector<Point2>& used) {
      std::vector<Point2> remaining = pool;

      for (uint32_t attempt = 0; attempt < 5 && remaining.size() >= MinRingPoints; attempt++) {
        if (!fitCircle(remaining, circle))
          return false;

        if (circle.error <= std::max(0.05, 0.06 * circle.r) && circle.r > MinRingRadius) {
          used = std::move(remaining);
          return true;
        }

        std::vector<double> distances;
        distances.reserve(remaining.size());

        for (const Point2& p : remaining)
          distances.push_back(radialDistance(p, circle));

        std::nth_element(distances.begin(), distances.begin() + distances.size() / 2, distances.end());
        double median = distances[distances.size() / 2];

        std::vector<Point2> near;

        for (const Point2& p : remaining) {
          if (std::fabs(radialDistance(p, circle) - median) <= std::max(0.06, 0.05 * median))
            near.push_back(p);
        }

        if (near.size() < MinRingPoints || near.size() == remaining.size())
          return false;

        remaining = std::move(near);
      }

      return false;
    }

    int64_t quantize(float value) {
      return int64_t(std::llround(double(value) * 1e4));
    }

    /// The distinct vertices of a shape, in order along the barrel
    std::vector<Point> distinctPoints(std::vector<Point> points) {
      for (Point& p : points) {
        p.a = float(quantize(p.a)) * 1e-4f;
        p.u = float(quantize(p.u)) * 1e-4f;
        p.s = float(quantize(p.s)) * 1e-4f;
      }

      auto key = [] (const Point& p) {
        return std::make_tuple(quantize(p.a), quantize(p.u), quantize(p.s));
      };

      std::sort(points.begin(), points.end(), [&key] (const Point& x, const Point& y) {
        return key(x) < key(y);
      });

      points.erase(std::unique(points.begin(), points.end(), [&key] (const Point& x, const Point& y) {
        return key(x) == key(y);
      }), points.end());

      return points;
    }

    /// Circles of vertices in planes square to the barrel, several per plane
    std::vector<Ring> ringsOf(const std::vector<Point>& distinct, uint32_t shape, bool scope) {
      std::vector<Ring> rings;
      size_t start = 0;

      while (start < distinct.size()) {
        size_t end = start;

        while (end + 1 < distinct.size()
            && distinct[end + 1].a - distinct[end].a <= PlaneGap
            && distinct[end + 1].a - distinct[start].a <= PlaneSpan)
          end += 1;

        size_t count = end - start + 1;

        if (count >= MinRingPoints) {
          double a = 0.0;
          std::vector<Point2> pool;
          pool.reserve(count);

          for (size_t i = start; i <= end; i++) {
            a += distinct[i].a;
            pool.emplace_back(distinct[i].u, distinct[i].s);
          }

          a /= double(count);

          for (uint32_t circles = 0; circles < 6 && pool.size() >= MinRingPoints; circles++) {
            Circle circle;
            std::vector<Point2> used;

            if (!findCircle(pool, circle, used))
              break;

            rings.push_back({ float(a), float(circle.u), float(circle.s), float(circle.r),
              uint32_t(used.size()), shape, false, scope });

            std::sort(used.begin(), used.end());
            pool.erase(std::remove_if(pool.begin(), pool.end(), [&used] (const Point2& p) {
              return std::binary_search(used.begin(), used.end(), p);
            }), pool.end());
          }
        }

        start = end + 1;
      }

      return rings;
    }

    float axisDistance(const Ring& ring, float u, float s) {
      return std::hypot(ring.u - u, ring.s - s);
    }

    struct Candidate {
      std::vector<Ring> rings;
      float  u     = 0.0f;
      float  s     = 0.0f;
      double score = 0.0;
    };

    struct Eyepiece {
      float plane    = 0.0f;
      float radius   = 0.0f;
      bool  hasGlass = false;
      float glass    = 0.0f;
      uint32_t rings = 0;
    };

    /// Rings sharing an axis, split where they are far apart along it
    std::vector<Candidate> tubesOf(std::vector<Ring> rings) {
      std::stable_sort(rings.begin(), rings.end(), [] (const Ring& x, const Ring& y) {
        return x.r > y.r;
      });

      std::vector<std::vector<Ring>> clusters;

      for (const Ring& ring : rings) {
        auto cluster = std::find_if(clusters.begin(), clusters.end(), [&ring] (const std::vector<Ring>& c) {
          return axisDistance(ring, c.front().u, c.front().s) <= AxisTolerance;
        });

        if (cluster != clusters.end())
          cluster->push_back(ring);
        else
          clusters.push_back({ ring });
      }

      std::vector<Candidate> tubes;

      for (std::vector<Ring>& cluster : clusters) {
        std::stable_sort(cluster.begin(), cluster.end(), [] (const Ring& x, const Ring& y) {
          return x.a < y.a;
        });

        std::vector<Ring> current = { cluster.front() };

        auto close = [&tubes, &current] () {
          Candidate tube;
          tube.rings = std::move(current);
          tubes.push_back(std::move(tube));
          current.clear();
        };

        for (size_t i = 1; i < cluster.size(); i++) {
          if (cluster[i].a - current.back().a >= TubeGap)
            close();

          current.push_back(cluster[i]);
        }

        close();
      }

      return tubes;
    }

    /// Moves the rim back over a ring of vertices that no circle fit found
    float extendRim(const std::vector<Point>& points, float rim, float radius, float u, float s) {
      std::vector<Point> near;

      for (const Point& p : points) {
        float rho = std::hypot(p.u - u, p.s - s);

        if (p.a >= rim - 3.5f && p.a < rim && rho >= 0.9f * radius && rho <= 1.35f * radius)
          near.push_back(p);
      }

      if (near.empty())
        return rim;

      std::sort(near.begin(), near.end(), [] (const Point& x, const Point& y) {
        return x.a < y.a;
      });

      std::vector<float> planes;
      size_t start = 0;

      while (start < near.size()) {
        size_t end = start;

        while (end + 1 < near.size() && near[end + 1].a - near[start].a <= 0.05f)
          end += 1;

        uint32_t sectors = 0;
        float cu = 0.0f, cs = 0.0f;

        for (size_t i = start; i <= end; i++) {
          float angle = std::atan2(near[i].s - s, near[i].u - u);
          int sector = int((angle + Pi) / (2.0f * Pi) * 8.0f) % 8;
          sectors |= 1u << sector;
          cu += near[i].u;
          cs += near[i].s;
        }

        float n = float(end - start + 1);
        uint32_t covered = 0;

        for (uint32_t i = 0; i < 8; i++)
          covered += (sectors >> i) & 1u;

        // Around the axis, centred on it
        if (covered >= 6 && std::hypot(cu / n - u, cs / n - s) <= 0.15f * radius)
          planes.push_back(near[start].a);

        start = end + 1;
      }

      std::sort(planes.begin(), planes.end(), std::greater<float>());

      for (float a : planes) {
        if (rim - a <= 3.0f)
          rim = a;
      }

      return rim;
    }

    bool placeEyepiece(const Candidate& tube, const std::vector<Point>& points, Eyepiece& eyepiece) {
      float u = tube.u;
      float s = tube.s;

      auto rho = [u, s] (const Point& p) {
        return std::hypot(p.u - u, p.s - s);
      };

      std::vector<Ring> rings;

      for (const Ring& ring : tube.rings) {
        if (axisDistance(ring, u, s) <= AxisStrict)
          rings.push_back(ring);
      }

      if (rings.size() < MinTubeRings)
        return false;

      std::stable_sort(rings.begin(), rings.end(), [] (const Ring& x, const Ring& y) {
        return x.a < y.a;
      });

      std::vector<float> radii;

      for (const Ring& ring : rings)
        radii.push_back(ring.r);

      std::sort(radii.begin(), radii.end());
      float median = radii[radii.size() / 2];

      const Ring* first = nullptr;

      for (const Ring& ring : rings) {
        if (ring.r >= 0.5f * median) {
          first = &ring;
          break;
        }
      }

      float rim = extendRim(points, first->a, first->r, u, s);

      float rearMax = 0.0f;

      for (const Ring& ring : rings) {
        if (ring.a <= first->a + 3.0f)
          rearMax = std::max(rearMax, ring.r);
      }

      // The glass: geometry on the axis near the rear, flat discs, and the
      // rings that grow from the axis in the same part
      constexpr float Unset = std::numeric_limits<float>::max();
      float    glass      = Unset;
      uint32_t glassShape = 0;

      for (const Point& p : points) {
        if (p.a >= rim - 1.5f && p.a <= rim + 4.0f && rho(p) < 0.35f * rearMax && p.a < glass) {
          glass = p.a;
          glassShape = p.shape;
        }
      }

      std::vector<size_t> caps;

      for (size_t i = 0; i < rings.size(); i++) {
        const Ring& ring = rings[i];

        if (ring.cap && ring.a >= rim - 0.5f && ring.a <= rim + 4.0f)
          caps.push_back(i);

        bool small = ring.r < 0.45f * rearMax && ring.a >= rim - 1.5f && ring.a <= rim + 4.0f;

        if ((small || (ring.cap && ring.a >= rim - 0.5f && ring.a <= rim + 4.0f)) && ring.a < glass) {
          glass = ring.a;
          glassShape = ring.shape;
        }
      }

      std::vector<bool> isGlass(rings.size(), false);
      float edge = 0.0f;
      bool hasGlass = glass != Unset;

      if (hasGlass) {
        std::vector<size_t> chain;

        for (size_t i = 0; i < rings.size(); i++) {
          if (rings[i].shape == glassShape && rings[i].a >= glass - 0.3f && rings[i].a <= glass + 1.0f)
            chain.push_back(i);
        }

        std::stable_sort(chain.begin(), chain.end(), [&rings] (size_t x, size_t y) {
          return rings[x].r < rings[y].r;
        });

        float lastR = 0.0f;
        float lastA = glass;
        bool  any   = false;

        for (size_t i : chain) {
          float step = any ? 0.4f : 0.6f;

          if (rings[i].r - lastR <= step && std::fabs(rings[i].a - lastA) <= 0.25f && rings[i].r < 0.9f * rearMax) {
            isGlass[i] = true;
            lastR = rings[i].r;
            lastA = rings[i].a;
            any   = true;
          }
        }

        for (size_t i : caps) {
          if (std::fabs(rings[i].a - glass) <= 0.25f)
            isGlass[i] = true;
        }

        for (size_t i = 0; i < rings.size(); i++) {
          if (isGlass[i])
            edge = std::max(edge, rings[i].r);
        }
      }

      // The walls: rings wide enough to be the tube and not its glass
      std::vector<size_t> walls;

      for (size_t i = 0; i < rings.size(); i++) {
        if (!isGlass[i] && rings[i].r >= std::max(0.95f * edge, 0.5f * median))
          walls.push_back(i);
      }

      if (walls.empty())
        return false;

      float beforeGlass = hasGlass ? glass - GlassClearance : Unset;
      float plane = (!hasGlass || glass - rim > NearGlass)
        ? std::min(rim + RimInset, beforeGlass)
        : beforeGlass;

      // The narrowest opening between the eye and the lens
      float radius = Unset;

      for (float reach : { RimInset, 2.0f * RimInset }) {
        for (size_t i : walls) {
          if (rings[i].a <= plane + reach)
            radius = std::min(radius, rings[i].r);
        }

        if (radius != Unset)
          break;
      }

      if (radius == Unset) {
        size_t rear = walls.front();

        for (size_t i : walls) {
          if (rings[i].a < rings[rear].a)
            rear = i;
        }

        radius = rings[rear].r;
      }

      if (radius < MinRadius || u < radius + 0.3f)
        return false;

      // Something must be inside: glass, a reticle, a lens
      float end   = rings.back().a;
      float inner = Unset;

      for (const Point& p : points) {
        if (p.a >= rim - 0.5f && p.a <= end && rho(p) < 0.9f * radius)
          inner = std::min(inner, p.a);
      }

      for (size_t i : caps)
        inner = std::min(inner, rings[i].a);

      if (inner == Unset)
        return false;

      // And nothing across the line of sight behind it
      float sightEnd = std::min(rim, inner) - 0.2f;

      for (const Point& p : points) {
        if (p.a >= rim - SightLength && p.a < sightEnd && rho(p) < SightRadius * radius)
          return false;
      }

      eyepiece.plane    = std::min(std::max(plane, rim + RimInset), inner - GlassClearance);
      eyepiece.radius   = radius;
      eyepiece.hasGlass = hasGlass;
      eyepiece.glass    = hasGlass ? glass : 0.0f;
      eyepiece.rings    = uint32_t(rings.size());
      return true;
    }

  }


  bool vrIsScopeName(const char* name) {
    if (!name)
      return false;

    for (const char* c = name; *c; c++) {
      if (std::tolower(static_cast<unsigned char>(c[0])) == 's'
       && c[1] && std::tolower(static_cast<unsigned char>(c[1])) == 'c'
       && c[2] && std::tolower(static_cast<unsigned char>(c[2])) == 'o'
       && c[3] && std::tolower(static_cast<unsigned char>(c[3])) == 'p'
       && c[4] && std::tolower(static_cast<unsigned char>(c[4])) == 'e')
        return true;
    }

    return false;
  }


  bool vrFindScope(
    const std::vector<VrScopeShape>& shapes,
    const VrScopeBarrel&             barrel,
          VrScopePlacement&          placement,
          VrScopeFinding&            finding) {
    std::vector<Point> points;
    std::vector<Ring>  rings;

    for (uint32_t index = 0; index < shapes.size(); index++) {
      const VrScopeShape& shape = shapes[index];
      std::vector<Point> own;
      own.reserve(shape.points.size());

      for (const VrVector3& vertex : shape.points) {
        VrVector3 d = vertex - barrel.muzzle;
        own.push_back({ vrDot(d, barrel.forward), vrDot(d, barrel.up), vrDot(d, barrel.right), index });
      }

      points.insert(points.end(), own.begin(), own.end());

      std::vector<Point> distinct = distinctPoints(std::move(own));
      std::vector<Ring>  found    = ringsOf(distinct, index, shape.namedScope);

      // A part made only of one or two single circles is flat discs: the
      // glass at the ends of a scope. A tube has more.
      size_t onRings = 0;
      bool   onePerPlane = true;

      for (size_t i = 0; i < found.size(); i++) {
        onRings += found[i].count;

        for (size_t j = 0; j < i; j++) {
          if (std::fabs(found[i].a - found[j].a) < 0.01f)
            onePerPlane = false;
        }
      }

      bool cap = !found.empty() && found.size() <= 2 && onRings == distinct.size() && onePerPlane;

      for (Ring& ring : found) {
        ring.cap = cap;
        rings.push_back(ring);
      }
    }

    std::vector<Candidate> candidates;

    for (Candidate& tube : tubesOf(std::move(rings))) {
      const std::vector<Ring>& r = tube.rings;

      if (r.size() < MinTubeRings || r.back().a - r.front().a < MinTubeLength || r.front().a > 0.0f)
        continue;

      float u = 0.0f, s = 0.0f;
      bool scope = false;

      for (const Ring& ring : r) {
        u += ring.u;
        s += ring.s;
        scope |= ring.scope;
      }

      tube.u = u / float(r.size());
      tube.s = s / float(r.size());

      if (tube.u <= MinAbove || tube.u >= MaxAbove || std::fabs(tube.s) >= MaxSide)
        continue;

      // A part named as a scope wins over any other tube
      tube.score = double(r.size()) * double(r.back().a - r.front().a) + (scope ? 1e6 : 0.0);
      candidates.push_back(std::move(tube));
    }

    std::stable_sort(candidates.begin(), candidates.end(), [] (const Candidate& x, const Candidate& y) {
      return x.score > y.score;
    });

    const Candidate* best = nullptr;
    const Candidate* chosen = nullptr;
    Eyepiece result;

    for (const Candidate& candidate : candidates) {
      Eyepiece eyepiece;

      if (!placeEyepiece(candidate, points, eyepiece))
        continue;

      if (!best)
        best = &candidate;

      // A scope in two parts on one line: the eyepiece is in the rear one
      bool sameLine = std::fabs(candidate.u - best->u) <= SameLine
                   && std::fabs(candidate.s - best->s) <= SameLine;

      if (sameLine && (!chosen || eyepiece.plane < result.plane)) {
        chosen = &candidate;
        result = eyepiece;
      }
    }

    if (!chosen)
      return false;

    placement = VrScopePlacement();
    placement.eyepiece   = barrel.muzzle + barrel.forward * result.plane
                         + barrel.up * chosen->u + barrel.right * chosen->s;
    placement.halfWidth  = result.radius;
    placement.halfHeight = result.radius;

    finding.above      = chosen->u;
    finding.right      = chosen->s;
    finding.rings      = result.rings;
    finding.hasGlass   = result.hasGlass;
    finding.glassDepth = result.hasGlass ? result.glass - result.plane : 0.0f;
    return true;
  }

}
