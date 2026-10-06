// The 3D scene: a low-poly Japanese observation tower in a dark-fantasy city.
// Native OpenGL 3.2 port of ui-prototype/tower.body.html. Every knob drives one effect.
#pragma once

#include "Math3d.h"
#include "MeshBuilder.h"

#include <vector>

namespace tw {

struct SceneInput {
    float input = 0, elevate = 30, comp = 30, punch = 40, sub = 35, mix = 80, space = 40, clip = 20, limit = 40,
          output = 0;
    int oversampling = 2;  // index: 1x 2x 4x 8x 16x
    int clipStyle = 1;
    bool bypass = false, cameraOnBass = true;
    float mouseX = 0, mouseY = 0;  // -0.5 .. 0.5 over the viewport, 0 when outside
    // The tower only exists with a valid licence: it builds itself when the licence
    // appears and comes apart when it goes away.
    bool licensed = true;
    bool quickBuild = false;  // tower already built once in this session: assemble fast
};

class Scene {
public:
    bool init();      // needs a current GL context
    void destroy();   // needs a current GL context
    void bassHit(float strength);
    void update(float dt, const SceneInput& in);
    void render(int vpX, int vpY, int vpW, int vpH, float aspect);
    float flash() const { return flash_; }
    float buildProgress() const { return build_; }  // 0 = empty plaza, 1 = tower complete

private:
    struct Range { int first = 0, count = 0; };
    struct Building { Range r; Vec3 pos; float ry = 0, d = 0, h = 8, y = 0, v = 0; bool office = false; float ph = 0; };
    struct Wave { float age = 99, life = 2.2f, s = 0, oy = 0, front = 0, prevFront = 0; bool echo = false; };
    struct Echo { float at, s; };
    struct Bird { float a, r, h, sp, ph, sc = 0; };

    void buildGeometry();
    void spawnWave(float s, bool echo);
    void drawLit(const Range& r, const Mat4& model, Vec3 colorMul = Vec3(1, 1, 1), float windows = -1.f);
    void drawLines(const Range& r, unsigned mode, const Mat4& model, Vec3 color, float alpha, float fog = 0.f);
    void drawSprite(Vec3 center, float sx, float sy, Vec3 color, float alpha, int mode);
    void setGrade(unsigned prog, int locBright, int locDesat);

    // GL objects
    unsigned litProg_ = 0, lineProg_ = 0, spriteProg_ = 0, skyProg_ = 0;
    unsigned litVao_ = 0, litVbo_ = 0, lineVao_ = 0, lineVbo_ = 0, emberVao_ = 0, emberVbo_ = 0, quadVao_ = 0, quadVbo_ = 0;
    bool ready_ = false;

    // uniform locations
    struct {
        int viewProj, model, sunDir, sunColor, hemiSky, hemiGround, rimDir, rimColor, p0Pos, p0Col, p1Pos, p1Col,
            fogColor, fogNear, fogFar, camPos, ch, colorMul, bright, desat, clipY;
    } lu_{};
    struct { int viewProj, model, color, pointSize, round, fogColor, fogNear, fogFar, fogAmt, camPos, bright, desat; } nu_{};
    struct { int viewProj, center, size, right, up, color, mode, bright, desat; } su_{};
    struct { int invViewProj, camPos, top, bottom; } ku_{};

    // geometry ranges
    Range world_, tower_, antenna_, birdBody_, birdWing_, segPlain_, segFin_, head_;
    Range ringTris_, shellLines_, gridLines_, whiskers_, tipTris_, stars_, footprint_;
    std::vector<Building> buildings_;
    std::vector<float> emberPos_, emberSpd_;
    std::vector<Bird> birds_;
    Wave waves_[14];
    std::vector<Echo> echoes_;
    int waveIndex_ = 0;

    // animated state
    float time_ = 0, tod_ = 0.8f, night_ = 0.7f, flash_ = 0, quake_ = 0, fovKick_ = 0, orbitV_ = 0;
    float theta_ = 0.595f, zoom_ = 0, camR_ = 143, camH_ = 26, antScale_ = 1.f;
    float ceilY_ = 60, clipFlash_ = 0, domeR_ = 70, domeFlash_ = 0, dragonTh_ = 0, dragonSize_ = 0, dragonLen_ = 0;
    Vec3 tipPos_{0, 68.6f, 0}, towerOffset_;
    SceneInput in_;

    // per-frame lighting / camera
    Mat4 view_, proj_, viewProj_;
    Vec3 camPos_, skyTop_, skyBottom_, fogColor_, sunColor_, orbColor_, orbPos_;
    float hemi_ = 0.6f, sun_ = 1.f, fogNear_ = 70, fogFar_ = 330, bright_ = 1.f, desat_ = 0.f;
    float windowGain_ = 0, steelGlow_ = 0, starAlpha_ = 0, starSize_ = 1.5f;
    Vec3 steelMul_{1, 1, 1};
    // licence-driven construction
    float build_ = 0, buildFlash_ = 0, buildTopY_ = 70;
};

} // namespace tw
