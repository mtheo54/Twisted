#include "Scene.h"

#include "Gl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace tw {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTop = 48.f, kAnt0 = 48.f, kAnt1 = 68.f, kAntH = kAnt1 - kAnt0;
constexpr int kEmbers = 420, kStars = 700, kBirds = 40, kDragonSegs = 64;
const int kBirdsByOs[5] = {0, 6, 14, 26, 40};
const float kDragonSize[5] = {0, 0.5f, 0.75f, 1.0f, 1.4f};
const int kDragonSegsByOs[5] = {0, 26, 38, 50, 64};

float clamp01(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
float approach(float cur, float target, float rate, float dt) { return cur + (target - cur) * std::min(1.f, dt * rate); }
float randf() { return (float)std::rand() / (float)RAND_MAX; }

// ---- shaders (GLSL 1.50, OpenGL 3.2 core: works on Windows, macOS and Linux) -------------
const char* kGrade = R"(
uniform float uBright;
uniform float uDesat;
// lighting happens in linear space; enc() converts to display gamma before the fog,
// so fog and sky colors are used exactly as written (like the prototype)
vec3 enc(vec3 c) { return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2)); }
vec3 grade(vec3 c) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    return mix(c, vec3(l), uDesat) * uBright;
}
)";

const char* kLitVs = R"(#version 150
in vec3 aPos; in vec3 aNormal; in vec3 aColor; in vec3 aEmis; in float aCh;
uniform mat4 uViewProj; uniform mat4 uModel;
out vec3 vN; out vec3 vColor; out vec3 vEmis; out float vCh; out vec3 vWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz; vN = mat3(uModel) * aNormal; vColor = aColor; vEmis = aEmis; vCh = aCh;
    gl_Position = uViewProj * w;
}
)";

std::string litFs() {
    return std::string("#version 150\n") + kGrade + R"(
in vec3 vN; in vec3 vColor; in vec3 vEmis; in float vCh; in vec3 vWorld;
out vec4 fragColor;
uniform vec3 uSunDir; uniform vec3 uSunColor; uniform vec3 uHemiSky; uniform vec3 uHemiGround;
uniform vec3 uRimDir; uniform vec3 uRimColor;
uniform vec3 uP0Pos; uniform vec4 uP0Col; uniform vec3 uP1Pos; uniform vec4 uP1Col;
uniform vec3 uFogColor; uniform float uFogNear; uniform float uFogFar; uniform vec3 uCamPos;
uniform float uCh[6]; uniform vec3 uColorMul; uniform float uClipY;
vec3 pointLight(vec3 n, vec3 pos, vec4 col) {
    vec3 d = pos - vWorld; float l = length(d);
    float att = max(1.0 - l / col.w, 0.0);
    return col.rgb * max(dot(n, d / max(l, 0.001)), 0.0) * att * att;
}
void main() {
    if (vWorld.y > uClipY) discard;   // tower under construction: only what is built so far
    vec3 n = normalize(vN);
    if (dot(n, uCamPos - vWorld) < 0.0) n = -n;   // two-sided flat shading
    vec3 base = vColor * uColorMul;
    vec3 light = mix(uHemiGround, uHemiSky, n.y * 0.5 + 0.5)
               + uSunColor * max(dot(n, uSunDir), 0.0)
               + uRimColor * max(dot(n, uRimDir), 0.0)
               + pointLight(n, uP0Pos, uP0Col) + pointLight(n, uP1Pos, uP1Col);
    vec3 col = base * light + vEmis * uCh[int(vCh + 0.5)];
    float fog = clamp((length(vWorld - uCamPos) - uFogNear) / (uFogFar - uFogNear), 0.0, 1.0);
    fragColor = vec4(grade(mix(enc(col), uFogColor, fog)), 1.0);
}
)";
}

const char* kLineVs = R"(#version 150
in vec3 aPos; in vec4 aColor;
uniform mat4 uViewProj; uniform mat4 uModel; uniform float uPointSize; uniform vec3 uCamPos;
out vec4 vC; out float vDist;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vC = aColor; vDist = length(w.xyz - uCamPos);
    gl_Position = uViewProj * w;
    gl_PointSize = uPointSize;
}
)";

std::string lineFs() {
    return std::string("#version 150\n") + kGrade + R"(
in vec4 vC; in float vDist; out vec4 fragColor;
uniform vec4 uColor; uniform int uRound;
uniform vec3 uFogColor; uniform float uFogNear; uniform float uFogFar; uniform float uFogAmt;
void main() {
    vec4 c = vC * uColor;
    if (uRound == 1) {
        vec2 q = gl_PointCoord * 2.0 - 1.0; float r = dot(q, q);
        if (r > 1.0) discard;
        c.a *= 1.0 - r * 0.6;
    }
    float fog = clamp((vDist - uFogNear) / (uFogFar - uFogNear), 0.0, 1.0) * uFogAmt;
    fragColor = vec4(grade(mix(enc(c.rgb), uFogColor, fog)), c.a);
}
)";
}

const char* kSpriteVs = R"(#version 150
in vec2 aCorner;
uniform mat4 uViewProj; uniform vec3 uCenter; uniform vec2 uSize; uniform vec3 uRight; uniform vec3 uUp;
out vec2 vUv;
void main() {
    vUv = aCorner;
    vec3 p = uCenter + uRight * aCorner.x * uSize.x + uUp * aCorner.y * uSize.y;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

std::string spriteFs() {
    return std::string("#version 150\n") + kGrade + R"(
in vec2 vUv; out vec4 fragColor;
uniform vec4 uColor; uniform int uMode;
void main() {
    float r = length(vUv);
    if (r > 1.0) discard;
    float a = uMode == 1 ? 1.0 - smoothstep(0.92, 1.0, r) : (r < 0.25 ? 1.0 : pow(1.0 - (r - 0.25) / 0.75, 2.0));
    fragColor = vec4(grade(enc(uColor.rgb)), uColor.a * a);
}
)";
}

const char* kSkyVs = R"(#version 150
in vec2 aCorner; out vec2 vNdc;
void main() { vNdc = aCorner; gl_Position = vec4(aCorner, 0.0, 1.0); }
)";
const char* kSkyFs = R"(#version 150
in vec2 vNdc; out vec4 fragColor;
uniform mat4 uInvViewProj; uniform vec3 uCamPos; uniform vec3 uTop; uniform vec3 uBottom;
uniform float uBright; uniform float uDesat;
void main() {
    vec4 p = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(p.xyz / p.w - uCamPos);
    float k = smoothstep(-0.05, 0.55, dir.y);
    vec3 c = mix(uBottom, uTop, k);
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    fragColor = vec4(mix(c, vec3(l), uDesat) * uBright, 1.0);
}
)";

struct SkyKey { float t; uint32_t top, bot, fog; float hemi, sun; uint32_t sunC, orb; };
const SkyKey kSky[4] = {
    {0.0f, 0x7fa6c4, 0xd9d4c4, 0xbfc3c0, 0.9f, 1.35f, 0xfff3e0, 0xfff6e4},
    {0.45f, 0x45507a, 0xd78a5a, 0x8a6a6c, 0.6f, 0.9f, 0xffa070, 0xffb27a},
    {0.7f, 0x1c1834, 0x5a2c40, 0x3a2433, 0.35f, 0.35f, 0xc59ad0, 0xe8d8e6},
    {1.0f, 0x07060f, 0x1d1426, 0x15121f, 0.22f, 0.22f, 0x9ab8e8, 0xe9ecf2},
};

Vec3 dragonPath(float th) {
    const float r = 30.f + std::sin(th * 1.7f) * 9.f;
    return {std::cos(th) * r, 40.f + std::sin(th * 2.3f) * 10.f + std::sin(th * 7.f) * 2.f, std::sin(th) * r};
}
} // namespace

