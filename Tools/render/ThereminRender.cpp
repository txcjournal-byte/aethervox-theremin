// =============================================================================
//  AetherVox - offline product renderer for the plugin artwork
//
//  A small physically-motivated SDF ray marcher that renders the instrument
//  used as the plugin UI: piano-black lacquer cabinet, oxblood lacquer inlay,
//  polished chrome pitch rod + volume loop, brushed black aluminium front
//  panel, knurled knobs with brushed chrome caps (rendered WITHOUT indicator
//  lines - the plugin draws those, so the knobs can turn), black glass display.
//  Lighting: three studio softboxes (explicitly sampled area lights, matching
//  what the reflections see), dark seamless studio, glossy floor.
//
//    g++ -O3 -march=native -std=c++17 -pthread ThereminRender.cpp -o render
//    ./render out.ppm coords.txt [width height spp]
// =============================================================================
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <vector>

struct V3 { double x, y, z; };
static inline V3 operator+ (V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline V3 operator- (V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline V3 operator* (V3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
static inline V3 operator* (V3 a, V3 b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
static inline double dot (V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross (V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline double len (V3 a) { return std::sqrt (dot (a, a)); }
static inline V3 norm (V3 a) { return a * (1.0 / len (a)); }
static inline V3 reflect (V3 d, V3 n) { return d - n * (2.0 * dot (d, n)); }
static inline double clamp01 (double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline double mix (double a, double b, double t) { return a + (b - a) * t; }
static inline V3 mixv (V3 a, V3 b, double t) { return a + (b - a) * t; }

// ----------------------------------------------------------------------------- geometry (cm)
namespace geo
{
    constexpr double cabCY = 14.5, cabHX = 60.0, cabHY = 10.5, cabHZ = 15.0, cabR = 4.2;
    constexpr double frontZ = -cabHZ;                                   // front face of cabinet
    constexpr double rodX = -50.0, rodBase = cabCY + cabHY, rodTop = 106.0, rodR = 0.85;
    constexpr double loopCX = 87.0, loopCY = 21.0, loopL = 12.0, loopRad = 11.5, loopTube = 0.85;
    constexpr double knobY = 14.0, knobR = 3.5, knobDepth = 2.5, capR = 2.7;
    const double knobX[6] = { -46.0, -34.0, -22.0, 22.0, 34.0, 46.0 };
    constexpr double dispHX = 11.0, dispHY = 4.6;
}

enum Mat { M_NONE = 0, M_LACQUER, M_CHROME, M_PANEL, M_RED, M_SKIRT, M_CAP, M_GLASS, M_BEZEL };

static inline double sdRoundBox (V3 p, V3 b, double r)
{
    V3 q { std::fabs (p.x) - b.x + r, std::fabs (p.y) - b.y + r, std::fabs (p.z) - b.z + r };
    V3 m { std::max (q.x, 0.0), std::max (q.y, 0.0), std::max (q.z, 0.0) };
    return len (m) + std::min (std::max (q.x, std::max (q.y, q.z)), 0.0) - r;
}

static inline double sdCapsuleY (V3 p, double y0, double y1, double r)
{
    const double y = std::clamp (p.y, y0, y1);
    return len ({ p.x, p.y - y, p.z }) - r;
}

/** Cylinder along z, centred at c, radius r, half depth h, edge round e. */
static inline double sdCylZ (V3 p, double r, double h, double e)
{
    const double dx = std::sqrt (p.x * p.x + p.y * p.y) - r + e, dz = std::fabs (p.z) - h + e;
    return std::min (std::max (dx, dz), 0.0) + len ({ std::max (dx, 0.0), std::max (dz, 0.0), 0.0 }) - e;
}

/** Cylinder along y. */
static inline double sdCylY (V3 p, double r, double h, double e)
{
    const double dx = std::sqrt (p.x * p.x + p.z * p.z) - r + e, dy = std::fabs (p.y) - h + e;
    return std::min (std::max (dx, dy), 0.0) + len ({ std::max (dx, 0.0), std::max (dy, 0.0), 0.0 }) - e;
}

struct Hit { double d; int m; };

/** Distance to an axis-aligned box (used as a conservative bound for skipped groups). */
static inline double sdAabb (V3 p, V3 lo, V3 hi)
{
    V3 c = (lo + hi) * 0.5, e = (hi - lo) * 0.5;
    V3 q { std::fabs (p.x - c.x) - e.x, std::fabs (p.y - c.y) - e.y, std::fabs (p.z - c.z) - e.z };
    return len ({ std::max (q.x, 0.0), std::max (q.y, 0.0), std::max (q.z, 0.0) }) + std::min (std::max (q.x, std::max (q.y, q.z)), 0.0);
}
static inline void un (Hit& h, double d, int m) { if (d < h.d) { h.d = d; h.m = m; } }

static Hit map (V3 p)
{
    using namespace geo;
    Hit h { 1e9, M_NONE };

    // ---- cabinet group bounding
    const double bCab = sdAabb (p, { -66, -1, -24 }, { 66, 30, 19 });
    if (bCab > 0.5) un (h, bCab, M_NONE);
    else
    {
        V3 c = p - V3 { 0, cabCY, 0 };
        un (h, sdRoundBox (c, { cabHX, cabHY, cabHZ }, cabR), M_LACQUER);

        // brushed aluminium front panel (slightly proud of the lacquer)
        un (h, sdRoundBox (p - V3 { 0, cabCY - 0.6, frontZ - 0.25 }, { cabHX - 6.0, cabHY - 3.0, 0.7 }, 0.6), M_PANEL);

        // oxblood lacquer inlay along the top front edge + chrome trim under it
        un (h, sdRoundBox (p - V3 { 0, cabCY + cabHY - 0.05, frontZ + 3.4 }, { cabHX - 4.0, 0.42, 1.6 }, 0.35), M_RED);
        un (h, sdRoundBox (p - V3 { 0, cabCY + cabHY - 2.1, frontZ - 0.05 }, { cabHX - 5.0, 0.22, 0.35 }, 0.2), M_CHROME);

        // display: chrome bezel + black glass
        V3 dp = p - V3 { 0, knobY, frontZ - 1.05 };
        un (h, sdRoundBox (dp, { dispHX + 0.9, dispHY + 0.9, 0.35 }, 0.9), M_BEZEL);
        un (h, sdRoundBox (dp - V3 { 0, 0, -0.2 }, { dispHX, dispHY, 0.3 }, 0.6), M_GLASS);

        // knobs
        for (double kx : knobX)
            {
                const double bk = sdAabb (p, { kx - knobR - 1, knobY - knobR - 1, frontZ - 6 }, { kx + knobR + 1, knobY + knobR + 1, frontZ + 0.5 });
                if (bk > 0.3) { un (h, bk, M_NONE); continue; }
                V3 q = p - V3 { kx, knobY, frontZ - 0.9 - knobDepth * 0.5 };
                const double a = std::atan2 (q.y, q.x);
                const double side = clamp01 ((std::fabs (q.z) < knobDepth * 0.5 - 0.35) ? 1.0 : 0.0);
                const double knurl = 0.055 * std::cos (a * 60.0) * side;
                un (h, sdCylZ (q, knobR + knurl, knobDepth * 0.5, 0.35) * 0.85, M_SKIRT);
                V3 cq = q - V3 { 0, 0, -knobDepth * 0.5 - 0.25 };
                un (h, sdCylZ (cq, capR, 0.32, 0.22), M_CAP);
            }

        // chrome feet
        for (double fx : { -52.0, 52.0 })
            for (double fz : { -9.0, 9.0 })
                un (h, sdCylY (p - V3 { fx, 2.2, fz }, 1.8, 2.2, 0.6), M_CHROME);
    }

    // ---- pitch antenna: collar + rod
    const double bRod = sdAabb (p, { rodX - 5, 15, -5 }, { rodX + 5, rodTop + 2, 5 });
    if (bRod > 0.5) un (h, bRod, M_NONE);
    else
    {
        V3 c = p - V3 { rodX, rodBase + 2.2, 0 };
        un (h, sdCylY (c, 2.6, 2.4, 0.8), M_CHROME);
        un (h, sdCylY (c - V3 { 0, -1.4, 0 }, 2.75, 0.45, 0.2), M_RED);         // red lacquer ring
        un (h, sdCylY (c - V3 { 0, 3.0, 0 }, 1.1, 1.2, 0.4), M_CHROME);          // taper
        un (h, sdCapsuleY (p - V3 { rodX, 0, 0 }, rodBase + 4.0, rodTop, rodR), M_CHROME);
    }

    // ---- volume antenna: arm + horizontal stadium loop
    const double bLoop = sdAabb (p, { 56, loopCY - 5, -loopRad - 2 }, { geo::loopCX + loopL + loopRad + 2, loopCY + 5, loopRad + 2 });
    if (bLoop > 0.5) un (h, bLoop, M_NONE);
    else
    {
        // mounting arm out of the cabinet end
        const double ax = std::clamp (p.x, 58.0, geo::loopCX - loopL - loopRad);
        un (h, len ({ p.x - ax, p.y - loopCY, p.z }) - 0.9, M_CHROME);
        un (h, sdCylY (V3 { p.y - loopCY, p.x - 61.5, p.z }, 1.5, 1.6, 0.4), M_CHROME);        // collar (axis along x)

        // stadium ring in the xz plane
        const double qx = std::max (std::fabs (p.x - geo::loopCX) - loopL, 0.0), qz = p.z;
        const double dl = std::sqrt (qx * qx + qz * qz) - loopRad;
        un (h, std::sqrt (dl * dl + (p.y - loopCY) * (p.y - loopCY)) - loopTube, M_CHROME);
    }
    return h;
}

static V3 normalAt (V3 p)
{
    const double e = 0.004;
    return norm ({ map (p + V3 { e, 0, 0 }).d - map (p - V3 { e, 0, 0 }).d,
                   map (p + V3 { 0, e, 0 }).d - map (p - V3 { 0, e, 0 }).d,
                   map (p + V3 { 0, 0, e }).d - map (p - V3 { 0, 0, e }).d });
}

static bool march (V3 ro, V3 rd, double tMax, double& t, int& m)
{
    t = 0.02;
    for (int i = 0; i < 220 && t < tMax; ++i)
    {
        Hit h = map (ro + rd * t);
        if (h.d < 0.0015 * (1.0 + t * 0.01) && h.m != M_NONE) { m = h.m; return true; }
        t += std::max (h.d, 0.002);
    }
    return false;
}

// ----------------------------------------------------------------------------- lights (rect area lights)
struct RectLight { V3 c, u, v, n; V3 rad; };
static std::vector<RectLight> lights;

static void setupLights()
{
    // huge overhead softbox: top surfaces of the lacquer and the loop reflect it
    lights.push_back ({ { 20, 300, 40 }, { 230, 0, 0 }, { 0, 0, 190 }, { 0, -1, 0 }, { 4.2, 4.15, 4.05 } });
    // high front softbox: the front panel / display glass reflect a soft gradient of it
    lights.push_back ({ { 20, 330, -330 }, { 210, 0, 0 }, { 0, 70, 30 }, norm ({ 0, -0.75, 0.66 }), { 1.6, 1.6, 1.65 } });
    // tall strip boxes left/right, slightly in front (long vertical highlights on the chrome rod)
    lights.push_back ({ { -320, 130, -110 }, { 0, 0, 45 }, { 0, 140, 0 }, norm ({ 1, 0, 0.35 }), { 3.4, 3.4, 3.5 } });
    lights.push_back ({ { 360, 130, -110 }, { 0, 0, 45 }, { 0, 140, 0 }, norm ({ -1, 0, 0.35 }), { 2.8, 2.9, 3.1 } });
    // invisible white bounce card lying in front of the instrument: lights the knob caps,
    // display glass and brushed panel from below, exactly like a product photographer would
    lights.push_back ({ { 20, 0.6, -75 }, { 120, 0, 0 }, { 0, 0, 32 }, { 0, 1, 0 }, { 0.55, 0.55, 0.57 } });
    // low wide back panel: edge rim on the cabinet top and gradient in the glossy floor
    lights.push_back ({ { 20, 120, 420 }, { 320, 0, 0 }, { 0, 60, 0 }, { 0, 0, -1 }, { 0.9, 0.9, 0.95 } });
}

/** Radiance of the first light rect hit within tMax (only visible in reflections). */
static bool lightHit (V3 ro, V3 rd, double tMax, V3& out)
{
    double best = tMax; bool found = false;
    for (const auto& L : lights)
    {
        const double dn = dot (rd, L.n);
        if (dn >= 0) continue;
        const double t = dot (L.c - ro, L.n) / dn;
        if (t <= 0 || t >= best) continue;
        V3 p = ro + rd * t - L.c;
        const double a = dot (p, L.u) / dot (L.u, L.u), b = dot (p, L.v) / dot (L.v, L.v);
        if (std::fabs (a) < 1 && std::fabs (b) < 1)
        {
            const double edge = clamp01 ((1 - std::fabs (a)) * 8) * clamp01 ((1 - std::fabs (b)) * 8);
            const double grad = 0.30 + 0.70 * clamp01 (0.5 - 0.5 * b);
            out = L.rad * ((0.35 + 0.65 * edge) * grad); best = t; found = true;
        }
    }
    return found;
}

/** Radiance seen along a ray that leaves the scene. Camera rays never see the lights. */
static V3 environment (V3 ro, V3 rd, int depth = 1)
{
    V3 lr;
    if (depth > 0 && lightHit (ro, rd, 1e9, lr)) return lr;
    for (const auto& L : lights)
    {
        if (depth == 0) break;
        const double dn = dot (rd, L.n);
        if (dn >= 0) continue;
        const double t = dot (L.c - ro, L.n) / dn;
        if (t <= 0) continue;
        V3 p = ro + rd * t - L.c;
        const double a = dot (p, L.u) / dot (L.u, L.u), b = dot (p, L.v) / dot (L.v, L.v);
        if (std::fabs (a) < 1 && std::fabs (b) < 1)
        {
            const double edge = clamp01 ((1 - std::fabs (a)) * 8) * clamp01 ((1 - std::fabs (b)) * 8);  // soft edges
            return L.rad * (0.35 + 0.65 * edge);
        }
    }
    // dark studio + faint oxblood glow low behind the instrument (signature reflection)
    const double base = 0.004 + 0.008 * clamp01 (rd.y * 0.5 + 0.5);
    V3 c { base, base, base * 1.05 };
    if (rd.z > 0.0)      // soft charcoal glow on the seamless backdrop behind the instrument
    {
        const double g = std::exp (-(rd.x - 0.02) * (rd.x - 0.02) / 0.12 - (rd.y - 0.06) * (rd.y - 0.06) / 0.06);
        c = c + V3 { 0.016, 0.016, 0.018 } * g;
    }
    if (depth > 0 && rd.z > 0.5 && rd.y < 0.10 && rd.y > -0.05)
    {
        const double g = std::exp (-std::pow ((rd.y - 0.03) / 0.03, 2.0)) * clamp01 ((rd.z - 0.5) * 2);
        c = c + V3 { 0.12, 0.008, 0.012 } * g;
    }
    return c;
}

// ----------------------------------------------------------------------------- shading
struct Rng
{
    std::mt19937 g;
    std::uniform_real_distribution<double> u { 0.0, 1.0 };
    explicit Rng (unsigned s) : g (s) {}
    double operator()() { return u (g); }
};

static bool occluded (V3 p, V3 dir, double dist)
{
    double t; int m;
    if (march (p, dir, dist, t, m)) return true;
    return false;
}

static V3 directDiffuse (V3 p, V3 n, Rng& rng)
{
    V3 sum { 0, 0, 0 };
    for (const auto& L : lights)
    {
        V3 lp = L.c + L.u * (rng() * 2 - 1) + L.v * (rng() * 2 - 1);
        V3 d = lp - p;
        const double dist = len (d);
        d = d * (1.0 / dist);
        const double cs = dot (n, d), cl = -dot (L.n, d);
        if (cs <= 0 || cl <= 0) continue;
        const double area = 4.0 * len (L.u) * len (L.v);
        if (occluded (p + n * 0.02, d, dist)) continue;
        sum = sum + L.rad * (cs * cl * area / (dist * dist) / 3.14159265);
    }
    return sum;
}

static double ambientOcclusion (V3 p, V3 n)
{
    double occ = 0, w = 1;
    for (int i = 1; i <= 5; ++i)
    {
        const double h = 0.25 * i;
        occ += (h - map (p + n * h).d) * w;
        w *= 0.6;
    }
    return clamp01 (1.0 - 1.4 * occ);
}

static V3 jitterDir (V3 r, double rough, Rng& rng)
{
    if (rough <= 0) return r;
    V3 a = std::fabs (r.x) < 0.9 ? V3 { 1, 0, 0 } : V3 { 0, 1, 0 };
    V3 t = norm (cross (r, a)), b = cross (r, t);
    const double phi = 6.2831853 * rng(), rr = rough * std::sqrt (rng());
    return norm (r + t * (std::cos (phi) * rr) + b * (std::sin (phi) * rr));
}

static double schlick (double f0, double c) { return f0 + (1 - f0) * std::pow (1 - clamp01 (c), 5.0); }

static V3 trace (V3 ro, V3 rd, int depth, Rng& rng);

static V3 floorShade (V3 ro, V3 rd, double tF, int depth, Rng& rng)
{
    V3 p = ro + rd * tF;
    V3 n { 0, 1, 0 };
    const double fall = std::exp (-(p.x - 20) * (p.x - 20) / (2 * 95.0 * 95.0) - (p.z - 5) * (p.z - 5) / (2 * 45.0 * 45.0));
    V3 diff = directDiffuse (p, n, rng) * 0.006;
    const double ao = ambientOcclusion (p, n);
    V3 col = (diff + V3 { 0.004, 0.004, 0.004 }) * ao;
    if (depth < 2)
    {
        V3 r = jitterDir (reflect (rd, n), 0.10, rng);
        const double F = schlick (0.04, dot (r, n));
        col = col + trace (p + n * 0.02, r, depth + 1, rng) * (F * 0.9);
    }
    col = col * (0.25 + 0.75 * fall);
    // seamless studio: distant floor melts into the backdrop (no visible horizon)
    const double dist = len (p - ro);
    const double blend = clamp01 ((dist - 230.0) / 260.0);
    return mixv (col, environment (ro, { rd.x, std::fabs (rd.y) * 0.3, rd.z }, 0), blend);
}

static V3 trace (V3 ro, V3 rd, int depth, Rng& rng)
{
    const double tFloor = rd.y < 0 ? -ro.y / rd.y : 1e9;
    double t; int m;
    const bool hit = march (ro, rd, std::min (tFloor, 900.0), t, m);
    if (! hit)
    {
        V3 lr;
        if (depth > 0 && lightHit (ro, rd, std::min (tFloor, 1e9), lr)) return lr;   // bounce card etc.
        if (tFloor < 1e8) return floorShade (ro, rd, tFloor, depth, rng);
        return environment (ro, rd, depth);
    }

    V3 p = ro + rd * t;
    V3 n = normalAt (p);
    if (dot (n, rd) > 0) n = n * -1.0;
    const double cosv = -dot (rd, n);

    // material parameters
    V3 albedo { 0, 0, 0 };
    V3 f0 { 0.04, 0.04, 0.04 };
    double rough = 0.0, coat = 0.0; bool metal = false;
    switch (m)
    {
        case M_LACQUER: albedo = { 0.012, 0.012, 0.013 }; coat = 0.05; rough = 0.015; break;
        case M_RED:     albedo = { 0.30, 0.012, 0.02 };   coat = 0.05; rough = 0.015; break;
        case M_CHROME:  metal = true; f0 = { 0.93, 0.93, 0.95 }; rough = 0.012; break;
        case M_BEZEL:   metal = true; f0 = { 0.90, 0.90, 0.92 }; rough = 0.03; break;
        case M_CAP:     metal = true; f0 = { 0.86, 0.86, 0.88 }; rough = 0.10; break;
        case M_PANEL:   albedo = { 0.020, 0.020, 0.022 }; metal = false; coat = 0.12; rough = 0.30; break;
        case M_SKIRT:   albedo = { 0.018, 0.018, 0.019 }; coat = 0.06; rough = 0.25; break;
        case M_GLASS:   albedo = { 0.002, 0.002, 0.003 }; coat = 0.045; rough = 0.0; break;
        default: break;
    }

    // brushed directions: concentric on knob caps, horizontal on the panel (fake anisotropy by jitter)
    V3 spec { 0, 0, 0 };
    if (depth < 3)
    {
        V3 r = reflect (rd, n);
        if (m == M_PANEL) r = norm (r + V3 { (rng() - 0.5) * 0.9, (rng() - 0.5) * 0.08, 0 });
        else r = jitterDir (r, rough, rng);
        if (dot (r, n) < 0) r = reflect (r, n);
        spec = trace (p + n * 0.02, r, depth + 1, rng);
    }
    else spec = environment (p, reflect (rd, n), depth + 1);

    if (metal)
    {
        V3 F { schlick (f0.x, cosv), schlick (f0.y, cosv), schlick (f0.z, cosv) };
        const double ao = ambientOcclusion (p, n);
        return spec * F * (0.4 + 0.6 * ao);
    }

    const double ao = ambientOcclusion (p, n);
    V3 diff = (directDiffuse (p, n, rng) + V3 { 0.01, 0.01, 0.01 }) * albedo * ao;
    const double F = schlick (coat, cosv);
    return diff * (1 - F) + spec * F;
}

// ----------------------------------------------------------------------------- camera
struct Cam
{
    V3 pos, fwd, right, up; double tanHalf, aspect;
    Cam (V3 p, V3 target, double fovDeg, double asp) : pos (p), aspect (asp)
    {
        fwd = norm (target - p);
        right = norm (cross ({ 0, 1, 0 }, fwd));
        up = cross (fwd, right);
        tanHalf = std::tan (fovDeg * 3.14159265 / 360.0);
    }
    V3 ray (double sx, double sy) const    // sx, sy in [-1,1]
    {
        return norm (fwd + right * (sx * tanHalf * aspect) + up * (sy * tanHalf));
    }
    void project (V3 P, int W, int H, double& px, double& py) const
    {
        V3 v = P - pos;
        const double z = dot (v, fwd);
        px = (dot (v, right) / z / (tanHalf * aspect) + 1) * 0.5 * W;
        py = (1 - dot (v, up) / z / tanHalf) * 0.5 * H;
    }
};

static inline double aces (double x)
{
    const double a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp01 ((x * (a * x + b)) / (x * (c * x + d) + e));
}

int main (int argc, char** argv)
{
    const char* out = argc > 1 ? argv[1] : "render.ppm";
    const char* coordsPath = argc > 2 ? argv[2] : "coords.txt";
    const int W = argc > 3 ? std::atoi (argv[3]) : 1440;
    const int H = argc > 4 ? std::atoi (argv[4]) : 900;
    const int spp = argc > 5 ? std::atoi (argv[5]) : 24;

    setupLights();
    if (std::getenv ("AV_DEBUG"))
    {
        const Cam dc ({ 22, 96, -215 }, { 22, 50, 0 }, 33.0, 1.6);
        for (double sy : { 0.95, 0.80, 0.70, 0.5 })
        {
            V3 rd = dc.ray (0.0, sy);
            double t; int m; bool hit = march (dc.pos, rd, 900, t, m);
            Rng r (1); V3 c = trace (dc.pos, rd, 0, r);
            std::printf ("sy %.2f rd.y %.3f hit %d m %d t %.1f col %.4f\n", sy, rd.y, hit, m, t, c.x);
        }
        return 0;
    }
    const Cam cam ({ 22, 96, -215 }, { 22, 50, 0 }, 33.0, (double) W / H);

    std::vector<float> img ((size_t) W * H * 3, 0.0f);
    std::atomic<int> nextRow { 0 };
    const int threads = std::max (1u, std::thread::hardware_concurrency());

    auto worker = [&] (int id)
    {
        Rng rng (1234u + (unsigned) id * 7919u);
        for (int y; (y = nextRow++) < H;)
        {
            for (int x = 0; x < W; ++x)
            {
                V3 acc { 0, 0, 0 };
                const int s1 = (int) std::sqrt ((double) spp);
                for (int s = 0; s < spp; ++s)
                {
                    const double jx = ((s % s1) + rng()) / s1, jy = ((s / s1) % s1 + rng()) / s1;
                    const double sx = (2.0 * (x + jx) / W - 1.0), sy = (1.0 - 2.0 * (y + jy) / H);
                    acc = acc + trace (cam.pos, cam.ray (sx, sy), 0, rng);
                }
                acc = acc * (1.0 / spp);
                // vignette
                const double vx = (x - W * 0.5) / (W * 0.5), vy = (y - H * 0.47) / (H * 0.5);
                const double vig = 1.0 - 0.38 * std::pow (std::min (1.0, (vx * vx + vy * vy) * 0.55), 1.4);
                const size_t i = ((size_t) y * W + x) * 3;
                img[i] = (float) (acc.x * vig); img[i + 1] = (float) (acc.y * vig); img[i + 2] = (float) (acc.z * vig);
            }
            if (id == 0 && y % 40 == 0) { std::fprintf (stderr, "\rrow %d/%d", y, H); std::fflush (stderr); }
        }
    };
    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back (worker, i);
    for (auto& t : pool) t.join();
    std::fprintf (stderr, "\n");

    // tone map + gamma, write PPM
    const double exposure = 1.9;
    FILE* f = std::fopen (out, "wb");
    std::fprintf (f, "P6\n%d %d\n255\n", W, H);
    for (size_t i = 0; i < img.size(); ++i)
    {
        const double v = std::pow (aces (img[i] * exposure), 1.0 / 2.2);
        std::fputc ((int) std::lround (clamp01 (v) * 255.0), f);
    }
    std::fclose (f);

    // ---- projected coordinates of interactive features (pixels)
    using namespace geo;
    FILE* c = std::fopen (coordsPath, "w");
    double px, py, qx, qy;
    cam.project ({ rodX, rodTop, 0 }, W, H, px, py);              std::fprintf (c, "rodTop %.1f %.1f\n", px, py);
    cam.project ({ rodX, rodBase + 6, 0 }, W, H, px, py);         std::fprintf (c, "rodBottom %.1f %.1f\n", px, py);
    cam.project ({ loopCX, loopCY, 0 }, W, H, px, py);            std::fprintf (c, "loopCentre %.1f %.1f\n", px, py);
    cam.project ({ loopCX - loopL - loopRad, loopCY, 0 }, W, H, px, py); std::fprintf (c, "loopLeft %.1f %.1f\n", px, py);
    cam.project ({ loopCX + loopL + loopRad, loopCY, 0 }, W, H, px, py); std::fprintf (c, "loopRight %.1f %.1f\n", px, py);
    cam.project ({ loopCX, loopCY, -loopRad }, W, H, px, py);     std::fprintf (c, "loopFront %.1f %.1f\n", px, py);
    cam.project ({ loopCX, loopCY, loopRad }, W, H, px, py);      std::fprintf (c, "loopBack %.1f %.1f\n", px, py);
    for (int k = 0; k < 6; ++k)
    {
        const double zc = frontZ - 0.9 - knobDepth - 0.6;
        cam.project ({ knobX[k], knobY, zc }, W, H, px, py);
        cam.project ({ knobX[k] + capR, knobY, zc }, W, H, qx, qy);
        std::fprintf (c, "knob%d %.1f %.1f %.1f\n", k, px, py, qx - px);
    }
    cam.project ({ -dispHX, knobY + dispHY, frontZ - 1.5 }, W, H, px, py);
    cam.project ({ dispHX, knobY - dispHY, frontZ - 1.5 }, W, H, qx, qy);
    std::fprintf (c, "display %.1f %.1f %.1f %.1f\n", px, py, qx, qy);
    cam.project ({ 0, cabCY - 6.6, frontZ - 1.0 }, W, H, px, py); std::fprintf (c, "wordmark %.1f %.1f\n", px, py);
    std::fclose (c);
    return 0;
}
