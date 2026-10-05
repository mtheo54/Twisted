#include "MeshBuilder.h"

#include <map>
#include <utility>

namespace tw {

void MeshBuilder::tri(Vec3 a, Vec3 b, Vec3 c) {
    Vec3 n = cross(b - a, c - a).normalized();
    for (const Vec3& p : {a, b, c})
        v.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {col_.x, col_.y, col_.z}, {emi_.x, emi_.y, emi_.z}, (float)ch_});
}

void MeshBuilder::box(const Mat4& xf) {
    Vec3 p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = xf.transformPoint(Vec3((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f));
    // faces wound counter-clockwise seen from outside
    quad(p[0], p[4], p[6], p[2]);  // -x
    quad(p[1], p[3], p[7], p[5]);  // +x
    quad(p[0], p[1], p[5], p[4]);  // -y
    quad(p[2], p[6], p[7], p[3]);  // +y
    quad(p[0], p[2], p[3], p[1]);  // -z
    quad(p[4], p[5], p[7], p[6]);  // +z
}

void MeshBuilder::cylinder(const Mat4& xf, float rTop, float rBottom, float height, int seg, bool openEnded) {
    const float h = height * 0.5f;
    auto ring = [&](float r, float y, int i) {
        const float th = (float)i / seg * 6.2831853f;
        return xf.transformPoint(Vec3(r * std::sin(th), y, r * std::cos(th)));
    };
    const Vec3 top = xf.transformPoint(Vec3(0, h, 0)), bot = xf.transformPoint(Vec3(0, -h, 0));
    for (int i = 0; i < seg; ++i) {
        const Vec3 b0 = ring(rBottom, -h, i), b1 = ring(rBottom, -h, i + 1);
        const Vec3 t0 = ring(rTop, h, i), t1 = ring(rTop, h, i + 1);
        if (rTop > 0.f) quad(b0, b1, t1, t0);
        else tri(b0, b1, top);
        if (!openEnded) {
            if (rTop > 0.f) tri(top, t0, t1);
            if (rBottom > 0.f) tri(bot, b1, b0);
        }
    }
}

void MeshBuilder::beam(Vec3 a, Vec3 b, float r, int seg) {
    const Vec3 d = b - a;
    const float len = d.length();
    // rotation taking +Y to d
    Vec3 y = d.normalized();
    Vec3 x = std::fabs(y.y) < 0.99f ? cross(y, Vec3(0, 1, 0)).normalized() : Vec3(1, 0, 0);
    Vec3 z = cross(x, y);
    Mat4 m = Mat4::identity();
    m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
    m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
    m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    const Vec3 mid = a + d * 0.5f;
    m.m[12] = mid.x; m.m[13] = mid.y; m.m[14] = mid.z;
    cylinder(m, r, r, len, seg, true);
}

void icosphereTriangles(std::vector<Vec3>& tris, int detail) {
    const float t = (1.f + std::sqrt(5.f)) / 2.f;
    std::vector<Vec3> verts = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                               {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    const int idx[60] = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                         3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    tris.clear();
    for (int i = 0; i < 60; ++i) tris.push_back(verts[idx[i]].normalized());
    for (int d = 0; d < detail; ++d) {
        std::vector<Vec3> next;
        for (size_t i = 0; i < tris.size(); i += 3) {
            const Vec3 a = tris[i], b = tris[i + 1], c = tris[i + 2];
            const Vec3 ab = ((a + b) * 0.5f).normalized(), bc = ((b + c) * 0.5f).normalized(), ca = ((c + a) * 0.5f).normalized();
            for (const Vec3& p : {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca}) next.push_back(p);
        }
        tris.swap(next);
    }
}

void MeshBuilder::icosahedron(const Mat4& xf, float radius, int detail) {
    std::vector<Vec3> t;
    icosphereTriangles(t, detail);
    for (size_t i = 0; i < t.size(); i += 3)
        tri(xf.transformPoint(t[i] * radius), xf.transformPoint(t[i + 1] * radius), xf.transformPoint(t[i + 2] * radius));
}

void MeshBuilder::octahedron(const Mat4& xf, float r) {
    const Vec3 px(r, 0, 0), nx(-r, 0, 0), py(0, r, 0), ny(0, -r, 0), pz(0, 0, r), nz(0, 0, -r);
    const Vec3 f[8][3] = {{px, py, pz}, {pz, py, nx}, {nx, py, nz}, {nz, py, px},
                          {px, pz, ny}, {pz, nx, ny}, {nx, nz, ny}, {nz, px, ny}};
    for (auto& tr : f) tri(xf.transformPoint(tr[0]), xf.transformPoint(tr[1]), xf.transformPoint(tr[2]));
}

void icosphereEdges(LineBuilder& out, int detail, Vec3 color) {
    std::vector<Vec3> t;
    icosphereTriangles(t, detail);
    // deduplicate edges by quantized endpoints
    std::map<std::pair<long, long>, bool> seen;
    auto key = [](Vec3 p) { return (long)std::lround(p.x * 1000) * 4000003L + (long)std::lround(p.y * 1000) * 2003L + (long)std::lround(p.z * 1000); };
    for (size_t i = 0; i < t.size(); i += 3)
        for (int e = 0; e < 3; ++e) {
            Vec3 a = t[i + e], b = t[i + (e + 1) % 3];
            long ka = key(a), kb = key(b);
            auto k = ka < kb ? std::make_pair(ka, kb) : std::make_pair(kb, ka);
            if (seen.count(k)) continue;
            seen[k] = true;
            out.line(a, b, color);
        }
}

} // namespace tw