// ============================================================================================
// Geometry
// ============================================================================================
void Scene::buildGeometry() {
    MeshBuilder M;
    LineBuilder L;
    Rng R(1337);
    auto beginM = [&] { Range r; r.first = (int)M.size(); return r; };
    auto endM = [&](Range& r) { r.count = (int)M.size() - r.first; };
    auto beginL = [&] { Range r; r.first = (int)L.v.size(); return r; };
    auto endL = [&](Range& r) { r.count = (int)L.v.size() - r.first; };
    const Mat4 I = Mat4::identity();
    auto T = [](float x, float y, float z) { return Mat4::translate(Vec3(x, y, z)); };
    auto S = [](float x, float y, float z) { return Mat4::scale(Vec3(x, y, z)); };

    const uint32_t steel = 0x7d2a24, steelDark = 0x3d1b1c, bone = 0xd9cdb8;
    auto steelMat = [&] { M.color(steel); M.emissive(0xff2a10, kChSteel); };
    auto plain = [&](uint32_t c) { M.color(c); M.noEmissive(); };
    // A small helper: emissive with arbitrary strength.
    // Emissive material; its strength above 1 is applied afterwards with scaleEmissive.
    auto emis = [&](uint32_t c, float, int ch) {
        M.color(0x111111);
        M.emissive(c, ch);
    };
    // post-scale emissive of the vertices added since `from`
    auto scaleEmissive = [&](size_t from, float k) {
        for (size_t i = from; i < M.v.size(); ++i) for (float& e : M.v[i].e) e *= k;
    };

    // ---- world: ground, plaza, mountains, lanterns, trees, pagoda, torii ------------------------
    world_ = beginM();
    plain(0x2b2530);
    M.quad(Vec3(-450, 0, -450), Vec3(-450, 0, 450), Vec3(450, 0, 450), Vec3(450, 0, -450));
    plain(0x3a3138);
    for (int i = 0; i < 8; ++i) {
        const float a0 = i / 8.f * 2 * kPi, a1 = (i + 1) / 8.f * 2 * kPi;
        M.tri(Vec3(0, 0.05f, 0), Vec3(std::cos(a1) * 17, 0.05f, std::sin(a1) * 17), Vec3(std::cos(a0) * 17, 0.05f, std::sin(a0) * 17));
    }
    plain(0x1d1a26);
    for (int i = 0; i < 28; ++i) {
        const float a = i / 28.f * 2 * kPi + R() * 0.15f, r = 270 + R() * 50, h = 40 + R() * 70;
        M.cone(T(std::cos(a) * r, h / 2 - 2, std::sin(a) * r) * Mat4::rotateY(R() * 3), 30 + R() * 30, h, 5);
    }
    auto lantern = [&](float x, float z) {
        plain(steelDark);
        M.cylinder(T(x, 2, z), 0.12f, 0.12f, 4, 4);
        const size_t from = M.v.size();
        emis(0xff6a3a, 2.2f, kChNight);
        M.cylinder(T(x, 4.2f, z), 0.45f, 0.45f, 0.9f, 6);
        scaleEmissive(from, 2.2f);
    };
    auto deadTree = [&](float x, float z) {
        plain(0x1c1618);
        M.cone(T(x, 3.5f, z) * Mat4::rotateZ((R() - 0.5f) * 0.3f), 0.5f, 7, 4);
        for (int i = 0; i < 3; ++i)
            M.cone(T(x, 3 + i * 1.3f, z) * Mat4::rotateX(R() * 1.6f - 0.8f) * Mat4::rotateY(R() * 6) * Mat4::rotateZ(R() * 1.6f - 0.8f), 0.18f, 3, 3);
    };
    const Mat4 roofRot = Mat4::rotateY(kPi / 4);
    auto pagoda = [&](float x, float z) {
        float y = 0, s = 9;
        for (int i = 0; i < 5; ++i) {
            const float h = 3.2f;
            plain(0x4a2522);
            M.box(T(x, y + h / 2, z) * S(s * 0.62f, h, s * 0.62f));
            plain(0x2a2c35);
            M.cone(T(x, y + h + 0.6f, z) * S(s * 0.95f, 1.6f, s * 0.95f) * roofRot, 1, 1, 4);
            y += h + 1.1f;
            s *= 0.82f;
        }
        plain(bone);
        M.cylinder(T(x, y + 3.5f, z), 0.1f, 0.35f, 7, 5);
    };
    auto torii = [&](float x, float z, float ry, float sc) {
        const Mat4 base = T(x, 0, z) * Mat4::rotateY(ry) * S(sc, sc, sc);
        M.color(0x9a2f22);
        M.emissive(0x3a0a06, kChConst);
        for (float px : {-3.f, 3.f}) M.cylinder(base * T(px, 4, 0), 0.35f, 0.42f, 8, 6);
        M.box(base * T(0, 7.7f, 0) * S(8.6f, 0.45f, 0.7f));
        M.box(base * T(0, 6.2f, 0) * S(7.8f, 0.4f, 0.5f));
        plain(0x151214);
        M.box(base * T(0, 8.3f, 0) * S(9.4f, 0.6f, 0.9f));
    };

    // ---- city grid (buildings get their own ranges for the pumping / clipping) -----------------
    std::vector<std::pair<float, float>> lanternSpots, treeSpots;
    struct Plan { float x, z, ry, dist; int kind; };
    std::vector<Plan> plans;
    const float STEP = 10.5f;
    for (int ix = -11; ix <= 11; ++ix)
        for (int iz = -11; iz <= 11; ++iz) {
            if (ix % 4 == 0 || iz % 4 == 0) {
                if (ix % 4 == 0 && iz % 4 == 0 && R() < 0.6f && std::hypot((float)ix, (float)iz) > 2)
                    lanternSpots.push_back({ix * STEP + 2.5f, iz * STEP + 2.5f});
                continue;
            }
            const float x = ix * STEP + (R() - 0.5f) * 2, z = iz * STEP + (R() - 0.5f) * 2, dist = std::hypot(x, z);
            if (dist < 21) continue;
            const float ry = (float)((int)(R() * 4)) * kPi / 2 + (R() - 0.5f) * 0.12f;
            const float r = R();
            // high-rises only in a middle ring: never next to the tower, never next to the orbiting camera
            const float pTall = (dist < 55 || dist > 105) ? 0.f : 0.45f;
            plans.push_back({x, z, ry, dist, r < pTall ? 1 : (r < 0.9f ? 0 : 2)});
        }
    for (auto& p : plans) if (p.kind == 2) treeSpots.push_back({p.x, p.z});
    for (auto& l : lanternSpots) lantern(l.first, l.second);
    for (auto& t : treeSpots) deadTree(t.first, t.second);
    for (int i = 0; i < 8; ++i) { const float a = i / 8.f * 2 * kPi; lantern(std::cos(a) * 15, std::sin(a) * 15); }
    pagoda(-44, 26);
    torii(26, 36, -0.6f, 1.1f);
    torii(0, -19, 0, 0.9f);
    endM(world_);

    const uint32_t plaster[4] = {0x8c7f70, 0x6f655c, 0x5a4d47, 0x7a6a5e};
    const uint32_t roofs[3] = {0x2a2c35, 0x3b2a2c, 0x26302f};
    const uint32_t officeCols[3] = {0x3b3a45, 0x463c44, 0x30343d};
    const uint32_t signs[3] = {0xff3d6e, 0x8ad6df, 0xffd27a};
    for (auto& p : plans) {
        if (p.kind == 2) continue;
        Building b;
        b.pos = Vec3(p.x, 0, p.z);
        b.ry = p.ry;
        b.d = p.dist;
        b.ph = R() * 6.28f;
        b.r = beginM();
        if (p.kind == 0) {  // house
            const float w = 5 + R() * 3, d = 5 + R() * 3, h = 3 + R() * 3.5f;
            const uint32_t wall = plaster[(int)(R() * 4)], roof = roofs[(int)(R() * 3)];
            plain(wall);
            M.box(T(0, h / 2, 0) * S(w, h, d));
            float rh = 2 + R() * 1.4f, rsx = w * 0.82f, rsz = d * 0.82f, ry0 = h + rh / 2;
            if (R() < 0.35f) {  // second floor, Edo shop-house style
                M.box(T(0, h + h * 0.3f, 0) * S(w * 0.7f, h * 0.6f, d * 0.7f));
                plain(roof);
                M.cone(T(0, h + 0.3f, 0) * S(w * 0.8f, 0.8f, d * 0.8f) * roofRot, 1, 1, 4);
                ry0 = h + h * 0.6f + rh / 2;
                rsx *= 0.85f;
                rsz *= 0.85f;
            }
            plain(roof);
            M.cone(T(0, ry0, 0) * S(rsx, rh, rsz) * roofRot, 1, 1, 4);
            b.h = ry0 + rh / 2;
            if (R() < 0.3f) {  // glowing shop sign
                const size_t from = M.v.size();
                emis(signs[(int)(R() * 3)], 1.6f, kChNight);
                M.box(T(0, h * 0.75f, d / 2 + 0.1f) * S(w * 0.5f, 0.7f, 0.15f));
                scaleEmissive(from, 1.6f);
            }
        } else {  // office with lit windows
            const float w = 7 + R() * 4, d = 7 + R() * 4, h = 12 + R() * 9;
            plain(officeCols[(int)(R() * 3)]);
            M.box(T(0, h / 2, 0) * S(w, h, d));
            plain(roofs[0]);
            M.box(T(0, h + 0.6f, 0) * S(w * 0.6f, 1.2f, d * 0.6f));
            b.h = h + 1.2f;
            b.office = true;
            for (int face = 0; face < 4; ++face) {
                const float half = (face % 2 == 0) ? w / 2 : d / 2, span = (face % 2 == 0) ? d : w;
                const Mat4 f = Mat4::rotateY(face * kPi / 2);
                for (float y = 2.f; y < h - 1.2f; y += 2.6f)
                    for (float x = -span / 2 + 1.2f; x <= span / 2 - 1.2f; x += 2.4f) {
                        if (R() >= 0.42f) continue;
                        M.color(0x111111);
                        M.emissive(R() < 0.8f ? 0xffcf8a : 0x9fe4ff, kChWindows);
                        const float z = half + 0.03f;
                        // windows on the +X side of the rotated frame
                        M.quad(f.transformPoint(Vec3(z, y, x - 0.5f)), f.transformPoint(Vec3(z, y, x + 0.5f)),
                               f.transformPoint(Vec3(z, y + 1.3f, x + 0.5f)), f.transformPoint(Vec3(z, y + 1.3f, x - 0.5f)));
                    }
            }
            if (R() < 0.5f) {  // rooftop aerial with a red light
                plain(steelDark);
                M.cylinder(T(w * 0.2f, h + 3.7f, 0), 0.1f, 0.15f, 5, 4);
                const size_t from = M.v.size();
                emis(0xff6a3a, 2.2f, kChNight);
                M.octahedron(T(w * 0.2f, h + 6.3f, 0), 0.3f);
                scaleEmissive(from, 2.2f);
            }
        }
        endM(b.r);
        buildings_.push_back(b);
    }

    // ---- tower ----------------------------------------------------------------------------------
    tower_ = beginM();
    auto hw = [](float y) { return 1.0f + 8.2f * std::pow(1 - y / kTop, 1.9f); };
    auto corners = [&](float y, Vec3 out[4]) {
        const float w = hw(y);
        const float cs[4][2] = {{w, w}, {-w, w}, {-w, -w}, {w, -w}};
        for (int i = 0; i < 4; ++i) out[i] = Vec3(cs[i][0], y, cs[i][1]);
    };
    for (int i = 0; i * 4 < (int)kTop; ++i) {
        Vec3 a[4], b[4];
        corners(i * 4.f, a);
        corners(i * 4.f + 4, b);
        const float r = 0.55f - 0.035f * i;
        for (int c = 0; c < 4; ++c) {
            const int n = (c + 1) % 4;
            steelMat();
            M.beam(a[c], b[c], r);
            M.beam(b[c], b[n], 0.16f);
            plain(steelDark);
            M.beam(a[c], b[n], 0.13f);
            M.beam(a[n], b[c], 0.13f);
        }
    }
    {   // arched feet
        Vec3 c0[4];
        corners(0, c0);
        steelMat();
        for (int c = 0; c < 4; ++c) {
            const Vec3 a = c0[c], b = c0[(c + 1) % 4];
            Vec3 mid = (a + b) * 0.5f;
            mid.y = 6.5f;
            M.beam(lerp(a, b, 0.2f), mid, 0.3f);
            M.beam(lerp(a, b, 0.8f), mid, 0.3f);
        }
    }
    auto deck = [&](float y, float r0, float r1, float h) {
        plain(steelDark);
        M.cylinder(T(0, y, 0), r0, r1, h, 8);
        const size_t from = M.v.size();
        emis(0xffc477, 1.4f, kChNight);
        M.cylinder(T(0, y + h * 0.1f, 0), r0 + 0.08f, r0 + 0.08f, h * 0.38f, 8, true);
        scaleEmissive(from, 1.4f);
        steelMat();
        M.cylinder(T(0, y + h / 2 + 0.4f, 0), r0 * 0.7f, r0 * 1.12f, 0.8f, 8);
        plain(steelDark);
        for (int i = 0; i < 8; ++i) {  // dark-fantasy spikes on the eaves
            const float a = i / 8.f * 2 * kPi + kPi / 8;
            M.cone(T(std::cos(a) * r0 * 1.12f, y + h / 2 + 0.6f, std::sin(a) * r0 * 1.12f) *
                       Mat4::rotateX(std::sin(a) * 0.5f) * Mat4::rotateZ(-std::cos(a) * 0.5f), 0.22f, 1.6f, 4);
        }
    };
    deck(21, 6.4f, 5.6f, 3.4f);
    deck(37, 3.4f, 3.0f, 2.2f);
    endM(tower_);

    antenna_ = beginM();
    for (int i = 0; i * 2.5f < kAntH; ++i) {
        const float y = i * 2.5f, t0 = y / kAntH, t1 = std::min(1.f, (y + 2.5f) / kAntH);
        if (i % 2) plain(bone); else steelMat();
        M.cylinder(T(0, y + 1.25f, 0), 0.7f - 0.6f * t1, 0.7f - 0.6f * t0, 2.5f, 6);
    }
    endM(antenna_);

    // ---- creatures ------------------------------------------------------------------------------
    birdBody_ = beginM();
    plain(0x1a1518);
    M.cone(Mat4::rotateX(kPi / 2), 0.22f, 1.2f, 4);
    endM(birdBody_);
    birdWing_ = beginM();
    M.tri(Vec3(0, 0, 0.35f), Vec3(0, 0, -0.35f), Vec3(1.7f, 0, -0.2f));
    endM(birdWing_);

    auto dragonSkin = [&] { M.color(0x1f5a52); M.emissive(0x0d4a40, kChDragon); };
    auto dragonGold = [&] { M.color(0xd6b062); M.emissive(0x6a4a10, kChDragon); };
    segPlain_ = beginM();
    dragonSkin();
    M.icosahedron(I, 1, 0);
    endM(segPlain_);
    segFin_ = beginM();
    dragonSkin();
    M.icosahedron(I, 1, 0);
    dragonGold();
    M.cone(T(0, 0.95f, 0) * Mat4::rotateX(-0.5f), 0.35f, 1.6f, 4);
    endM(segFin_);
    head_ = beginM();
    dragonSkin();
    M.cone(T(0, 0, 1.6f) * Mat4::rotateX(kPi / 2), 1.1f, 3.2f, 5);
    M.icosahedron(I, 1.35f, 0);
    for (float sd : {-1.f, 1.f}) {
        dragonGold();
        M.cone(T(sd * 0.7f, 1.1f, -0.6f) * Mat4::rotateX(-1.0f) * Mat4::rotateZ(sd * 0.25f), 0.25f, 2.8f, 4);
        M.color(0x000000);
        M.emissive(0xff4a2c, kChConst);
        M.octahedron(T(sd * 0.75f, 0.45f, 0.9f), 0.22f);
    }
    endM(head_);

    // ---- unlit geometry ---------------------------------------------------------------------------
    const Vec3 white(1, 1, 1);
    ringTris_ = beginL();
    for (int i = 0; i < 64; ++i) {  // a band (visible side-on) plus a flat annulus (visible from above)
        const float a0 = i / 64.f * 2 * kPi, a1 = (i + 1) / 64.f * 2 * kPi;
        const Vec3 p0(std::cos(a0), 0, std::sin(a0)), p1(std::cos(a1), 0, std::sin(a1));
        const Vec3 up(0, 0.05f, 0);
        for (Vec3 q : {p0 - up, p1 - up, p1 + up, p0 - up, p1 + up, p0 + up}) L.add(q, white);
        for (Vec3 q : {p0 * 0.965f, p1 * 0.965f, p1 * 1.035f, p0 * 0.965f, p1 * 1.035f, p0 * 1.035f}) L.add(q, white);
    }
    endL(ringTris_);
    shellLines_ = beginL();
    icosphereEdges(L, 2, white);
    endL(shellLines_);
    gridLines_ = beginL();
    for (int i = 0; i < 24; ++i) {
        const float a = i / 24.f * 2 * kPi;
        L.line(Vec3(0, 0, 0), Vec3(std::cos(a) * 170, 0, std::sin(a) * 170), white);
    }
    for (int c = 1; c <= 10; ++c) {
        const float r = 170.f * c / 10;
        for (int i = 0; i < 96; ++i) {
            const float a0 = i / 96.f * 2 * kPi, a1 = (i + 1) / 96.f * 2 * kPi;
            L.line(Vec3(std::cos(a0) * r, 0, std::sin(a0) * r), Vec3(std::cos(a1) * r, 0, std::sin(a1) * r), white);
        }
    }
    endL(gridLines_);
    whiskers_ = beginL();
    for (float sd : {-1.f, 1.f}) {
        const Vec3 pts[4] = {{sd * 0.6f, -0.2f, 2.6f}, {sd * 2.2f, -0.6f, 3.6f}, {sd * 3.6f, -1.6f, 3.2f}, {sd * 4.6f, -2.6f, 2.0f}};
        for (int i = 0; i < 3; ++i) L.line(pts[i], pts[i + 1], rgb(0xd6b062));
    }
    endL(whiskers_);
    tipTris_ = beginL();
    {
        MeshBuilder tmp;
        tmp.octahedron(I, 0.7f);
        for (auto& vv : tmp.v) L.add(Vec3(vv.p[0], vv.p[1], vv.p[2]), white);
    }
    endL(tipTris_);
    stars_ = beginL();
    for (int i = 0; i < kStars; ++i) {
        const float a = R() * 2 * kPi, el = 0.12f + R() * 1.3f;
        L.add(Vec3(std::cos(a) * std::cos(el) * 480, std::sin(el) * 480, std::sin(a) * std::cos(el) * 480), white);
    }
    endL(stars_);
    footprint_ = beginL();
    {   // the empty plot where the tower stands once licensed: outline, diagonals, corner stakes
        const float w = 9.2f, y = 0.12f;
        const Vec3 c[4] = {{w, y, w}, {-w, y, w}, {-w, y, -w}, {w, y, -w}};
        for (int i = 0; i < 4; ++i) {
            L.line(c[i], c[(i + 1) % 4], white);
            L.line(c[i], c[i] + Vec3(0, 3.5f, 0), white);
            L.line(c[i] * 1.25f, c[i] * 0.75f, white);
        }
        L.line(c[0], c[2], white);
        L.line(c[1], c[3], white);
    }
    endL(footprint_);

    // ---- upload ---------------------------------------------------------------------------------
    GL.GenVertexArrays(1, &litVao_);
    GL.BindVertexArray(litVao_);
    GL.GenBuffers(1, &litVbo_);
    GL.BindBuffer(TW_GL_ARRAY_BUFFER, litVbo_);
    GL.BufferData(TW_GL_ARRAY_BUFFER, (GLsizeiptrT)(M.v.size() * sizeof(LitVertex)), M.v.data(), TW_GL_STATIC_DRAW);
    const int ls = sizeof(LitVertex);
    GL.EnableVertexAttribArray(0); GL.VertexAttribPointer(0, 3, GL_FLOAT, 0, ls, (void*)0);
    GL.EnableVertexAttribArray(1); GL.VertexAttribPointer(1, 3, GL_FLOAT, 0, ls, (void*)(3 * sizeof(float)));
    GL.EnableVertexAttribArray(2); GL.VertexAttribPointer(2, 3, GL_FLOAT, 0, ls, (void*)(6 * sizeof(float)));
    GL.EnableVertexAttribArray(3); GL.VertexAttribPointer(3, 3, GL_FLOAT, 0, ls, (void*)(9 * sizeof(float)));
    GL.EnableVertexAttribArray(4); GL.VertexAttribPointer(4, 1, GL_FLOAT, 0, ls, (void*)(12 * sizeof(float)));

    auto lineVao = [&](unsigned& vao, unsigned& vbo, const void* data, size_t bytes, unsigned usage) {
        GL.GenVertexArrays(1, &vao);
        GL.BindVertexArray(vao);
        GL.GenBuffers(1, &vbo);
        GL.BindBuffer(TW_GL_ARRAY_BUFFER, vbo);
        GL.BufferData(TW_GL_ARRAY_BUFFER, (GLsizeiptrT)bytes, data, usage);
        const int s = sizeof(LineVertex);
        GL.EnableVertexAttribArray(0); GL.VertexAttribPointer(0, 3, GL_FLOAT, 0, s, (void*)0);
        GL.EnableVertexAttribArray(1); GL.VertexAttribPointer(1, 4, GL_FLOAT, 0, s, (void*)(3 * sizeof(float)));
    };
    lineVao(lineVao_, lineVbo_, L.v.data(), L.v.size() * sizeof(LineVertex), TW_GL_STATIC_DRAW);

    // embers (dynamic)
    emberPos_.resize(kEmbers * 3);
    emberSpd_.resize(kEmbers);
    for (int i = 0; i < kEmbers; ++i) {
        const float a = R() * 2 * kPi, r = 4 + R() * 80;
        emberPos_[i * 3] = std::cos(a) * r;
        emberPos_[i * 3 + 1] = R() * 80;
        emberPos_[i * 3 + 2] = std::sin(a) * r;
        emberSpd_[i] = 0.6f + R() * 1.6f;
    }
    std::vector<LineVertex> ev(kEmbers);
    lineVao(emberVao_, emberVbo_, ev.data(), ev.size() * sizeof(LineVertex), TW_GL_DYNAMIC_DRAW);

    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    GL.GenVertexArrays(1, &quadVao_);
    GL.BindVertexArray(quadVao_);
    GL.GenBuffers(1, &quadVbo_);
    GL.BindBuffer(TW_GL_ARRAY_BUFFER, quadVbo_);
    GL.BufferData(TW_GL_ARRAY_BUFFER, sizeof quad, quad, TW_GL_STATIC_DRAW);
    GL.EnableVertexAttribArray(0);
    GL.VertexAttribPointer(0, 2, GL_FLOAT, 0, 2 * sizeof(float), (void*)0);
    GL.BindVertexArray(0);

    // birds
    birds_.resize(kBirds);
    for (auto& b : birds_) {
        b.a = R() * 6.28f;
        b.r = 28 + R() * 70;
        b.h = 26 + R() * 40;
        b.sp = (0.12f + R() * 0.2f) * (R() < 0.5f ? 1 : -1);
        b.ph = R() * 6.28f;
    }
}

