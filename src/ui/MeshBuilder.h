// CPU-side builders for flat-shaded low-poly meshes and unlit line / point geometry.
#pragma once

#include "Math3d.h"

#include <vector>

namespace tw {

// Lit vertex: position, normal, color, emissive color, emissive channel.
struct LitVertex {
    float p[3], n[3], c[3], e[3], ch;
};

// Emissive channels: their strength is set per frame by the scene.
enum EmissiveChannel { kChNone = 0, kChNight = 1, kChWindows = 2, kChSteel = 3, kChDragon = 4, kChConst = 5, kNumChannels = 6 };

class MeshBuilder {
public:
    std::vector<LitVertex> v;

    void color(uint32_t hex) { col_ = rgb(hex); }
    void color(Vec3 c) { col_ = c; }
    void emissive(uint32_t hex, int channel) { emi_ = rgb(hex); ch_ = channel; }
    void noEmissive() { emi_ = Vec3(); ch_ = kChNone; }

    void tri(Vec3 a, Vec3 b, Vec3 c);
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d) { tri(a, b, c); tri(a, c, d); }
    // Unit cube centered on the origin, transformed.
    void box(const Mat4& xf);
    // Three.js-style cylinder: centered on the origin along Y.
    void cylinder(const Mat4& xf, float rTop, float rBottom, float height, int segments, bool openEnded = false);
    void cone(const Mat4& xf, float radius, float height, int segments) { cylinder(xf, 0.f, radius, height, segments); }
    // Cylinder between two points (tower beams).
    void beam(Vec3 a, Vec3 b, float r, int segments = 5);
    void icosahedron(const Mat4& xf, float radius, int detail);
    void octahedron(const Mat4& xf, float radius);

    size_t size() const { return v.size(); }

private:
    Vec3 col_{1, 1, 1}, emi_{};
    int ch_ = kChNone;
};

// Unlit vertex for lines, points, glow geometry.
struct LineVertex {
    float p[3], c[4];
};

class LineBuilder {
public:
    std::vector<LineVertex> v;
    void add(Vec3 p, Vec3 c, float a = 1.f) { v.push_back({{p.x, p.y, p.z}, {c.x, c.y, c.z, a}}); }
    void line(Vec3 a, Vec3 b, Vec3 c, float alpha = 1.f) { add(a, c, alpha); add(b, c, alpha); }
};

// Icosphere edges (wireframe shells, limiter bubble), as line pairs, unit radius.
void icosphereEdges(LineBuilder& out, int detail, Vec3 color);
// Triangles of a unit icosphere (flat), used by other builders.
void icosphereTriangles(std::vector<Vec3>& tris, int detail);

} // namespace tw