bool Scene::init() {
    const char* litAttr[] = {"aPos", "aNormal", "aColor", "aEmis", "aCh"};
    const char* lineAttr[] = {"aPos", "aColor"};
    const char* quadAttr[] = {"aCorner"};
    const std::string lf = litFs(), nf = lineFs(), sf = spriteFs();
    litProg_ = compileProgram(kLitVs, lf.c_str(), litAttr, 5);
    lineProg_ = compileProgram(kLineVs, nf.c_str(), lineAttr, 2);
    spriteProg_ = compileProgram(kSpriteVs, sf.c_str(), quadAttr, 1);
    skyProg_ = compileProgram(kSkyVs, kSkyFs, quadAttr, 1);

    auto U = [](unsigned p, const char* n) { return GL.GetUniformLocation(p, n); };
    lu_ = {U(litProg_, "uViewProj"), U(litProg_, "uModel"), U(litProg_, "uSunDir"), U(litProg_, "uSunColor"),
           U(litProg_, "uHemiSky"), U(litProg_, "uHemiGround"), U(litProg_, "uRimDir"), U(litProg_, "uRimColor"),
           U(litProg_, "uP0Pos"), U(litProg_, "uP0Col"), U(litProg_, "uP1Pos"), U(litProg_, "uP1Col"),
           U(litProg_, "uFogColor"), U(litProg_, "uFogNear"), U(litProg_, "uFogFar"), U(litProg_, "uCamPos"),
           U(litProg_, "uCh"), U(litProg_, "uColorMul"), U(litProg_, "uBright"), U(litProg_, "uDesat"),
           U(litProg_, "uClipY")};
    nu_ = {U(lineProg_, "uViewProj"), U(lineProg_, "uModel"), U(lineProg_, "uColor"), U(lineProg_, "uPointSize"),
           U(lineProg_, "uRound"), U(lineProg_, "uFogColor"), U(lineProg_, "uFogNear"), U(lineProg_, "uFogFar"),
           U(lineProg_, "uFogAmt"), U(lineProg_, "uCamPos"), U(lineProg_, "uBright"), U(lineProg_, "uDesat")};
    su_ = {U(spriteProg_, "uViewProj"), U(spriteProg_, "uCenter"), U(spriteProg_, "uSize"), U(spriteProg_, "uRight"),
           U(spriteProg_, "uUp"), U(spriteProg_, "uColor"), U(spriteProg_, "uMode"), U(spriteProg_, "uBright"),
           U(spriteProg_, "uDesat")};
    ku_ = {U(skyProg_, "uInvViewProj"), U(skyProg_, "uCamPos"), U(skyProg_, "uTop"), U(skyProg_, "uBottom")};

    buildGeometry();
    ready_ = litProg_ && lineProg_ && spriteProg_ && skyProg_;
    return ready_;
}

void Scene::destroy() {
    const unsigned vbos[4] = {litVbo_, lineVbo_, emberVbo_, quadVbo_};
    const unsigned vaos[4] = {litVao_, lineVao_, emberVao_, quadVao_};
    GL.DeleteBuffers(4, vbos);
    GL.DeleteVertexArrays(4, vaos);
    for (unsigned p : {litProg_, lineProg_, spriteProg_, skyProg_}) if (p) GL.DeleteProgram(p);
    ready_ = false;
}

// ============================================================================================
// Simulation
// ============================================================================================
void Scene::spawnWave(float s, bool echo) {
    Wave& w = waves_[waveIndex_++ % 14];
    w.age = 0;
    w.s = s;
    w.oy = tipPos_.y;
    w.echo = echo;
    w.life = 2.2f * (1.f + in_.space / 100.f * 0.6f);
    w.front = w.prevFront = 0;
}

void Scene::bassHit(float strength) {
    const float s = strength * (in_.bypass ? 0.45f : 1.f) * (0.55f + 0.6f * in_.elevate / 100.f);
    buildFlash_ = std::max(buildFlash_, strength);
    if (build_ < 0.999f) {  // no antenna yet: nothing to send waves from
        quake_ = std::max(quake_, strength * in_.sub / 100.f);
        return;
    }
    spawnWave(s, false);
    const int n = (int)std::lround(in_.space / 100.f * 3.f);  // Space: echo waves
    for (int i = 1; i <= n; ++i) echoes_.push_back({time_ + i * 0.28f, s * std::pow(0.5f, (float)i)});
    flash_ = std::max(flash_, s);
    clipFlash_ = std::max(clipFlash_, strength * in_.clip / 100.f);
    quake_ = std::max(quake_, strength * in_.sub / 100.f);                       // Sub: the tower shakes
    if (in_.cameraOnBass) orbitV_ += strength * (0.08f + 0.55f * in_.punch / 100.f);  // Punch: camera turns
    fovKick_ = std::max(fovKick_, strength * in_.punch / 100.f * 3.5f);
}

void Scene::update(float dt, const SceneInput& in) {
    in_ = in;
    time_ += dt;
    const float n01 = 0.01f;

    // ---- time of day (Dry/Wet) ----
    tod_ = approach(tod_, in.mix * n01, 3.f, dt);
    int k = 0;
    while (k < 2 && tod_ > kSky[k + 1].t) ++k;
    const SkyKey &a = kSky[k], &b = kSky[k + 1];
    const float t = clamp01((tod_ - a.t) / (b.t - a.t));
    skyTop_ = lerp(rgb(a.top), rgb(b.top), t);
    skyBottom_ = lerp(rgb(a.bot), rgb(b.bot), t);
    fogColor_ = lerp(rgb(a.fog), rgb(b.fog), t);
    hemi_ = a.hemi + (b.hemi - a.hemi) * t;
    sun_ = a.sun + (b.sun - a.sun) * t;
    sunColor_ = lerp(rgb(a.sunC), rgb(b.sunC), t);
    orbColor_ = lerp(rgb(a.orb), rgb(b.orb), t);
    orbPos_ = Vec3(-140 + 60 * tod_, 150 - 110 * std::sin(std::min(1.f, tod_ * 1.6f) * kPi / 2) + 100 * std::max(0.f, tod_ - 0.6f), -260);
    night_ = std::max(0.f, (tod_ - 0.35f) / 0.65f);
    steelMul_ = lerp(Vec3(1, 1, 1), Vec3(0x5a / (float)0x7d, 0x1d / (float)0x2a, 0x1c / (float)0x24), night_);

    // ---- licence: the tower builds itself or comes apart ----
    {
        const float target = in.licensed ? 1.f : 0.f;
        const float upRate = in.quickBuild ? 1.f / 1.5f : 1.f / 7.f, downRate = 1.f / 3.f;
        if (build_ < target) build_ = std::min(target, build_ + dt * upRate);
        else if (build_ > target) build_ = std::max(target, build_ - dt * downRate);
        buildFlash_ *= std::exp(-dt * 4);
    }

    // ---- knobs ----
    antScale_ = approach(antScale_, 0.7f + (in.input + 24.f) / 48.f * 0.6f, 6.f, dt);  // Entrée: antenna length
    const float d = in.elevate * n01;                                                   // Elevate
    steelGlow_ = d * d * 0.55f * (0.35f + night_) + flash_ * d * 0.4f;
    starAlpha_ = night_ * (0.2f + 0.8f * d) * (0.9f + 0.1f * std::sin(time_ * 3));
    starSize_ = 1.f + d * 1.8f;
    windowGain_ = 1.1f * night_;
    const float sp = in.space * n01;                                                    // Space: fog
    fogNear_ = camR_ * (0.5f - sp * 0.4f);
    fogFar_ = camR_ + 200 - sp * 140;
    ceilY_ = approach(ceilY_, 34 - in.clip * n01 * 28, 4.f, dt);                        // Clipper ceiling
    clipFlash_ *= std::exp(-dt * 5);
    domeR_ = approach(domeR_, 70 - in.limit * n01 * 52, 4.f, dt);                        // Limiter bubble
    domeFlash_ *= std::exp(-dt * 4);
    quake_ *= std::exp(-dt * 7);
    towerOffset_ = Vec3((randf() - 0.5f) * quake_ * 0.9f, 0, (randf() - 0.5f) * quake_ * 0.9f);
    tipPos_ = towerOffset_ + Vec3(0, kAnt0 + (kAntH + 0.6f) * antScale_, 0);
    bright_ = std::min(1.6f, std::max(0.55f, std::pow(2.f, in.output / 18.f))) * (in.bypass ? 0.8f : 1.f);  // Sortie
    desat_ = approach(desat_, in.bypass ? 0.75f : 0.f, 3.f, dt);
    flash_ *= std::pow(0.02f, dt);

    // ---- waves ----
    for (size_t i = 0; i < echoes_.size();)
        if (time_ >= echoes_[i].at) { spawnWave(echoes_[i].s, true); echoes_.erase(echoes_.begin() + (long)i); }
        else ++i;
    for (auto& w : waves_) {
        if (w.age > w.life) { w.front = w.prevFront = 999; continue; }
        w.age += dt;
        w.prevFront = w.front;
        w.front = w.echo ? 0 : 175 * std::min(1.f, w.age / 1.4f);  // ground shockwave for the pumping
    }

    // ---- buildings: Compression pumping, Clipper flattening ----
    const float h = std::min(dt, 1.f / 30);
    for (auto& bd : buildings_) {
        for (auto& w : waves_) if (!w.echo && w.age <= w.life && bd.d > w.prevFront && bd.d <= w.front) bd.v += w.s * 7;
        bd.v += (-150 * bd.y - 8 * bd.v) * h;
        bd.y += bd.v * h;
    }

    // ---- embers ----
    const float emSpeed = 0.5f + d * 2.f;
    for (int i = 0; i < kEmbers; ++i) {
        emberPos_[i * 3 + 1] += emberSpd_[i] * dt * emSpeed;
        emberPos_[i * 3] += std::sin(time_ * 0.5f + i) * 0.02f;
        if (emberPos_[i * 3 + 1] > 85) emberPos_[i * 3 + 1] = 0;
    }

    // ---- creatures (oversampling) ----
    const int os = std::max(0, std::min(4, in.oversampling));
    const float day = clamp01((0.6f - night_) / 0.3f);
    for (int i = 0; i < kBirds; ++i) {
        Bird& bi = birds_[(size_t)i];
        bi.sc = approach(bi.sc, i < kBirdsByOs[os] ? day * 1.3f : 0.f, 2.5f, dt);
        bi.a += bi.sp * dt;
    }
    const float vis = clamp01((night_ - 0.45f) / 0.25f);
    dragonSize_ = approach(dragonSize_, kDragonSize[os] * vis, 1.5f, dt);
    dragonLen_ = approach(dragonLen_, kDragonSegsByOs[os] * vis, 1.5f, dt);
    dragonTh_ += dt * 0.22f;

    // ---- camera: orbit pushed by bass hits, Space = distance, bypass = dolly onto the antenna ----
    zoom_ = approach(zoom_, in.bypass ? 1.f : 0.f, 2.2f, dt);
    orbitV_ *= std::exp(-dt * 1.6f);
    theta_ += (orbitV_ + 0.012f) * dt;
    camR_ = approach(camR_, 110 + sp * 70, 3.f, dt);
    fovKick_ *= std::exp(-dt * 8);
}

// ============================================================================================
// Rendering
// ============================================================================================
void Scene::setGrade(unsigned, int locBright, int locDesat) {
    GL.Uniform1f(locBright, bright_);
    GL.Uniform1f(locDesat, desat_);
}

void Scene::drawLit(const Range& r, const Mat4& model, Vec3 colorMul, float windows) {
    if (r.count <= 0) return;
    GL.UniformMatrix4fv(lu_.model, 1, 0, model.m);
    GL.Uniform3f(lu_.colorMul, colorMul.x, colorMul.y, colorMul.z);
    if (windows >= 0.f) {
        float ch[kNumChannels] = {0, night_, windows, steelGlow_, 0.5f + 0.6f * flash_, 1.f};
        GL.Uniform1fv(lu_.ch, kNumChannels, ch);
    }
    glDrawArrays(GL_TRIANGLES, r.first, r.count);
}

void Scene::drawLines(const Range& r, unsigned mode, const Mat4& model, Vec3 color, float alpha, float fog) {
    if (r.count <= 0 || alpha <= 0.001f) return;
    GL.UniformMatrix4fv(nu_.model, 1, 0, model.m);
    GL.Uniform4f(nu_.color, color.x, color.y, color.z, alpha);
    GL.Uniform1f(nu_.fogAmt, fog);
    glDrawArrays(mode, r.first, r.count);
}

void Scene::drawSprite(Vec3 c, float sx, float sy, Vec3 color, float alpha, int mode) {
    GL.Uniform3f(su_.center, c.x, c.y, c.z);
    GL.Uniform2f(su_.size, sx, sy);
    GL.Uniform4f(su_.color, color.x, color.y, color.z, alpha);
    GL.Uniform1i(su_.mode, mode);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Scene::render(int vpX, int vpY, int vpW, int vpH, float aspect) {
    if (!ready_ || vpW <= 0 || vpH <= 0) return;
    const float n01 = 0.01f;
    const float tall = std::max(0.f, 1.6f - aspect);   // keep the tower framed on narrow windows

    // ---- camera ----
    const float z = zoom_ * zoom_ * (3 - 2 * zoom_);
    const float wideR = camR_ + tall * 40, wideH = 26 + tall * 6;
    const float r = wideR + (64 - wideR) * z;
    const float hgt = wideH + (36 - wideH) * z + std::sin(time_ * 0.11f) * 1.2f - in_.mouseY * 3;
    const float th = theta_ + in_.mouseX * 0.12f;
    camPos_ = Vec3(std::sin(th) * r, hgt + (randf() - 0.5f) * quake_ * 1.4f, std::cos(th) * r);
    const Vec3 look(0, 27 + (40 - 27) * z, 0);
    const float fov = 42 + (44 - 42) * z - fovKick_;
    view_ = Mat4::lookAt(camPos_, look, Vec3(0, 1, 0));
    proj_ = Mat4::perspective(fov, aspect, 0.5f, 900.f);
    viewProj_ = proj_ * view_;
    const Vec3 camRight(view_.m[0], view_.m[4], view_.m[8]), camUp(view_.m[1], view_.m[5], view_.m[9]);

    glViewport(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vpX, vpY, vpW, vpH);
    glClearColor(fogColor_.x, fogColor_.y, fogColor_.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);

    // ---- sky ----
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    GL.UseProgram(skyProg_);
    const Mat4 inv = viewProj_.inverse();
    GL.UniformMatrix4fv(ku_.invViewProj, 1, 0, inv.m);
    GL.Uniform3f(ku_.camPos, camPos_.x, camPos_.y, camPos_.z);
    GL.Uniform3f(ku_.top, skyTop_.x, skyTop_.y, skyTop_.z);
    GL.Uniform3f(ku_.bottom, skyBottom_.x, skyBottom_.y, skyBottom_.z);
    GL.Uniform1f(GL.GetUniformLocation(skyProg_, "uBright"), bright_);
    GL.Uniform1f(GL.GetUniformLocation(skyProg_, "uDesat"), desat_);
    GL.BindVertexArray(quadVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // ---- celestial body (sun by day, moon by night) ----
    glEnable(GL_BLEND);
    GL.UseProgram(spriteProg_);
    GL.UniformMatrix4fv(su_.viewProj, 1, 0, viewProj_.m);
    GL.Uniform3f(su_.right, camRight.x, camRight.y, camRight.z);
    GL.Uniform3f(su_.up, camUp.x, camUp.y, camUp.z);
    setGrade(spriteProg_, su_.bright, su_.desat);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    drawSprite(orbPos_, 55, 55, Vec3(1.f, 0.94f, 0.82f), 0.5f + 0.5f * std::fabs(tod_ - 0.5f) * 2, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawSprite(orbPos_, 9, 9, orbColor_, 1.f, 1);

    // ---- stars (behind everything, additive, no depth) ----
    GL.UseProgram(lineProg_);
    GL.UniformMatrix4fv(nu_.viewProj, 1, 0, viewProj_.m);
    GL.Uniform3f(nu_.camPos, camPos_.x, camPos_.y, camPos_.z);
    GL.Uniform3f(nu_.fogColor, fogColor_.x, fogColor_.y, fogColor_.z);
    GL.Uniform1f(nu_.fogNear, fogNear_);
    GL.Uniform1f(nu_.fogFar, fogFar_);
    setGrade(lineProg_, nu_.bright, nu_.desat);
    glEnable(TW_GL_PROGRAM_POINT_SIZE);
    GL.BindVertexArray(lineVao_);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    GL.Uniform1i(nu_.round, 1);
    GL.Uniform1f(nu_.pointSize, starSize_ * 1.5f);
    drawLines(stars_, GL_POINTS, Mat4::identity(), Vec3(0.96f, 0.94f, 1.f), starAlpha_);
    GL.Uniform1i(nu_.round, 0);

    // ---- opaque lit geometry ----
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    GL.UseProgram(litProg_);
    GL.UniformMatrix4fv(lu_.viewProj, 1, 0, viewProj_.m);
    const Vec3 sunDir = Vec3(-80, 120, -60).normalized(), rimDir = Vec3(60, 40, 120).normalized();
    const Vec3 sunC = sunColor_ * sun_, hemiSky = rgb(0xbfd0e0) * hemi_, hemiGround = rgb(0x2a2026) * hemi_;
    const Vec3 rimC = rgb(0x8ad6df) * (0.35f * night_);
    GL.Uniform3f(lu_.sunDir, sunDir.x, sunDir.y, sunDir.z);
    GL.Uniform3f(lu_.sunColor, sunC.x, sunC.y, sunC.z);
    GL.Uniform3f(lu_.hemiSky, hemiSky.x, hemiSky.y, hemiSky.z);
    GL.Uniform3f(lu_.hemiGround, hemiGround.x, hemiGround.y, hemiGround.z);
    GL.Uniform3f(lu_.rimDir, rimDir.x, rimDir.y, rimDir.z);
    GL.Uniform3f(lu_.rimColor, rimC.x, rimC.y, rimC.z);
    const Vec3 tipL = rgb(0x8ad6df) * ((0.4f + 6 * flash_) * (0.3f + night_) * 0.35f * (build_ >= 1.f ? 1.f : 0.f));
    const Vec3 deckL = rgb(0xffb066) * (2.2f * night_ * 0.6f * (build_ > 0.45f ? 1.f : 0.f));
    GL.Uniform3f(lu_.p0Pos, tipPos_.x, tipPos_.y, tipPos_.z);
    GL.Uniform4f(lu_.p0Col, tipL.x, tipL.y, tipL.z, 90.f);
    GL.Uniform3f(lu_.p1Pos, towerOffset_.x, 21, towerOffset_.z);
    GL.Uniform4f(lu_.p1Col, deckL.x, deckL.y, deckL.z, 40.f);
    GL.Uniform3f(lu_.fogColor, fogColor_.x, fogColor_.y, fogColor_.z);
    GL.Uniform1f(lu_.fogNear, fogNear_);
    GL.Uniform1f(lu_.fogFar, fogFar_);
    GL.Uniform3f(lu_.camPos, camPos_.x, camPos_.y, camPos_.z);
    setGrade(litProg_, lu_.bright, lu_.desat);
    GL.BindVertexArray(litVao_);

    const Vec3 one(1, 1, 1);
    drawLit(world_, Mat4::identity(), one, windowGain_);
    const Mat4 towerM = Mat4::translate(towerOffset_);
    // construction height: everything above it is not built yet
    buildTopY_ = tipPos_.y + 1.5f;
    const float eased = build_ * build_ * (3 - 2 * build_);
    const float clipY = build_ >= 1.f ? 1e6f : (build_ <= 0.f ? -1.f : eased * buildTopY_);
    GL.Uniform1f(lu_.clipY, clipY);
    drawLit(tower_, towerM, steelMul_);
    drawLit(antenna_, towerM * Mat4::translate(Vec3(0, kAnt0, 0)) * Mat4::scale(Vec3(1, antScale_, 1)), steelMul_);
    GL.Uniform1f(lu_.clipY, 1e6f);

    const float d = in_.elevate * n01, amt = in_.comp * n01 * 0.5f;
    for (auto& bd : buildings_) {
        const float s = std::max(-0.6f, std::min(0.6f, bd.y)) * amt;
        const float hb = bd.h * (1 + s);
        float hc = hb;
        if (in_.clip > 0.5f) {
            const float c = ceilY_;
            if (in_.clipStyle == 0) hc = std::min(hb, c);  // Hard: flat cut
            else if (in_.clipStyle == 1) hc = hb < c * 0.6f ? hb : c * 0.6f + c * 0.4f * std::tanh((hb - c * 0.6f) / (c * 0.4f));
            else hc = (hb < c * 0.5f ? hb : c * 0.5f + c * 0.5f * std::tanh((hb - c * 0.5f) / (c * 0.5f))) *
                      (1 + 0.08f * std::sin(time_ * 9 + bd.d) * clipFlash_);  // Analog: soft knee + wobble
        }
        const Mat4 m = Mat4::translate(bd.pos) * Mat4::rotateY(bd.ry) * Mat4::scale(Vec3(1 - s * 0.35f, hc / bd.h, 1 - s * 0.35f));
        const float win = bd.office ? windowGain_ * (1 + std::sin(time_ * 6 + bd.ph) * 0.25f * d) : windowGain_;
        drawLit(bd.r, m, one, win);
    }

    // birds
    for (auto& bi : birds_) {
        if (bi.sc < 0.02f) continue;
        const Vec3 p(std::cos(bi.a) * bi.r, bi.h + std::sin(bi.a * 3 + bi.ph) * 3, std::sin(bi.a) * bi.r);
        const float sg = bi.sp > 0 ? 1.f : -1.f;
        const Vec3 tangent(-std::sin(bi.a) * sg, 0, std::cos(bi.a) * sg);
        const Mat4 m = Mat4::facing(p, p + tangent) * Mat4::scale(Vec3(bi.sc, bi.sc, bi.sc));
        const float flap = std::sin(time_ * 11 + bi.ph) * 0.7f;
        drawLit(birdBody_, m, one);
        drawLit(birdWing_, m * Mat4::rotateZ(flap), one);
        drawLit(birdWing_, m * Mat4::scale(Vec3(-1, 1, 1)) * Mat4::rotateZ(flap), one);
    }

    // dragon
    Mat4 headM;
    const bool dragonOn = dragonSize_ > 0.03f;
    if (dragonOn) {
        const int n = std::max(2, (int)std::lround(dragonLen_));
        const float step = 0.045f * (0.6f + dragonSize_ * 0.6f);
        for (int i = 0; i < std::min(n, kDragonSegs); ++i) {
            const Vec3 p = dragonPath(dragonTh_ - (i + 1) * step), q = dragonPath(dragonTh_ - i * step);
            const float taper = std::pow(1.f - (float)i / n, 0.6f);
            const float sz = dragonSize_ * (0.5f + 1.6f * taper);
            drawLit(i % 3 == 1 ? segFin_ : segPlain_, Mat4::facing(p, q) * Mat4::scale(Vec3(sz, sz, sz * 1.5f)), one);
        }
        const Vec3 hp = dragonPath(dragonTh_), hq = dragonPath(dragonTh_ + 0.02f);
        const float hs = dragonSize_ * 1.5f;
        headM = Mat4::facing(hp, hq) * Mat4::scale(Vec3(hs, hs, hs));
        drawLit(head_, headM, one);
    }

    // ---- additive / transparent effects ----
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    GL.UseProgram(lineProg_);
    GL.BindVertexArray(lineVao_);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    if (dragonOn) drawLines(whiskers_, GL_LINES, headM, one, 1.f);

    // tip of the antenna, flashing on bass
    const Vec3 tipCol = lerp(rgb(0xff5a3c), rgb(0xd6fbff), std::min(1.f, flash_));
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (clipY > tipPos_.y) drawLines(tipTris_, GL_TRIANGLES, Mat4::translate(tipPos_), tipCol, 1.f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    // licence: empty plot, or the glowing construction front climbing the tower
    if (build_ < 1.f) {
        const Vec3 gold = rgb(0xd6b062);
        const float pulse = 0.5f + 0.5f * std::sin(time_ * 2.2f);
        drawLines(footprint_, GL_LINES, Mat4::translate(towerOffset_), gold, (1.f - build_) * (0.25f + 0.35f * pulse + 0.4f * buildFlash_));
        if (build_ > 0.f) {
            const float y = clipY;
            float r = y < kTop ? 1.0f + 8.2f * std::pow(1 - y / kTop, 1.9f) + 1.2f : 1.6f;
            if (std::fabs(y - 21) < 3) r = std::max(r, 7.6f);
            if (std::fabs(y - 37) < 2.2f) r = std::max(r, 4.4f);
            const Mat4 front = Mat4::translate(towerOffset_ + Vec3(0, y, 0));
            drawLines(ringTris_, GL_TRIANGLES, front * Mat4::scale(Vec3(r, 2.5f, r)), gold, 0.9f);
            drawLines(ringTris_, GL_TRIANGLES, front * Mat4::scale(Vec3(r * 1.35f, 1, r * 1.35f)), gold, 0.35f + 0.4f * buildFlash_);
        }
    }

    // Clipper: glowing ceiling grid
    if (in_.clip > 0.5f) {
        const Vec3 gc = rgb(in_.clipStyle == 0 ? 0xff4a2c : (in_.clipStyle == 1 ? 0xffb35c : 0xd6b062));
        drawLines(gridLines_, GL_LINES, Mat4::translate(Vec3(0, ceilY_, 0)), gc,
                  (0.12f + 0.5f * clipFlash_) * (0.4f + 0.6f * in_.clip * n01));
    }

    // waves from the antenna
    const Vec3 waveCol = lerp(rgb(0xf6e2b0), rgb(0x8ad6df), night_);
    const float sizeMul = 0.6f + in_.space * n01 * 0.8f;
    const bool domeOn = in_.limit > 0.5f && build_ >= 1.f;  // the bubble needs an antenna
    for (auto& w : waves_) {
        if (w.age > w.life) continue;
        const float kk = std::min(1.f, w.age / w.life), e = 1 - std::pow(1 - kk, 3.f), fade = std::pow(1 - kk, 1.6f);
        float rs = 1 + 52 * e * sizeMul;
        if (domeOn && rs > domeR_) { domeFlash_ = std::max(domeFlash_, w.s * 0.5f); rs = domeR_; }
        drawLines(ringTris_, GL_TRIANGLES, Mat4::translate(Vec3(0, w.oy, 0)) * Mat4::scale(Vec3(rs, 1 + 3 * e, rs)), waveCol, fade * w.s * 0.95f);
        const float k2 = std::max(0.f, kk - 0.12f), e2 = 1 - std::pow(1 - k2, 3.f);
        float r2 = 1 + 40 * e2 * sizeMul;
        if (domeOn) r2 = std::min(r2, domeR_);
        drawLines(ringTris_, GL_TRIANGLES, Mat4::translate(Vec3(0, w.oy - 10 * e2, 0)) * Mat4::scale(Vec3(r2, 1, r2)), waveCol, std::pow(1 - k2, 2.f) * w.s * 0.5f);
        float ss = 1 + 26 * e * sizeMul;
        if (domeOn) ss = std::min(ss, domeR_);
        drawLines(shellLines_, GL_LINES, Mat4::translate(Vec3(0, w.oy, 0)) * Mat4::scale(Vec3(ss, ss * 0.55f, ss)), waveCol, fade * w.s * 0.16f);
    }

    // Limiter: bubble around the antenna tip
    if (domeOn) {
        const float lim = in_.limit * n01;
        drawLines(shellLines_, GL_LINES, Mat4::translate(tipPos_) * Mat4::rotateY(time_ * 0.05f) * Mat4::scale(Vec3(domeR_, domeR_, domeR_)),
                  lerp(rgb(0xf6e2b0), rgb(0x8ad6df), night_), 0.03f + 0.05f * lim + 0.35f * domeFlash_);
    }

    // embers (Elevate)
    {
        std::vector<LineVertex> ev((size_t)kEmbers);
        for (int i = 0; i < kEmbers; ++i) ev[(size_t)i] = {{emberPos_[i * 3], emberPos_[i * 3 + 1], emberPos_[i * 3 + 2]}, {1, 1, 1, 1}};
        GL.BindVertexArray(emberVao_);
        GL.BindBuffer(TW_GL_ARRAY_BUFFER, emberVbo_);
        GL.BufferSubData(TW_GL_ARRAY_BUFFER, 0, (GLsizeiptrT)(ev.size() * sizeof(LineVertex)), ev.data());
        GL.Uniform1i(nu_.round, 1);
        GL.Uniform1f(nu_.pointSize, (1.5f + d * 4.f) * (float)vpH / 600.f);
        const Vec3 ec = lerp(rgb(0x9a8f8a), rgb(0xff7a3a), std::min(1.f, d * 1.6f));
        drawLines(Range{0, kEmbers}, GL_POINTS, Mat4::identity(), ec, (0.1f + 0.75f * night_) * (0.35f + d * 0.65f), 1.f);
        GL.Uniform1i(nu_.round, 0);
    }

    // glow of the antenna tip
    GL.UseProgram(spriteProg_);
    GL.BindVertexArray(quadVao_);
    const float gs = (5 + 9 * flash_) * (0.8f + 0.6f * d) * 0.5f;
    if (clipY > tipPos_.y) drawSprite(tipPos_, gs, gs, Vec3(1.f, 0.47f, 0.35f), 0.9f, 0);
    if (build_ > 0.f && build_ < 1.f)
        drawSprite(towerOffset_ + Vec3(0, clipY, 0), 9 + 5 * buildFlash_, 4 + 2 * buildFlash_, Vec3(1.f, 0.85f, 0.5f), 0.8f, 0);

    GL.BindVertexArray(0);
    GL.UseProgram(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
}

} // namespace tw
