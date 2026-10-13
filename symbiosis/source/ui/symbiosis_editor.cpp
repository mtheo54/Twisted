//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_editor.cpp
//------------------------------------------------------------------------
#include "symbiosis_editor.h"

#include "symbiosis_controller.h"
#include "version.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cresourcedescription.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformbitmap.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/linux/x11frame.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iterator>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace VSTGUI;
using namespace Steinberg;

namespace Symbiosis {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr CCoord kW = SymbiosisEditor::kCanvasWidth;
constexpr CCoord kH = SymbiosisEditor::kCanvasHeight;
const CPoint kOrbit (430, 245); // centre of the astrolabe

// radii of the astrolabe (canvas units)
constexpr double kWaveMin = 96.0;   // waves: level 0
constexpr double kWaveSpan = 52.0;  // waves: level 1 at 148
constexpr double kDiscR = 155.0;    // light inner disc
constexpr double kRingIn = 158.0;   // thick gunmetal ring
constexpr double kRingOut = 183.0;

//------------------------------------------------------------------------
// Ivory & ink: paper, charcoal ink, gunmetal, black chrome, red accents
namespace Pal {
const CColor ink (0x24, 0x24, 0x26, 0xFF);      // text and line art
const CColor inkSoft (0x55, 0x55, 0x59, 0xFF);
const CColor paper (0xF1, 0xF0, 0xEE, 0xFF);
const CColor paperShade (0xDC, 0xDB, 0xD8, 0xFF);
const CColor steel (0x8E, 0x90, 0x95, 0xFF);
const CColor steelLight (0xE4, 0xE5, 0xE8, 0xFF);
const CColor steelDark (0x2B, 0x2C, 0x2F, 0xFF);
const CColor red (0xD8, 0x1C, 0x2A, 0xFF);      // accent
const CColor redGlow (0xE0, 0x14, 0x24, 0xFF);
const CColor redDeep (0x8A, 0x08, 0x12, 0xFF);
const CColor ledRed (0xFF, 0x3C, 0x3C, 0xFF);
const CColor ledWhite (0xF4, 0xF6, 0xFA, 0xFF);
const CColor white (0xFF, 0xFF, 0xFF, 0xFF);
const CColor black (0x00, 0x00, 0x00, 0xFF);
} // namespace Pal

CColor alpha (CColor c, double a)
{
	c.alpha = static_cast<uint8_t> (std::clamp (a, 0.0, 1.0) * c.alpha + 0.5);
	return c;
}

CColor mix (const CColor& a, const CColor& b, double t)
{
	t = std::clamp (t, 0.0, 1.0);
	auto lerp = [t] (uint8_t x, uint8_t y) { return static_cast<uint8_t> (x + (y - x) * t + 0.5); };
	return CColor (lerp (a.red, b.red), lerp (a.green, b.green), lerp (a.blue, b.blue), lerp (a.alpha, b.alpha));
}

double deg (double d) { return d * kPi / 180.0; }

CPoint polar (const CPoint& c, double radius, double angleDeg)
{
	return CPoint (c.x + radius * std::cos (deg (angleDeg)), c.y + radius * std::sin (deg (angleDeg)));
}

CRect circleRect (const CPoint& c, double r) { return CRect (c.x - r, c.y - r, c.x + r, c.y + r); }

SharedPointer<CGraphicsPath> newPath (CDrawContext* ctx) { return VSTGUI::owned (ctx->createGraphicsPath ()); }

void strokePath (CDrawContext* ctx, CGraphicsPath* path, const CColor& color, CCoord width)
{
	ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	ctx->setLineWidth (width);
	ctx->setFrameColor (color);
	ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

/** Stroke for dense polylines (spectrum waves): bevel joins, far cheaper to tessellate than round. */
void strokeWave (CDrawContext* ctx, CGraphicsPath* path, const CColor& color, CCoord width)
{
	ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinBevel));
	ctx->setLineWidth (width);
	ctx->setFrameColor (color);
	ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void drawLine (CDrawContext* ctx, const CPoint& a, const CPoint& b, const CColor& color, CCoord width)
{
	ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapRound));
	ctx->setLineWidth (width);
	ctx->setFrameColor (color);
	ctx->drawLine (a, b);
}

void fillCircle (CDrawContext* ctx, const CPoint& c, double r, const CColor& color)
{
	ctx->setFillColor (color);
	ctx->drawEllipse (circleRect (c, r), kDrawFilled);
}

void strokeCircle (CDrawContext* ctx, const CPoint& c, double r, const CColor& color, CCoord width)
{
	ctx->setLineStyle (kLineSolid);
	ctx->setLineWidth (width);
	ctx->setFrameColor (color);
	ctx->drawEllipse (circleRect (c, r), kDrawStroked);
}

void strokeArc (CDrawContext* ctx, const CPoint& c, double r, double fromDeg, double toDeg, const CColor& color,
                CCoord width)
{
	if (auto p = newPath (ctx))
	{
		const int steps = std::max (2, static_cast<int> (std::abs (toDeg - fromDeg) / 2.0));
		for (int i = 0; i <= steps; ++i)
		{
			const CPoint q = polar (c, r, fromDeg + (toDeg - fromDeg) * i / steps);
			i == 0 ? p->beginSubpath (q) : p->addLine (q);
		}
		strokePath (ctx, p, color, width);
	}
}

/** Soft glow: radial gradient disc. */
void glow (CDrawContext* ctx, const CPoint& c, double r, const CColor& color, double strength)
{
	if (auto p = newPath (ctx))
	{
		p->addEllipse (circleRect (c, r));
		if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, alpha (color, strength), alpha (color, 0.0))))
		{
			g->addColorStop (0.35, alpha (color, strength * 0.45));
			ctx->fillRadialGradient (p, *g, c, r);
		}
	}
}

enum class Metal
{
	Gunmetal, // dark steel: ring, gears, frames of the controls
	Silver    // brushed aluminium: panel rim, bottom plate
};

/** Brushed metal: diagonal gradient over the bounds of the path. */
void fillMetal (CDrawContext* ctx, CGraphicsPath* path, const CRect& bounds, bool evenOdd = false,
                Metal metal = Metal::Gunmetal)
{
	GradientColorStopMap stops;
	if (metal == Metal::Gunmetal)
	{
		stops.emplace (0.00, CColor (0x26, 0x27, 0x2A, 0xFF));
		stops.emplace (0.18, CColor (0x9A, 0x9D, 0xA3, 0xFF));
		stops.emplace (0.34, CColor (0x45, 0x47, 0x4C, 0xFF));
		stops.emplace (0.50, CColor (0x18, 0x19, 0x1B, 0xFF));
		stops.emplace (0.66, CColor (0x7C, 0x7F, 0x85, 0xFF));
		stops.emplace (0.84, CColor (0x22, 0x23, 0x26, 0xFF));
		stops.emplace (1.00, CColor (0x5C, 0x5E, 0x63, 0xFF));
	}
	else
	{
		stops.emplace (0.00, CColor (0x9C, 0x9E, 0xA2, 0xFF));
		stops.emplace (0.20, CColor (0xF6, 0xF6, 0xF7, 0xFF));
		stops.emplace (0.45, CColor (0xC4, 0xC6, 0xCA, 0xFF));
		stops.emplace (0.65, CColor (0xE9, 0xEA, 0xEC, 0xFF));
		stops.emplace (1.00, CColor (0xA6, 0xA8, 0xAC, 0xFF));
	}
	if (auto g = VSTGUI::owned (CGradient::create (stops)))
		ctx->fillLinearGradient (path, *g, bounds.getTopLeft (), bounds.getBottomRight (), evenOdd);
}

void metalAnnulus (CDrawContext* ctx, const CPoint& c, double rIn, double rOut)
{
	if (auto p = newPath (ctx))
	{
		p->addEllipse (circleRect (c, rOut));
		p->addEllipse (circleRect (c, rIn));
		fillMetal (ctx, p, circleRect (c, rOut), true);
	}
	strokeCircle (ctx, c, rOut, CColor (0x08, 0x08, 0x09, 0xC0), 0.8);
	strokeCircle (ctx, c, rIn, CColor (0x08, 0x08, 0x09, 0xC0), 0.8);
}

/** Black chrome sphere (rivets, ornaments, screws). */
void metalSphere (CDrawContext* ctx, const CPoint& c, double r)
{
	fillCircle (ctx, CPoint (c.x + r * 0.15, c.y + r * 0.25), r * 1.1, CColor (0, 0, 0, 0x60));
	if (auto p = newPath (ctx))
	{
		p->addEllipse (circleRect (c, r));
		GradientColorStopMap stops;
		stops.emplace (0.0, CColor (0xF4, 0xF6, 0xF8, 0xFF));
		stops.emplace (0.25, CColor (0x9A, 0x9D, 0xA3, 0xFF));
		stops.emplace (0.6, CColor (0x2B, 0x2D, 0x31, 0xFF));
		stops.emplace (1.0, CColor (0x0A, 0x0A, 0x0B, 0xFF));
		if (auto g = VSTGUI::owned (CGradient::create (stops)))
			ctx->fillRadialGradient (p, *g, CPoint (c.x - r * 0.35, c.y - r * 0.4), r * 1.6);
	}
}

//------------------------------------------------------------------------
/** Montserrat (SIL OFL), shipped in Resources/Fonts. */
struct UiFonts
{
	SharedPointer<CFontDesc> title = makeOwned<CFontDesc> ("Montserrat", 21);
	SharedPointer<CFontDesc> label = makeOwned<CFontDesc> ("Montserrat", 10);
	SharedPointer<CFontDesc> small = makeOwned<CFontDesc> ("Montserrat", 8.5);
	SharedPointer<CFontDesc> tiny = makeOwned<CFontDesc> ("Montserrat", 6.5);
	SharedPointer<CFontDesc> menu = makeOwned<CFontDesc> ("Montserrat", 10);
	SharedPointer<CFontDesc> knob = makeOwned<CFontDesc> ("Montserrat", 17);
	SharedPointer<CFontDesc> readout = makeOwned<CFontDesc> ("Montserrat", 37);
	SharedPointer<CFontDesc> caption = makeOwned<CFontDesc> ("Montserrat", 7.5, kBoldFace);
};

/**
 * Textures rendered offline by tools/generate_assets.py, shipped in <bundle>/Resources.
 * Each PNG has a fixed pixel density (pixels per canvas unit), declared to VSTGUI so the
 * bitmaps draw at their canvas size and stay sharp at 120 % zoom and on HiDPI screens.
 * A missing texture falls back to a plain vector drawing.
 */
constexpr double kAssetScaleBackground = 2.0;
constexpr double kAssetScaleRing = 2.4;
constexpr double kAssetScaleKnob = 3.0;
constexpr double kAssetScalePlasma = 2.0;
constexpr double kRingImageRadius = 245.0;  // half size of astrolabe_ring.png, canvas units
constexpr double kKnobImageRadius = 52.0;   // half size of the knob textures
constexpr double kPlasmaImageRadius = 160.0;
constexpr double kGemPeriod = 45.0;         // degrees: the cut repeats every 45 degrees

SharedPointer<CBitmap> loadTexture (const char* name, double scale)
{
	auto bitmap = makeOwned<CBitmap> (CResourceDescription (name));
	auto platformBitmap = bitmap->getPlatformBitmap ();
	if (!platformBitmap)
		return nullptr;
	platformBitmap->setScaleFactor (scale);
	return bitmap;
}

struct UiTextures
{
	SharedPointer<CBitmap> background = loadTexture ("background.png", kAssetScaleBackground);
	SharedPointer<CBitmap> ring = loadTexture ("astrolabe_ring.png", kAssetScaleRing);
	SharedPointer<CBitmap> bezel = loadTexture ("knob_bezel.png", kAssetScaleKnob);
	SharedPointer<CBitmap> crystal = loadTexture ("knob_gem_smoke.png", kAssetScaleKnob); // filmstrip
	SharedPointer<CBitmap> cap = loadTexture ("knob_cap.png", kAssetScaleKnob);
	SharedPointer<CBitmap> gloss = loadTexture ("knob_gloss.png", kAssetScaleKnob);
	SharedPointer<CBitmap> plasmaA = loadTexture ("plasma_a.png", kAssetScalePlasma); // ink smoke
	SharedPointer<CBitmap> plasmaB = loadTexture ("plasma_b.png", kAssetScalePlasma); // red mist
};

UiFonts* fontsInstance = nullptr;
UiTextures* texturesInstance = nullptr;
int fontsUsers = 0;
void acquireFonts ()
{
	if (fontsUsers++ == 0)
	{
		fontsInstance = new UiFonts;
		texturesInstance = new UiTextures;
	}
}
void releaseFonts ()
{
	if (--fontsUsers == 0)
	{
		delete fontsInstance;
		delete texturesInstance;
		fontsInstance = nullptr;
		texturesInstance = nullptr;
	}
}
UiTextures& textures () { return *texturesInstance; }
UiFonts& fonts () { return *fontsInstance; }

/** Number of square cells of a vertical filmstrip. */
int filmstripFrames (const CBitmap* bitmap)
{
	return std::max (1, static_cast<int> (std::lround (bitmap->getHeight () / bitmap->getWidth ())));
}

/**
 * Draws a texture centred on c (no rotation: VSTGUI's Cairo backend clips rotated bitmaps
 * wrongly, so motion comes from filmstrip frames and cross-fades instead).
 * frame selects a cell of a vertical filmstrip whose cells are halfSize * 2 tall.
 */
void drawTexture (CDrawContext* ctx, CBitmap* bitmap, const CPoint& c, double halfSize, float opacity = 1.f,
                  int frame = 0)
{
	ctx->setBitmapInterpolationQuality (BitmapInterpolationQuality::kHigh);
	bitmap->draw (ctx, circleRect (c, halfSize), CPoint (0, frame * 2.0 * halfSize), opacity);
}

void drawCentered (CDrawContext* ctx, const std::string& s, CCoord cx, CCoord baseline)
{
	ctx->drawString (s.c_str (), CPoint (cx - ctx->getStringWidth (s.c_str ()) * 0.5, baseline));
}

void drawTracked (CDrawContext* ctx, const std::string& s, CPoint origin, CCoord tracking)
{
	for (size_t i = 0; i < s.size ();)
	{
		const auto lead = static_cast<unsigned char> (s[i]);
		const size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
		const std::string glyph = s.substr (i, len);
		ctx->drawString (glyph.c_str (), origin);
		origin.x += ctx->getStringWidth (glyph.c_str ()) + tracking;
		i += len;
	}
}

/** Charcoal label, lightly embossed into the paper. */
void drawLabel (CDrawContext* ctx, const std::string& s, CCoord cx, CCoord baseline)
{
	ctx->setFont (fonts ().label);
	ctx->setFontColor (CColor (0xFF, 0xFF, 0xFF, 0xC0));
	drawCentered (ctx, s, cx + 0.4, baseline + 0.8);
	ctx->setFontColor (Pal::ink);
	drawCentered (ctx, s, cx, baseline);
}

/** Text with a soft halo. */
void drawGlowText (CDrawContext* ctx, const std::string& s, CCoord cx, CCoord baseline, const CColor& color,
                   const CColor& halo)
{
	const CCoord w = ctx->getStringWidth (s.c_str ());
	for (int ring = 3; ring >= 1; --ring)
	{
		ctx->setFontColor (alpha (halo, 0.10 * (4 - ring)));
		for (int k = 0; k < 8; ++k)
		{
			const CPoint o = polar (CPoint (0, 0), ring * 1.2, k * 45.0);
			ctx->drawString (s.c_str (), CPoint (cx - w * 0.5 + o.x, baseline + o.y));
		}
	}
	ctx->setFontColor (color);
	ctx->drawString (s.c_str (), CPoint (cx - w * 0.5, baseline));
}

/**
 * Renders `paint` once into a transparent offscreen bitmap at the exact device resolution
 * (screen scale x editor zoom): later frames only copy pixels, no resampling, no paths.
 */
template <typename Paint>
SharedPointer<CBitmap> renderToCache (CFrame* frame, const CPoint& size, Paint&& paint, double resolution = 1.0)
{
	const double scale = (frame ? frame->getScaleFactor () * frame->getZoom () : 1.0) * resolution;
	auto off = COffscreenContext::create (size, scale);
	if (!off)
		return nullptr;
	off->beginDraw ();
	off->clearRect (CRect (0, 0, size.x * scale, size.y * scale));
	{
		CDrawContext::Transform t (*off, CGraphicsTransform ().scale (scale, scale));
		off->setDrawMode (kAntiAliasing | kNonIntegralMode);
		paint (off.get ());
	}
	off->endDraw ();
	return off->getBitmap ();
}

/** Smooth value noise (halftone field of the orbit). */
float hash2 (int x, int y)
{
	uint32_t h = static_cast<uint32_t> (x) * 374761393u + static_cast<uint32_t> (y) * 668265263u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return static_cast<float> ((h ^ (h >> 16)) & 0xFFFFFFu) / static_cast<float> (0xFFFFFF);
}

float valueNoise (float x, float y)
{
	const float fx = std::floor (x), fy = std::floor (y);
	const int xi = static_cast<int> (fx), yi = static_cast<int> (fy);
	const float tx = x - fx, ty = y - fy;
	const float u = tx * tx * (3.f - 2.f * tx), v = ty * ty * (3.f - 2.f * ty);
	const float a = hash2 (xi, yi), b = hash2 (xi + 1, yi), c = hash2 (xi, yi + 1), d = hash2 (xi + 1, yi + 1);
	return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

/** Four-pointed star. */
void sparkle (CDrawContext* ctx, const CPoint& c, double r, const CColor& color)
{
	if (auto p = newPath (ctx))
	{
		const double k = r * 0.22;
		p->beginSubpath (c.x, c.y - r);
		p->addLine (c.x + k, c.y - k);
		p->addLine (c.x + r, c.y);
		p->addLine (c.x + k, c.y + k);
		p->addLine (c.x, c.y + r);
		p->addLine (c.x - k, c.y + k);
		p->addLine (c.x - r, c.y);
		p->addLine (c.x - k, c.y - k);
		p->closeSubpath ();
		ctx->setFillColor (color);
		ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
	}
}

/** Toothed gunmetal gear with spokes. */
void gear (CDrawContext* ctx, const CPoint& c, double r, int teeth, double rotation)
{
	fillCircle (ctx, CPoint (c.x + 2, c.y + 3), r + 1, CColor (0, 0, 0, 0x40));
	if (auto p = newPath (ctx))
	{
		const double root = r - 4.0;
		const double step = 360.0 / teeth;
		for (int i = 0; i < teeth; ++i)
		{
			const double a = rotation + step * i;
			const CPoint p0 = polar (c, root, a);
			i == 0 ? p->beginSubpath (p0) : p->addLine (p0);
			p->addLine (polar (c, r, a + step * 0.18));
			p->addLine (polar (c, r, a + step * 0.48));
			p->addLine (polar (c, root, a + step * 0.66));
		}
		p->closeSubpath ();
		p->addEllipse (circleRect (c, r * 0.72));
		fillMetal (ctx, p, circleRect (c, r), true);
	}
	// spokes and hub inside the open ring
	fillCircle (ctx, c, r * 0.72, CColor (0xE4, 0xE3, 0xE1, 0xD8));
	for (int s = 0; s < 5; ++s)
	{
		const double a = rotation + 72.0 * s;
		if (auto p = newPath (ctx))
		{
			p->beginSubpath (polar (c, r * 0.2, a - 14));
			p->addLine (polar (c, r * 0.74, a - 5));
			p->addLine (polar (c, r * 0.74, a + 5));
			p->addLine (polar (c, r * 0.2, a + 14));
			p->closeSubpath ();
			fillMetal (ctx, p, circleRect (c, r));
		}
	}
	metalAnnulus (ctx, c, r * 0.6, r * 0.74);
	metalSphere (ctx, c, r * 0.24);
}

//------------------------------------------------------------------------
// Layout (canvas units), measured on the reference artwork
const CPoint kPullCentre (102, 172);
const CPoint kHarmonicsCentre (764, 172);
constexpr CCoord kSliderTop = 262, kSliderBottom = 388;
constexpr CCoord kLeftCols[3] = {63, 132, 206};
constexpr CCoord kRightCols[3] = {656, 730, 797};
constexpr CCoord kRowY[2] = {449, 488};
const CRect kScopeTop (348, 434, 512, 460);
const CRect kScopeBottom (348, 473, 512, 499);

//------------------------------------------------------------------------
/** Everything that never moves, rendered once into an offscreen bitmap. */
class BackgroundView : public CView
{
public:
	explicit BackgroundView (const CRect& size) : CView (size) { setMouseEnabled (false); }

	void draw (CDrawContext* ctx) override
	{
		if (!cache)
			cache = renderToCache (getFrame (), CPoint (kW, kH), [this] (CDrawContext* c) { paint (c); });
		if (cache)
			cache->draw (ctx, getViewSize ());
		else
			paint (ctx);
	}

	CLASS_METHODS_NOCOPY (BackgroundView, CView)

private:
	void paint (CDrawContext* ctx)
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		ctx->setFillColor (Pal::paper);
		ctx->drawRect (CRect (0, 0, kW, kH), kDrawFilled);
		if (auto* background = textures ().background.get ())
			background->draw (ctx, CRect (0, 0, kW, kH)); // paper, ink smoke, specks, darkened periphery

		paintConstellations (ctx);
		paintOrnaments (ctx);
		paintAstrolabe (ctx);
		paintPanel (ctx);
		paintLabels (ctx);
	}

	void paintConstellations (CDrawContext* ctx)
	{
		auto constellation = [&] (std::initializer_list<CPoint> pts) {
			const CPoint* prev = nullptr;
			for (const auto& p : pts)
			{
				if (prev)
					drawLine (ctx, *prev, p, alpha (Pal::ink, 0.7), 0.6);
				prev = &p;
			}
			for (const auto& p : pts)
				fillCircle (ctx, p, 1.5, Pal::ink);
		};
		constellation ({{180, 107}, {193, 103}, {205, 117}, {214, 130}, {196, 133}});
		constellation ({{171, 339}, {178, 360}, {186, 377}, {176, 394}});
		constellation ({{676, 195}, {690, 183}, {704, 211}, {695, 236}, {716, 255}, {712, 270}});
		constellation ({{738, 287}, {752, 296}, {766, 322}, {745, 349}, {770, 362}});
		constellation ({{646, 330}, {660, 318}, {668, 342}});
		// red ink haze around the PULL knob, as a splash on the paper
		glow (ctx, CPoint (148, 132), 34, Pal::redGlow, 0.10);
		glow (ctx, CPoint (60, 210), 26, Pal::redGlow, 0.08);
	}

	void paintOrnaments (CDrawContext* ctx)
	{
		const CColor line = alpha (Pal::ink, 0.8);
		auto curve = [&] (CPoint a, CPoint c1, CPoint c2, CPoint b, CCoord width, bool mirror) {
			for (int side = 0; side < (mirror ? 2 : 1); ++side)
			{
				auto mx = [side] (CPoint p) { return side ? CPoint (kW - p.x, p.y) : p; };
				if (auto p = newPath (ctx))
				{
					p->beginSubpath (mx (a));
					p->addBezierCurve (mx (c1), mx (c2), mx (b));
					strokePath (ctx, p, line, width);
				}
			}
		};
		// header rules flowing into the astrolabe
		curve ({14, 47}, {120, 47}, {230, 47}, {300, 47}, 1.0, true);
		curve ({300, 47}, {330, 47}, {350, 52}, {368, 62}, 1.0, true);
		curve ({240, 52}, {290, 54}, {320, 66}, {336, 86}, 0.8, true);
		// vines on the side panels
		curve ({160, 60}, {215, 66}, {236, 100}, {238, 150}, 0.8, true);
		curve ({238, 150}, {240, 200}, {222, 240}, {200, 262}, 0.8, true);
		curve ({182, 72}, {222, 84}, {252, 120}, {256, 170}, 0.7, true);
		curve ({215, 286}, {245, 318}, {252, 362}, {256, 410}, 0.8, true);
		curve ({256, 410}, {260, 455}, {288, 488}, {340, 504}, 0.8, true);
		curve ({232, 272}, {268, 300}, {280, 350}, {276, 398}, 0.7, true);
		curve ({276, 398}, {272, 440}, {250, 474}, {222, 504}, 0.7, true);
		curve ({226, 300}, {200, 330}, {205, 370}, {230, 380}, 0.6, true);
		// edge curls
		curve ({848, 300}, {826, 330}, {826, 400}, {846, 440}, 0.9, false);
		curve ({836, 330}, {818, 360}, {822, 410}, {836, 430}, 0.7, false);
		curve ({14, 300}, {30, 340}, {30, 400}, {14, 440}, 0.7, false);

		// crescent moons in their rings
		for (const CPoint& m : {CPoint (300, 466), CPoint (560, 466)})
		{
			strokeCircle (ctx, m, 10, alpha (Pal::ink, 0.85), 0.9);
			if (auto p = newPath (ctx))
			{
				p->addEllipse (circleRect (m, 6.5));
				p->addEllipse (circleRect (CPoint (m.x + (m.x < 430 ? 3.2 : -3.2), m.y - 2.4), 5.6));
				ctx->setFillColor (Pal::ink);
				ctx->drawGraphicsPath (p, CDrawContext::kPathFilledEvenOdd);
			}
		}
		// small ink stars around the astrolabe
		for (const CPoint& s : {CPoint (418, 435), CPoint (595, 425), CPoint (265, 150), CPoint (618, 140), CPoint (241, 260)})
			sparkle (ctx, s, 5, alpha (Pal::ink, 0.9));
	}

	void paintAstrolabe (CDrawContext* ctx)
	{
		const CPoint c = kOrbit;

		// light inner disc, halftone field, red level rings
		if (auto p = newPath (ctx))
		{
			p->addEllipse (circleRect (c, kDiscR + 3));
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0xF7, 0xF6, 0xF4, 0xF2), CColor (0xD9, 0xD8, 0xD5, 0xF8))))
				ctx->fillRadialGradient (p, *g, c, kDiscR + 3);
		}
		std::mt19937 rng (77u);
		std::uniform_real_distribution<double> u (0.0, 1.0);
		for (double y = -kDiscR; y <= kDiscR; y += 3.4)
		{
			for (double x = -kDiscR; x <= kDiscR; x += 3.4)
			{
				const double r = std::sqrt (x * x + y * y);
				if (r < 86 || r > kDiscR - 4)
					continue;
				const double n = valueNoise (static_cast<float> (x * 0.03 + 4), static_cast<float> (y * 0.03 + 9));
				const double a = std::clamp ((n - 0.32) * 0.6, 0.0, 0.32) * (0.6 + 0.4 * u (rng));
				if (a > 0.02)
					fillCircle (ctx, CPoint (c.x + x, c.y + y), 0.6, alpha (Pal::ink, a));
			}
		}
		for (int i = 0; i < 70; ++i)
		{
			const CPoint s (c.x + (u (rng) - 0.5) * 300, c.y + (u (rng) - 0.5) * 300);
			const double r = 0.5 + 0.7 * u (rng);
			fillCircle (ctx, s, r, alpha (i % 4 == 0 ? Pal::red : Pal::ink, 0.35 + 0.45 * u (rng)));
		}

		ctx->setFont (fonts ().tiny);
		const double levels[] = {0.2, 0.5, 0.8};
		const double labelAngles[3][2] = {{-122.0, 128.0}, {-58.0, 55.0}, {-105.0, 72.0}};
		for (int k = 0; k < 3; ++k)
		{
			const double r = kWaveMin + kWaveSpan * levels[k];
			strokeCircle (ctx, c, r, alpha (Pal::red, 0.45), 0.6);
			char buf[8];
			std::snprintf (buf, sizeof buf, "%.1f", levels[k]);
			for (double a : labelAngles[k])
			{
				const CPoint q = polar (c, r, a);
				ctx->setFontColor (alpha (Pal::ink, 0.7));
				drawCentered (ctx, buf, q.x, q.y - 2.5);
			}
		}
		strokeCircle (ctx, c, kDiscR - 2, alpha (Pal::ink, 0.6), 0.8);

		// crosshair
		const CColor cross = alpha (Pal::ink, 0.45);
		drawLine (ctx, CPoint (c.x - kDiscR + 2, c.y), CPoint (c.x - 72, c.y), cross, 0.7);
		drawLine (ctx, CPoint (c.x + 72, c.y), CPoint (c.x + kDiscR - 2, c.y), cross, 0.7);
		drawLine (ctx, CPoint (c.x, c.y - kDiscR + 2), CPoint (c.x, c.y - 72), cross, 0.7);
		drawLine (ctx, CPoint (c.x, c.y + 72), CPoint (c.x, c.y + kDiscR - 2), cross, 0.7);
		sparkle (ctx, CPoint (c.x, c.y - kDiscR + 8), 3.5, Pal::ink);

		// gunmetal ring, toothed crown, rune band (texture; plain rings otherwise)
		if (auto* ring = textures ().ring.get ())
			drawTexture (ctx, ring, c, kRingImageRadius);
		else
		{
			metalAnnulus (ctx, c, kRingIn, kRingOut);
			strokeCircle (ctx, c, 214, alpha (Pal::ink, 0.8), 28.0);
		}

		// ORBIT VISUALIZER plate just inside the ring, at the bottom
		const double plateOut = kRingIn - 0.5, plateIn = kRingIn - 12.5;
		if (auto p = newPath (ctx))
		{
			for (int i = 0; i <= 30; ++i)
			{
				const CPoint q = polar (c, plateOut, 62.0 + 56.0 * i / 30);
				i == 0 ? p->beginSubpath (q) : p->addLine (q);
			}
			for (int i = 30; i >= 0; --i)
				p->addLine (polar (c, plateIn, 62.0 + 56.0 * i / 30));
			p->closeSubpath ();
			ctx->setFillColor (CColor (0xEC, 0xEB, 0xE8, 0xF4));
			ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
		}
		strokeArc (ctx, c, plateIn, 62.0, 118.0, alpha (Pal::ink, 0.8), 0.9);
		{
			ctx->setFont (fonts ().caption);
			ctx->setFontColor (Pal::ink);
			const std::string label = "ORBIT VISUALIZER";
			const double radius = kRingIn - 3.2; // baseline; glyphs rise toward the centre
			double total = 0.0;
			for (char ch : label)
				total += ctx->getStringWidth (std::string (1, ch).c_str ()) + 1.2;
			double a = 90.0 + (total * 0.5) / radius * 180.0 / kPi;
			for (char ch : label)
			{
				const std::string str (1, ch);
				const double w = ctx->getStringWidth (str.c_str ()) + 1.2;
				const double mid = a - (w * 0.5) / radius * 180.0 / kPi;
				const CPoint q = polar (c, radius, mid);
				CDrawContext::Transform t (*ctx, CGraphicsTransform ().rotate (mid - 90.0).translate (q.x, q.y));
				ctx->drawString (str.c_str (), CPoint (-(w - 1.2) * 0.5, 0));
				a -= w / radius * 180.0 / kPi;
			}
		}

		// big rivets either side of the astrolabe
		for (double a : {180.0, 0.0})
		{
			const CPoint r = polar (c, 233, a);
			metalAnnulus (ctx, r, 8.0, 10.5);
			metalSphere (ctx, r, 7.0);
		}

		// rivets on the ring and the crown ornament
		for (double a : {180.0, 0.0, 228.0, 312.0})
			metalSphere (ctx, polar (c, (kRingIn + kRingOut) * 0.5, a), 5.5);
		drawLine (ctx, CPoint (c.x, c.y - 236), CPoint (c.x, c.y - 172), Pal::ink, 1.2);
		metalAnnulus (ctx, CPoint (c.x, c.y - 203), 13, 16.5);
		metalSphere (ctx, CPoint (c.x, c.y - 203), 12.5);

		// gears
		gear (ctx, CPoint (583, 116), 30, 22, 8.0);
		gear (ctx, CPoint (556, 92), 15, 12, 4.0);
		metalSphere (ctx, CPoint (585, 96), 7.5);
		strokeCircle (ctx, CPoint (520, 72), 4.5, Pal::ink, 1.0);
		fillCircle (ctx, CPoint (520, 72), 1.5, Pal::ink);
		gear (ctx, CPoint (283, 374), 36, 26, 3.0);
		gear (ctx, CPoint (312, 400), 16, 12, 10.0);
		metalSphere (ctx, CPoint (272, 392), 8.0);
	}

	void paintPanel (CDrawContext* ctx)
	{
		// brushed aluminium bottom plate
		const CRect plate (4, 510, kW - 4, kH - 2);
		if (auto p = newPath (ctx))
		{
			p->addRect (plate);
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0xE2, 0xE3, 0xE5, 0xFF), CColor (0xB2, 0xB4, 0xB8, 0xFF))))
				ctx->fillLinearGradient (p, *g, plate.getTopLeft (), plate.getBottomLeft ());
		}
		std::mt19937 rng (0xA1u);
		std::uniform_real_distribution<double> u (0.0, 1.0);
		for (int i = 0; i < 220; ++i)
		{
			const CCoord y = plate.top + 1 + u (rng) * (plate.getHeight () - 2);
			const CCoord x0 = plate.left + u (rng) * plate.getWidth ();
			const CCoord len = 40 + 260 * u (rng);
			const bool light = u (rng) > 0.5;
			drawLine (ctx, CPoint (x0, y), CPoint (std::min (plate.right, x0 + len), y),
			          light ? CColor (0xFF, 0xFF, 0xFF, 0x50) : CColor (0x70, 0x72, 0x76, 0x30), 0.5);
		}
		drawLine (ctx, CPoint (plate.left, plate.top + 0.5), CPoint (plate.right, plate.top + 0.5), CColor (0xFF, 0xFF, 0xFF, 0xB0), 1.0);
		const CColor deco = alpha (Pal::ink, 0.75);
		auto mirrorLine = [&] (CPoint a, CPoint b) {
			drawLine (ctx, a, b, deco, 0.8);
			drawLine (ctx, CPoint (kW - a.x, a.y), CPoint (kW - b.x, b.y), deco, 0.8);
		};
		mirrorLine ({70, 511}, {125, 556});
		mirrorLine ({95, 556}, {150, 513});
		mirrorLine ({118, 511}, {175, 548});
		mirrorLine ({60, 535}, {165, 528});
		mirrorLine ({398, 511}, {430, 538});
		mirrorLine ({410, 511}, {430, 528});
		mirrorLine ({385, 556}, {430, 546});

		// silver panel rim and corner screws
		auto frameLine = [&] (CRect r, CCoord radius, const CColor& col, CCoord w) {
			if (auto p = newPath (ctx))
			{
				p->addRoundRect (r, radius);
				strokePath (ctx, p, col, w);
			}
		};
		frameLine (CRect (2, 2, kW - 2, kH - 2), 14, CColor (0x8A, 0x8C, 0x90, 0xFF), 2.0);
		frameLine (CRect (4, 4, kW - 4, kH - 4), 13, CColor (0xFF, 0xFF, 0xFF, 0xC0), 1.2);
		frameLine (CRect (6, 6, kW - 6, 508), 11, CColor (0x6E, 0x70, 0x75, 0xFF), 1.0);
		frameLine (CRect (7.2, 7.2, kW - 7.2, 506.8), 10, CColor (0xFF, 0xFF, 0xFF, 0x90), 0.8);
		drawLine (ctx, CPoint (6, 509), CPoint (kW - 6, 509), CColor (0x6E, 0x70, 0x75, 0xFF), 1.2);
		for (const CPoint& s : {CPoint (16, 497), CPoint (kW - 16, 497), CPoint (16, 545), CPoint (kW - 16, 545)})
		{
			fillCircle (ctx, CPoint (s.x + 0.6, s.y + 0.8), 5.6, CColor (0xFF, 0xFF, 0xFF, 0x90));
			metalSphere (ctx, s, 5.0);
		}

		// logo and title
		const CPoint logo (31, 24);
		strokeArc (ctx, logo, 10.5, 140, 400, Pal::ink, 1.7);
		strokeArc (ctx, CPoint (logo.x - 2.5, logo.y - 2), 5.5, 200, 380, Pal::ink, 1.7);
		strokeArc (ctx, CPoint (logo.x + 2.5, logo.y + 2), 5.5, 20, 200, Pal::ink, 1.7);
		ctx->setFont (fonts ().title);
		ctx->setFontColor (CColor (0xFF, 0xFF, 0xFF, 0xD0));
		drawTracked (ctx, "SYMBIOSIS", CPoint (50.5, 32.5), 1.6);
		ctx->setFontColor (CColor (0x2E, 0x2E, 0x31, 0xFF));
		drawTracked (ctx, "SYMBIOSIS", CPoint (50, 31.5), 1.6);
		drawLine (ctx, CPoint (546, 47), CPoint (kW - 14, 47), alpha (Pal::ink, 0.8), 1.0);

		// scope frames and their bracket
		for (const CRect& r : {kScopeTop, kScopeBottom})
		{
			if (auto p = newPath (ctx))
			{
				p->addRoundRect (r, 3);
				if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0xD8, 0xD8, 0xD6, 0xFF), CColor (0xF6, 0xF6, 0xF5, 0xFF))))
					ctx->fillLinearGradient (p, *g, r.getTopLeft (), r.getBottomLeft ());
				strokePath (ctx, p, CColor (0x1E, 0x1E, 0x20, 0xFF), 1.4);
			}
			drawLine (ctx, CPoint (r.left + 4, r.getCenter ().y), CPoint (r.right - 4, r.getCenter ().y), alpha (Pal::ink, 0.15), 0.6);
		}
		const CColor br = alpha (Pal::ink, 0.85);
		drawLine (ctx, CPoint (334, kScopeTop.getCenter ().y), CPoint (348, kScopeTop.getCenter ().y), br, 1.0);
		drawLine (ctx, CPoint (334, kScopeBottom.getCenter ().y), CPoint (348, kScopeBottom.getCenter ().y), br, 1.0);
		drawLine (ctx, CPoint (334, kScopeTop.getCenter ().y), CPoint (334, kScopeBottom.getCenter ().y), br, 1.0);
		drawLine (ctx, CPoint (318, 466), CPoint (334, 466), br, 1.0);
	}

	void paintLabels (CDrawContext* ctx)
	{
		drawLabel (ctx, "PULL", kPullCentre.x, 242);
		drawLabel (ctx, "HARMONICS", kHarmonicsCentre.x, 242);
		drawLabel (ctx, "INERTIA", 67, 405);
		drawLabel (ctx, "REACTION", 137, 405);
		drawLabel (ctx, "DYNAMIC", 730, 403);
		drawLabel (ctx, "TRACKING", 730, 415);
		drawLabel (ctx, "TRANS", 799, 403);
		drawLabel (ctx, "SHIELD", 799, 415);
		const char* left[2][3] = {{"PHASE LINK", "SOLO DELTA", "BYPASS"}, {"EXACT", "HARMONIC", "FREEZE"}};
		const char* right[2][3] = {{"OS 1X", "OS 2X", "OS 4X"}, {"MASTER", "SLAVE", "INPUT"}};
		for (int r = 0; r < 2; ++r)
		{
			for (int k = 0; k < 3; ++k)
			{
				drawLabel (ctx, left[r][k], kLeftCols[k], kRowY[r] - 15);
				drawLabel (ctx, right[r][k], kRightCols[k], kRowY[r] - 15);
			}
		}
	}

	SharedPointer<CBitmap> cache;
};

//------------------------------------------------------------------------
/**
 * Gem knob: PULL is a faceted smoky crystal (filmstrip turning with the value, red tick
 * ring), HARMONICS a glossy black steel cap (charcoal tick ring). Fixed gloss and dark
 * knurled bezel on top.
 */
class GemKnob : public CKnobBase
{
public:
	enum class Style
	{
		Crystal,
		Cap
	};

	GemKnob (const CPoint& centre, IControlListener* listener, int32_t tag, Style s)
	: CKnobBase (circleRect (centre, 66), listener, tag, nullptr), c (centre), style (s)
	{
	}

	/** Double-click, or Ctrl-click (Cmd on macOS), restores the default value. */
	CMouseEventResult onMouseDown (CPoint& where, const CButtonState& buttons) override
	{
		if (buttons.isLeftButton () && (buttons.isDoubleClick () || buttons.getModifierState () == kDefaultValueModifier))
		{
			beginEdit ();
			setValue (getDefaultValue ());
			valueChanged ();
			endEdit ();
			invalid ();
			return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
		}
		return CKnobBase::onMouseDown (where, buttons);
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const double v = getValueNormalized ();
		const bool red = style == Style::Crystal;
		const CColor lit = red ? Pal::red : Pal::ink;
		const CColor unlit = red ? CColor (0x9C, 0x8E, 0x90, 0xC0) : CColor (0xA4, 0xA5, 0xA8, 0xC0);

		if (red)
			glow (ctx, c, 64, Pal::redGlow, 0.16);
		else
			glow (ctx, c, 60, Pal::ink, 0.10);

		// tick ring
		const int ticks = 41;
		for (int i = 0; i < ticks; ++i)
		{
			const double t = static_cast<double> (i) / (ticks - 1);
			const double a = 135.0 + 270.0 * t;
			const bool on = t <= v + 1e-6;
			const bool major = i % 5 == 0;
			const CPoint p0 = polar (c, 50, a), p1 = polar (c, major ? 59 : 56, a);
			if (on && red)
				drawLine (ctx, p0, p1, alpha (lit, 0.25), 3.4);
			drawLine (ctx, p0, p1, on ? lit : unlit, major ? 1.5 : 1.1);
		}
		for (double a : {-90.0, 180.0, 0.0})
		{
			const CPoint s = polar (c, 64, a);
			if (red)
			{
				glow (ctx, s, 10, Pal::redGlow, 0.45);
				sparkle (ctx, s, a == -90.0 ? 6.5 : 5.0, Pal::red);
			}
			else
				sparkle (ctx, s, a == -90.0 ? 5.5 : 4.5, Pal::ink);
		}
		strokeArc (ctx, c, 61.5, 120, 420, alpha (Pal::ink, 0.3), 0.7);

		auto& tex = textures ();
		CBitmap* body = red ? tex.crystal.get () : tex.cap.get ();
		if (body && tex.gloss && tex.bezel)
		{
			// the crystal filmstrip turns the stone with the value (each frame lit offline
			// under the same fixed light); a single-frame texture simply stays put
			const int frames = filmstripFrames (body);
			const double turn = std::fmod (270.0 * v, kGemPeriod);
			const int frame = frames > 1 ? static_cast<int> (std::lround (turn / kGemPeriod * frames)) % frames : 0;
			drawTexture (ctx, body, c, kKnobImageRadius, 1.f, frame);
			drawTexture (ctx, tex.gloss, c, kKnobImageRadius);
			drawTexture (ctx, tex.bezel, c, kKnobImageRadius);
		}
		else
		{
			metalAnnulus (ctx, c, 38, 47);
			fillCircle (ctx, c, 38, CColor (0x14, 0x14, 0x16, 0xFF));
			glow (ctx, CPoint (c.x - 12, c.y - 14), 16, Pal::white, 0.3);
		}

		// value
		char buf[16];
		std::snprintf (buf, sizeof buf, "%d%%", static_cast<int> (std::lround (v * 100.0)));
		ctx->setFont (fonts ().knob);
		ctx->setFontColor (CColor (0x00, 0x00, 0x00, 0xB0));
		drawCentered (ctx, buf, c.x + 0.8, c.y + 7.0);
		ctx->setFontColor (CColor (0xF4, 0xF4, 0xF6, 0xFF));
		drawCentered (ctx, buf, c.x, c.y + 6.2);
	}

	CLASS_METHODS_NOCOPY (GemKnob, CKnobBase)

private:
	CPoint c;
	Style style;
};

//------------------------------------------------------------------------
/** Vertical slider: black slot glowing red or white, ruby thumb in a gunmetal frame. */
class RubySlider : public CControl
{
public:
	RubySlider (CCoord x, IControlListener* listener, int32_t tag, const CColor& glowColor)
	: CControl (CRect (x - 17, kSliderTop - 10, x + 17, kSliderBottom + 10), listener, tag), cx (x), color (glowColor)
	{
	}

	static constexpr CCoord kTravel = kSliderBottom - kSliderTop - 30;

	CCoord thumbTop () const { return kSliderBottom - 30 - kTravel * getValueNormalized (); }

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		if (e.clickCount > 1 || e.modifiers.has (ModifierKey::Control))
		{
			beginEdit ();
			setValueNormalized (getDefaultValue ());
			valueChanged ();
			endEdit ();
			invalid ();
			e.consumed = true;
			return;
		}
		dragging = true;
		startY = e.mousePosition.y;
		startValue = getValueNormalized ();
		beginEdit ();
		e.consumed = true;
	}

	void onMouseMoveEvent (MouseMoveEvent& e) override
	{
		if (!dragging)
			return;
		const double fine = e.modifiers.has (ModifierKey::Shift) ? 0.2 : 1.0;
		setValueNormalized (static_cast<float> (std::clamp (startValue + (startY - e.mousePosition.y) / kTravel * fine, 0.0, 1.0)));
		valueChanged ();
		invalid ();
		e.consumed = true;
	}

	void onMouseUpEvent (MouseUpEvent& e) override
	{
		if (dragging)
		{
			dragging = false;
			endEdit ();
			e.consumed = true;
		}
	}

	void onMouseCancelEvent (MouseCancelEvent& e) override
	{
		if (dragging)
		{
			dragging = false;
			endEdit ();
		}
		e.consumed = true;
	}

	void onMouseWheelEvent (MouseWheelEvent& e) override
	{
		beginEdit ();
		setValueNormalized (static_cast<float> (std::clamp (getValueNormalized () + 0.02 * e.deltaY, 0.0, 1.0)));
		valueChanged ();
		endEdit ();
		invalid ();
		e.consumed = true;
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CCoord ty = thumbTop ();

		// dashes at 100 %, the thumb and 0 %
		for (CCoord y : {kSliderTop + 8, ty + 15, kSliderBottom - 8})
		{
			drawLine (ctx, CPoint (cx - 15, y), CPoint (cx - 9, y), alpha (Pal::ink, 0.7), 0.9);
			drawLine (ctx, CPoint (cx + 9, y), CPoint (cx + 15, y), alpha (Pal::ink, 0.7), 0.9);
		}

		// black slot with a silver lip
		const CRect slot (cx - 4.5, kSliderTop, cx + 4.5, kSliderBottom);
		if (auto p = newPath (ctx))
		{
			p->addRoundRect (slot, 4.5);
			ctx->setFillColor (CColor (0x0C, 0x0C, 0x0D, 0xFF));
			ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
			strokePath (ctx, p, CColor (0x5A, 0x5C, 0x61, 0xFF), 1.1);
		}
		const CPoint lo (cx, kSliderBottom - 4), hi (cx, kSliderTop + 4), mid (cx, ty + 15);
		drawLine (ctx, hi, mid, alpha (color, 0.18), 2.0);
		drawLine (ctx, mid, lo, alpha (color, 0.35), 7.0);
		drawLine (ctx, mid, lo, color, 2.4);
		drawLine (ctx, mid, lo, CColor (0xFF, 0xF4, 0xF4, 0xB0), 0.8);

		// thumb
		const CRect frameRect (cx - 10.5, ty, cx + 10.5, ty + 30);
		fillCircle (ctx, CPoint (cx + 1, ty + 18), 15, CColor (0, 0, 0, 0x35));
		if (auto p = newPath (ctx))
		{
			p->addRoundRect (frameRect, 3.0);
			CRect in (frameRect);
			in.inset (3, 3);
			p->addRoundRect (in, 2.0);
			fillMetal (ctx, p, frameRect, true);
		}
		drawLine (ctx, CPoint (cx, ty - 4), CPoint (cx, ty), Pal::ink, 1.2);
		CRect gem (frameRect);
		gem.inset (3, 3);
		if (auto p = newPath (ctx))
		{
			p->addRoundRect (gem, 2.0);
			GradientColorStopMap stops;
			stops.emplace (0.0, CColor (0xFF, 0xA0, 0xA8, 0xFF));
			stops.emplace (0.3, CColor (0xE8, 0x26, 0x3A, 0xFF));
			stops.emplace (0.7, CColor (0x96, 0x08, 0x1C, 0xFF));
			stops.emplace (1.0, CColor (0x38, 0x02, 0x08, 0xFF));
			if (auto g = VSTGUI::owned (CGradient::create (stops)))
				ctx->fillRadialGradient (p, *g, CPoint (gem.left + 4, gem.top + 5), gem.getHeight ());
		}
		const CPoint gc = gem.getCenter ();
		const CColor facet (0xFF, 0xD8, 0xE0, 0x50);
		drawLine (ctx, gem.getTopLeft (), gc, facet, 0.5);
		drawLine (ctx, gem.getTopRight (), gc, facet, 0.5);
		drawLine (ctx, gem.getBottomLeft (), gc, facet, 0.5);
		drawLine (ctx, gem.getBottomRight (), gc, facet, 0.5);
		drawLine (ctx, CPoint (gem.left, gc.y), CPoint (gem.right, gc.y), CColor (0x40, 0x00, 0x08, 0x50), 0.5);
		glow (ctx, CPoint (gc.x - 2, gem.top + 6), 6, Pal::white, 0.45);
	}

	CLASS_METHODS_NOCOPY (RubySlider, CControl)

private:
	CCoord cx;
	CColor color;
	bool dragging {false};
	double startY {0.0};
	double startValue {0.0};
};

//------------------------------------------------------------------------
/** Black button in a gunmetal frame with a red or white LED. */
class LedButton : public CView
{
public:
	enum class Style
	{
		Bar,
		Jewel
	};

	LedButton (const CPoint& centre, const CColor& led, Style s, std::function<bool ()> on, std::function<void ()> click)
	: CView (CRect (centre.x - 24, centre.y - 9, centre.x + 24, centre.y + 9)), ledColor (led), style (s),
	  isOn (std::move (on)), onClick (std::move (click))
	{
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		if (onClick)
			onClick ();
		invalid ();
		e.consumed = true;
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect r = getViewSize ();
		const bool on = isOn && isOn ();
		if (auto p = newPath (ctx))
		{
			CRect shadow (r);
			shadow.offset (0.6, 1.2);
			p->addRoundRect (shadow, 3.5);
			ctx->setFillColor (CColor (0, 0, 0, 0x35));
			ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
		}
		if (auto p = newPath (ctx))
		{
			p->addRoundRect (r, 3.5);
			CRect in (r);
			in.inset (2.0, 2.0);
			p->addRoundRect (in, 2.0);
			fillMetal (ctx, p, r, true);
		}
		CRect body (r);
		body.inset (2.0, 2.0);
		if (auto p = newPath (ctx))
		{
			p->addRoundRect (body, 2.0);
			const CColor top = CColor (0x34, 0x35, 0x38, 0xFF);
			const CColor bottom = CColor (0x0B, 0x0B, 0x0C, 0xFF);
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, top, bottom)))
				ctx->fillLinearGradient (p, *g, body.getTopLeft (), body.getBottomLeft ());
		}
		drawLine (ctx, CPoint (body.left + 2, body.top + 0.8), CPoint (body.right - 2, body.top + 0.8), CColor (0xFF, 0xFF, 0xFF, 0x30), 0.8);

		const CPoint c = body.getCenter ();
		const CColor led = on ? ledColor : mix (ledColor, CColor (0x2A, 0x2A, 0x2C, 0xFF), 0.82);
		if (style == Style::Bar)
		{
			if (on)
				glow (ctx, c, 13, ledColor, 0.5);
			const CRect bar (c.x - 7, c.y - 1.6, c.x + 7, c.y + 1.6);
			if (auto p = newPath (ctx))
			{
				p->addRoundRect (bar, 1.6);
				ctx->setFillColor (led);
				ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
			}
			if (on)
				drawLine (ctx, CPoint (c.x - 5, c.y), CPoint (c.x + 5, c.y), CColor (0xFF, 0xFF, 0xFF, 0xE0), 0.9);
		}
		else
		{
			drawLine (ctx, CPoint (body.left + 3, c.y), CPoint (c.x - 7, c.y), alpha (Pal::steel, 0.6), 0.8);
			drawLine (ctx, CPoint (c.x + 7, c.y), CPoint (body.right - 3, c.y), alpha (Pal::steel, 0.6), 0.8);
			if (on)
				glow (ctx, c, 11, ledColor, 0.5);
			if (auto p = newPath (ctx))
			{
				p->addEllipse (CRect (c.x - 3.6, c.y - 6.5, c.x + 3.6, c.y + 6.5));
				fillMetal (ctx, p, CRect (c.x - 5, c.y - 8, c.x + 5, c.y + 8), false, Metal::Silver);
			}
			if (auto p = newPath (ctx))
			{
				p->addEllipse (CRect (c.x - 2.5, c.y - 5.2, c.x + 2.5, c.y + 5.2));
				const CColor hi = on ? CColor (0xFF, 0xB8, 0xC0, 0xFF) : CColor (0x7A, 0x30, 0x38, 0xFF);
				const CColor lo = on ? CColor (0xC8, 0x10, 0x24, 0xFF) : CColor (0x30, 0x06, 0x0C, 0xFF);
				if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, hi, lo)))
					ctx->fillRadialGradient (p, *g, CPoint (c.x - 0.8, c.y - 2.5), 6.0);
			}
		}
	}

	CLASS_METHODS_NOCOPY (LedButton, CView)

private:
	CColor ledColor;
	Style style;
	std::function<bool ()> isOn;
	std::function<void ()> onClick;
};

//------------------------------------------------------------------------
/** Light menu field with a charcoal frame and arrow; the menu is built on click. */
class DropdownView : public CView
{
public:
	DropdownView (const CRect& size, std::function<std::string ()> textFn, std::function<void (DropdownView*)> click)
	: CView (size), text (std::move (textFn)), onClick (std::move (click))
	{
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		if (onClick)
			onClick (this);
		e.consumed = true;
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect r = getViewSize ();
		if (auto p = newPath (ctx))
		{
			p->addRoundRect (r, 2.5);
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0xF8, 0xF8, 0xF7, 0xF4), CColor (0xDE, 0xDE, 0xDC, 0xF4))))
				ctx->fillLinearGradient (p, *g, r.getTopLeft (), r.getBottomLeft ());
			strokePath (ctx, p, CColor (0x3A, 0x3A, 0x3D, 0xFF), 1.0);
		}
		ctx->setFont (fonts ().menu);
		ctx->setFontColor (Pal::ink);
		drawCentered (ctx, text ? text () : std::string (), r.getCenter ().x - 5, r.getCenter ().y + 3.6);
		if (auto p = newPath (ctx))
		{
			const CPoint a (r.right - 13, r.getCenter ().y - 1.8);
			p->beginSubpath (a);
			p->addLine (a.x + 7, a.y);
			p->addLine (a.x + 3.5, a.y + 3.8);
			p->closeSubpath ();
			ctx->setFillColor (Pal::ink);
			ctx->drawGraphicsPath (p, CDrawContext::kPathFilled);
		}
	}

	CLASS_METHODS_NOCOPY (DropdownView, CView)

private:
	std::function<std::string ()> text;
	std::function<void (DropdownView*)> onClick;
};

void popupMenu (CView* anchor, const std::vector<std::pair<std::string, int32_t>>& entries, std::function<void (int)> chosen)
{
	auto* frame = anchor->getFrame ();
	if (!frame)
		return;
	auto menu = makeOwned<COptionMenu> ();
	for (const auto& e : entries)
		menu->addEntry (e.first.c_str (), -1, e.second);
	CPoint where = anchor->getViewSize ().getBottomLeft ();
	anchor->localToFrame (where);
	menu->popup (frame, where, [chosen] (COptionMenu* m) {
		if (chosen && m->getLastResult () >= 0)
			chosen (static_cast<int> (m->getLastResult ()));
	});
}

//------------------------------------------------------------------------
/** Gear icon (about menu) and power button (bypass). */
class IconButton : public CView
{
public:
	enum class Kind
	{
		Gear,
		Power
	};

	IconButton (const CRect& size, Kind k, std::function<bool ()> on, std::function<void (IconButton*)> click)
	: CView (size), kind (k), isOn (std::move (on)), onClick (std::move (click))
	{
	}

	void onMouseDownEvent (MouseDownEvent& e) override
	{
		if (!e.buttonState.isLeft ())
			return;
		if (onClick)
			onClick (this);
		invalid ();
		e.consumed = true;
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect r = getViewSize ();
		const CPoint c = r.getCenter ();
		if (kind == Kind::Gear)
		{
			if (auto p = newPath (ctx))
			{
				for (int i = 0; i < 8; ++i)
				{
					const double a = i * 45.0;
					const CPoint q = polar (c, 6.0, a - 14);
					i == 0 ? p->beginSubpath (q) : p->addLine (q);
					p->addLine (polar (c, 8.5, a - 9));
					p->addLine (polar (c, 8.5, a + 9));
					p->addLine (polar (c, 6.0, a + 14));
				}
				p->closeSubpath ();
				p->addEllipse (circleRect (c, 2.8));
				ctx->setFillColor (Pal::ink);
				ctx->drawGraphicsPath (p, CDrawContext::kPathFilledEvenOdd);
			}
			return;
		}
		const bool bypassed = isOn && isOn ();
		if (auto p = newPath (ctx))
		{
			p->addRoundRect (r, 2.5);
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0xF0, 0xF0, 0xEF, 0xFF), CColor (0xC8, 0xC9, 0xCB, 0xFF))))
				ctx->fillLinearGradient (p, *g, r.getTopLeft (), r.getBottomLeft ());
			strokePath (ctx, p, CColor (0x3A, 0x3A, 0x3D, 0xFF), 1.0);
		}
		const CColor col = bypassed ? Pal::red : Pal::ink; // red while bypassed
		strokeArc (ctx, c, 5.0, -50, 230, col, 1.5);
		drawLine (ctx, CPoint (c.x, c.y - 7), CPoint (c.x, c.y - 1), col, 1.5);
	}

	CLASS_METHODS_NOCOPY (IconButton, CView)

private:
	Kind kind;
	std::function<bool ()> isOn;
	std::function<void (IconButton*)> onClick;
};

//------------------------------------------------------------------------
/** Segmented level meters: output (top) and sidechain master (bottom). */
class MeterView : public CView
{
public:
	MeterView (const CRect& size, const HudState& h) : CView (size), hud (h) { setMouseEnabled (false); }

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect r = getViewSize ();
		drawMeter (ctx, r.top + 4, hud.slaveOutDb);
		drawMeter (ctx, r.top + 16, hud.masterDb);
	}

	CLASS_METHODS_NOCOPY (MeterView, CView)

private:
	void drawMeter (CDrawContext* ctx, CCoord y, float db) const
	{
		const CRect r = getViewSize ();
		const float shown = std::max (db, -60.f);
		char buf[16];
		std::snprintf (buf, sizeof buf, "%d", static_cast<int> (std::lround (shown)));
		ctx->setFont (fonts ().small);
		ctx->setFontColor (Pal::ink);
		ctx->drawString (buf, CPoint (r.left + 15 - ctx->getStringWidth (buf), y + 6.5));
		ctx->drawString ("dB", CPoint (r.right - 13, y + 6.5));

		const int segments = 34;
		const CCoord x0 = r.left + 20, w = r.getWidth () - 38;
		const CCoord seg = w / segments;
		const int lit = static_cast<int> (std::lround ((shown + 60.f) / 60.f * segments));
		for (int i = 0; i < segments; ++i)
		{
			const CRect s (x0 + i * seg, y, x0 + (i + 1) * seg - 0.9, y + 7);
			CColor col (0xC6, 0xC6, 0xC8, 0xFF);
			if (i < lit)
			{
				col = CColor (0x33, 0x33, 0x36, 0xFF);
				if (i >= lit - 3)
					col = mix (CColor (0x8A, 0x10, 0x1C, 0xFF), Pal::red, (i - (lit - 3)) / 3.0);
			}
			ctx->setFillColor (col);
			ctx->drawRect (s, kDrawFilled);
		}
	}

	const HudState& hud;
};

//------------------------------------------------------------------------
/** Small oscilloscope on light glass (slave output in red, master in ink). */
class ScopeView : public CView
{
public:
	ScopeView (const CRect& size, const HudState& h, bool masterWave) : CView (size), hud (h), master (masterWave)
	{
		setMouseEnabled (false);
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		CRect r = getViewSize ();
		r.inset (4, 3);
		const auto& wave = master ? hud.masterWave : hud.slaveWave;
		float peak = 0.05f;
		for (float v : wave)
			peak = std::max (peak, std::abs (v));
		const CColor col = master ? CColor (0x1E, 0x1E, 0x20, 0xFF) : Pal::red;
		if (auto p = newPath (ctx))
		{
			for (int i = 0; i < DSP::kHudWavePoints; ++i)
			{
				const CPoint q (r.left + r.getWidth () * i / (DSP::kHudWavePoints - 1),
				                r.getCenter ().y - wave[static_cast<size_t> (i)] / peak * r.getHeight () * 0.45);
				i == 0 ? p->beginSubpath (q) : p->addLine (q);
			}
			strokePath (ctx, p, alpha (col, 0.18), 3.0);
			strokePath (ctx, p, col, 1.1);
		}
		const CCoord marker = r.left + r.getWidth () * 0.76;
		drawLine (ctx, CPoint (marker, r.top), CPoint (marker, r.bottom), alpha (Pal::ink, 0.55), 0.8);
	}

	CLASS_METHODS_NOCOPY (ScopeView, CView)

private:
	const HudState& hud;
	bool master;
};

//------------------------------------------------------------------------
/**
 * Live part of the astrolabe: ink and red-mist plasma, braided spectrum rings (slave in
 * red, master in black ink), black medallion with the red ACTIVE INFLUENCE readout.
 */
class OrbitView : public CView
{
public:
	explicit OrbitView (const HudState& h) : CView (circleRect (kOrbit, kRingIn + 6)), hud (h) { setMouseEnabled (false); }

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);

		// Bloom buffer: the plasma and the wide, soft passes go to a reused offscreen at half the
		// device resolution (4x fewer pixels to rasterise), composited with bilinear smoothing,
		// which also diffuses the halo. The sharp passes stay at full resolution.
		const CRect area = getViewSize ();
		const double device = getFrame () ? getFrame ()->getScaleFactor () * getFrame ()->getZoom () : 1.0;
		const double bloomScale = device * 0.5;
		if (!bloomBuffer)
			bloomBuffer = COffscreenContext::create (area.getSize (), bloomScale);
		// the soft layers move slowly: refresh them every other frame (sharp lines: every frame)
		const bool refreshBloom = !bloomValid || (++frameCounter % 2) == 0;
		if (bloomBuffer && !refreshBloom)
		{
			ctx->setBitmapInterpolationQuality (BitmapInterpolationQuality::kMedium);
			bloomBuffer->getBitmap ()->draw (ctx, area);
		}
		else if (bloomBuffer)
		{
			bloomValid = true;
			bloomBuffer->beginDraw ();
			bloomBuffer->clearRect (CRect (0, 0, area.getWidth () * bloomScale, area.getHeight () * bloomScale));
			{
				CDrawContext::Transform t (*bloomBuffer, CGraphicsTransform ().translate (-area.left, -area.top).scale (bloomScale, bloomScale));
				bloomBuffer->setDrawMode (kAntiAliasing | kNonIntegralMode);
				drawPlasma (bloomBuffer);
				drawWaves (bloomBuffer, Pass::Bloom);
			}
			bloomBuffer->endDraw ();
			ctx->setBitmapInterpolationQuality (BitmapInterpolationQuality::kMedium);
			bloomBuffer->getBitmap ()->draw (ctx, area);
		}
		else
		{
			drawPlasma (ctx);
			drawWaves (ctx, Pass::Bloom);
		}
		drawWaves (ctx, Pass::Sharp);

		// static medallion and the glowing readout: cached, re-rendered only when the text changes
		constexpr double kMedallionHalf = 86.0;
		if (!medallionCache)
			medallionCache = renderToCache (getFrame (), CPoint (2 * kMedallionHalf, 2 * kMedallionHalf), [this] (CDrawContext* c) {
				CDrawContext::Transform t (*c, CGraphicsTransform ().translate (kMedallionHalf - kOrbit.x, kMedallionHalf - kOrbit.y));
				drawMedallion (c);
			});
		if (medallionCache)
			medallionCache->draw (ctx, circleRect (kOrbit, kMedallionHalf));
		else
			drawMedallion (ctx);

		char buf[16];
		std::snprintf (buf, sizeof buf, "%d%%", static_cast<int> (std::lround (100.0 * hud.influence)));
		const CRect readoutRect (kOrbit.x - 60, kOrbit.y - 32, kOrbit.x + 60, kOrbit.y + 30);
		if (!readoutCache || readoutText != buf)
		{
			readoutText = buf;
			readoutCache = renderToCache (getFrame (), readoutRect.getSize (), [&] (CDrawContext* c) {
				CDrawContext::Transform t (*c, CGraphicsTransform ().translate (-readoutRect.left, -readoutRect.top));
				drawReadout (c, readoutText);
			});
		}
		if (readoutCache)
			readoutCache->draw (ctx, readoutRect);
		else
			drawReadout (ctx, readoutText);
	}

	CLASS_METHODS_NOCOPY (OrbitView, CView)

private:
	enum class Pass
	{
		Bloom, // gaseous fill, wide translucent halo, saturated body
		Sharp  // signal line, dark heart, braided strands
	};

	void drawWaves (CDrawContext* ctx, Pass pass) const
	{
		if (hud.showInput)
			drawWave (ctx, pass, hud.slaveIn, CColor (0xD0, 0x50, 0x5C, 0xFF), CColor (0xD8, 0x60, 0x6A, 0xFF),
			          CColor (0x9A, 0x20, 0x2C, 0xFF), 0.35, 0.0, false, true);
		if (hud.showMaster)
			drawWave (ctx, pass, hud.master, CColor (0x26, 0x26, 0x28, 0xFF), CColor (0x60, 0x60, 0x64, 0xFF),
			          CColor (0x00, 0x00, 0x00, 0xFF), 1.0, 1.7, false, false);
		if (hud.showSlave)
			drawWave (ctx, pass, hud.slaveOut, CColor (0xB4, 0x16, 0x22, 0xFF), CColor (0x7A, 0x14, 0x1C, 0xFF), Pal::redDeep, 0.85, 0.0, true, true);
	}

	/**
	 * Ink plasma between the medallion and the waves: ink smoke and red mist cross-faded
	 * with a slow breathing, their opacity following the signal.
	 */
	void drawPlasma (CDrawContext* ctx) const
	{
		auto& tex = textures ();
		if (!tex.plasmaA || !tex.plasmaB)
			return;
		const CPoint size (2 * kPlasmaImageRadius, 2 * kPlasmaImageRadius);
		const CPoint local (kPlasmaImageRadius, kPlasmaImageRadius);
		if (!plasmaCacheA)
			plasmaCacheA = renderToCache (getFrame (), size, [&] (CDrawContext* c) {
				drawTexture (c, tex.plasmaA, local, kPlasmaImageRadius);
			}, 0.5);
		if (!plasmaCacheB)
			plasmaCacheB = renderToCache (getFrame (), size, [&] (CDrawContext* c) {
				drawTexture (c, tex.plasmaB, local, kPlasmaImageRadius);
			}, 0.5);
		CBitmap* layerA = plasmaCacheA ? plasmaCacheA.get () : tex.plasmaA.get ();
		CBitmap* layerB = plasmaCacheB ? plasmaCacheB.get () : tex.plasmaB.get ();
		double energy = 0.0;
		int count = 0;
		for (int b = 0; b < DSP::kHudBands; ++b)
		{
			if (hud.showSlave)
				energy += hud.slaveOut[static_cast<size_t> (b)], ++count;
			if (hud.showMaster)
				energy += hud.master[static_cast<size_t> (b)], ++count;
		}
		energy = count > 0 ? energy / count : 0.0;
		const double strength = std::clamp (0.12 + 1.0 * energy, 0.0, 0.62);
		const double breath = std::sin (hud.time * 0.8);
		const CRect area = circleRect (kOrbit, kPlasmaImageRadius);
		layerA->draw (ctx, area, CPoint (0, 0), static_cast<float> (strength * (0.85 + 0.15 * breath)));
		layerB->draw (ctx, area, CPoint (0, 0), static_cast<float> (strength * (0.28 - 0.12 * breath)));
	}

	/**
	 * One spectrum as a braided ring of ink:
	 *  - gaseous fill between the medallion and the ring (radial gradient),
	 *  - bloom: wide translucent pass (alpha ~0.25), saturated body,
	 *  - sharp signal line, then a thin dark heart that keeps it crisp on the light disc.
	 */
	void drawWave (CDrawContext* ctx, Pass pass, const std::array<float, DSP::kHudBands>& levels, const CColor& core,
	               const CColor& halo, const CColor& heart, double opacity, double phaseOffset, bool twist,
	               bool leftSide) const
	{
		// Mirrored spectrum: bass at the bottom, treble at the top, climbing both sides,
		// so the ring closes without a seam.
		constexpr int bands = DSP::kHudBands;
		constexpr int n = 2 * bands; // 192 vertices, Catmull-Rom smoothed
		std::array<double, n> radius {}, angle {};
		auto bandAt = [] (int j) {
			j = (j % n + n) % n;
			return j < bands ? j : n - 1 - j;
		};
		auto radiusAt = [&] (int j) {
			const double level = levels[static_cast<size_t> (bandAt (j))];
			return kWaveMin + kWaveSpan * std::clamp (level, 0.0, 1.0);
		};
		for (int j = 0; j < n; ++j)
		{
			const double r1 = radiusAt (j);
			double a = 90.0 + 360.0 * (j + 0.5) / n;
			if (twist) // phase error twists the slave trace off its band
				a += 2.0 * std::sin (hud.phase[static_cast<size_t> (bandAt (j))]);
			radius[static_cast<size_t> (j)] = std::max (kWaveMin - 8.0, r1);
			angle[static_cast<size_t> (j)] = a;
		}
		struct Strand
		{
			double amp, freq, speed, width, alphaCore;
		};
		const Strand strands[] = {{0.0, 0.0, 0.0, 2.0, 1.0}, {5.0, 7.0, 1.3, 0.9, 0.7}, {9.0, 11.0, -0.9, 0.8, 0.55}};
		// the soft bloom passes use every other vertex: half the tessellation, no visible loss
		const int stride = pass == Pass::Bloom ? 2 : 1;
		constexpr int segments = 6;
		std::array<CPoint, n> pts {};
		int index = 0;
		for (const auto& st : strands)
		{
			for (int i = 0; i < n; ++i)
			{
				const double a = angle[static_cast<size_t> (i)];
				const double r = radius[static_cast<size_t> (i)] +
				                 st.amp * std::sin (deg (a) * st.freq + hud.time * st.speed + phaseOffset + index);
				pts[static_cast<size_t> (i)] = polar (kOrbit, r, a);
			}
			if (index == 0 && pass == Pass::Bloom)
			{
				// gaseous fill of the ring interior, around the medallion (even-odd)
				if (auto path = newPath (ctx))
				{
					for (int i = 0; i < n; i += 2)
						i == 0 ? path->beginSubpath (pts[0]) : path->addLine (pts[static_cast<size_t> (i)]);
					path->closeSubpath ();
					path->addEllipse (circleRect (kOrbit, 72));
					GradientColorStopMap stops;
					stops.emplace (0.0, alpha (halo, 0.0));
					stops.emplace (0.45, alpha (halo, 0.03 * opacity));
					stops.emplace (1.0, alpha (halo, 0.12 * opacity));
					if (auto g = VSTGUI::owned (CGradient::create (stops)))
						ctx->fillRadialGradient (path, *g, kOrbit, 180.0, CPoint (0, 0), true);
				}
			}
			if (index > 0)
			{
				// secondary strands: one path each, at the mean emphasis
				auto path = newPath (ctx);
				if (!path)
					return;
				for (int i = 0; i < n; i += stride)
					i == 0 ? path->beginSubpath (pts[0]) : path->addLine (pts[static_cast<size_t> (i)]);
				path->closeSubpath ();
				const double o = opacity * 0.62;
				if (pass == Pass::Bloom)
					strokeWave (ctx, path, alpha (halo, 0.12 * o), 3.5);
				else
					strokeWave (ctx, path, alpha (core, st.alphaCore * o), st.width);
				++index;
				continue;
			}
			// the slave is strongest on the left half, the master on the right (as on the artwork)
			for (int seg = 0; seg < segments; ++seg)
			{
				const int first = seg * n / segments;
				const int last = (seg + 1) * n / segments;
				const double mid = angle[static_cast<size_t> ((first + last) / 2 % n)];
				const double side = 0.5 - 0.5 * std::cos (deg (mid)); // 1 on the left, 0 on the right
				const double emphasis = 0.35 + 0.65 * (leftSide ? side : 1.0 - side);
				auto path = newPath (ctx);
				if (!path)
					return;
				for (int i = first; i <= last; i += stride)
				{
					const CPoint& q = pts[static_cast<size_t> (i % n)];
					i == first ? path->beginSubpath (q) : path->addLine (q);
				}
				path->addLine (pts[static_cast<size_t> (last % n)]);
				const double o = opacity * emphasis;
				if (pass == Pass::Bloom)
				{
					strokeWave (ctx, path, alpha (halo, 0.25 * o), 16.0); // diffuse mist
					strokeWave (ctx, path, alpha (core, 0.35 * o), 6.5);  // saturated body
				}
				else
				{
					strokeWave (ctx, path, alpha (core, o), st.width);     // signal line
					strokeWave (ctx, path, alpha (heart, 0.9 * o), 0.8);  // dark heart
				}
			}
			++index;
		}
	}

	static void drawReadout (CDrawContext* ctx, const std::string& text)
	{
		ctx->setFont (fonts ().readout);
		drawGlowText (ctx, text, kOrbit.x, kOrbit.y + 9, CColor (0xFF, 0x3A, 0x3A, 0xFF), CColor (0xFF, 0x10, 0x18, 0xFF));
		ctx->setFont (fonts ().caption);
		ctx->setFontColor (CColor (0xE2, 0xE2, 0xE4, 0xFF));
		drawCentered (ctx, "ACTIVE INFLUENCE", kOrbit.x, kOrbit.y + 24);
	}

	void drawMedallion (CDrawContext* ctx) const
	{
		const CPoint c = kOrbit;
		fillCircle (ctx, CPoint (c.x + 2, c.y + 3), 73, CColor (0, 0, 0, 0x45));
		if (auto p = newPath (ctx))
		{
			p->addEllipse (circleRect (c, 58));
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0x2C, 0x1C, 0x1E, 0xFF), CColor (0x06, 0x06, 0x07, 0xFF))))
				ctx->fillRadialGradient (p, *g, CPoint (c.x, c.y - 6), 62);
		}
		std::mt19937 rng (11u);
		std::uniform_real_distribution<double> u (0.0, 1.0);
		for (int i = 0; i < 40; ++i)
		{
			const double a = u (rng) * 360.0, r = std::sqrt (u (rng)) * 52.0;
			const double size = 0.4 + 0.5 * u (rng);
			fillCircle (ctx, polar (c, r, a), size, alpha (CColor (0xFF, 0x50, 0x50, 0xFF), 0.25 + 0.5 * u (rng)));
		}
		metalAnnulus (ctx, c, 55, 59);
		strokeCircle (ctx, c, 63, alpha (Pal::ink, 0.8), 0.9);
		metalAnnulus (ctx, c, 66, 72);
		for (int i = 0; i < 48; ++i)
			drawLine (ctx, polar (c, 67, i * 7.5), polar (c, 69, i * 7.5), CColor (0x08, 0x08, 0x09, 0x90), 0.6);
		for (int i = 0; i < 8; ++i)
		{
			const double a = -90.0 + 45.0 * i;
			if (i == 0)
			{
				const CPoint t = polar (c, 69, a);
				if (auto p = newPath (ctx))
				{
					p->beginSubpath (t.x, t.y - 8);
					p->addLine (t.x + 5, t.y);
					p->addLine (t.x, t.y + 6);
					p->addLine (t.x - 5, t.y);
					p->closeSubpath ();
					fillMetal (ctx, p, CRect (t.x - 5, t.y - 8, t.x + 5, t.y + 6));
				}
				continue;
			}
			metalSphere (ctx, polar (c, 69, a), i % 2 ? 3.6 : 4.6);
		}
	}

	const HudState& hud;
	// device-resolution caches (GUI thread only)
	mutable SharedPointer<CBitmap> plasmaCacheA, plasmaCacheB;
	SharedPointer<COffscreenContext> bloomBuffer;
	bool bloomValid {false};
	uint32_t frameCounter {0};
	SharedPointer<CBitmap> medallionCache, readoutCache;
	std::string readoutText;
};

} // namespace

//------------------------------------------------------------------------
SymbiosisEditor::SymbiosisEditor (SymbiosisController* controller)
: VSTGUIEditor (controller), symbiosisController (controller)
{
	ViewRect viewRect (0, 0, kWidth, kHeight);
	setRect (viewRect);
}

//------------------------------------------------------------------------
double SymbiosisEditor::getParam (Vst::ParamID tag) const
{
	return symbiosisController->getParamNormalized (tag);
}

//------------------------------------------------------------------------
void SymbiosisEditor::setParamFromGui (Vst::ParamID tag, double normalized)
{
	symbiosisController->beginEdit (tag);
	symbiosisController->editFromGui (tag, normalized);
	symbiosisController->endEdit (tag);
	auto it = controls.find (tag);
	if (it != controls.end ())
	{
		it->second->setValueNormalized (static_cast<float> (getParam (tag)));
		it->second->invalid ();
	}
	refreshParamViews ();
}

//------------------------------------------------------------------------
void SymbiosisEditor::applyPreset (const FactoryPreset& preset)
{
	for (Vst::ParamID id = 0; id < kNumParams; ++id)
		if (isPresetParam (id))
			setPlainFromGui (id, preset.plain[id]);
}

//------------------------------------------------------------------------
std::string SymbiosisEditor::currentPresetName () const
{
	for (const auto& preset : kFactoryPresets)
	{
		bool same = true;
		for (Vst::ParamID id = 0; id < kNumParams && same; ++id)
			if (isPresetParam (id))
				same = std::abs (toNormalized (id, preset.plain[id]) - getParam (id)) < 1e-3;
		if (same)
			return preset.name;
	}
	return "CUSTOM";
}

//------------------------------------------------------------------------
void SymbiosisEditor::refreshParamViews ()
{
	for (auto* v : paramViews)
		v->invalid ();
}

//------------------------------------------------------------------------
bool PLUGIN_API SymbiosisEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame)
		return false;

	acquireFonts ();
	frame = new CFrame (CRect (0, 0, kCanvasWidth, kCanvasHeight), this);
	frame->setBackgroundColor (Pal::paper);
	frame->addView (new BackgroundView (CRect (0, 0, kCanvasWidth, kCanvasHeight)));

	auto* orbit = new OrbitView (hud);
	frame->addView (orbit);
	hudViews.push_back (orbit);

	// knobs and sliders: CControls tagged with their parameter
	auto addControl = [this] (CControl* c, Vst::ParamID id) {
		c->setDefaultValue (static_cast<float> (defaultNormalized (id)));
		c->setValueNormalized (static_cast<float> (getParam (id)));
		frame->addView (c);
		controls[id] = c;
	};
	addControl (new GemKnob (kPullCentre, this, kParamPullId, GemKnob::Style::Crystal), kParamPullId);
	addControl (new GemKnob (kHarmonicsCentre, this, kParamHarmonicsId, GemKnob::Style::Cap), kParamHarmonicsId);
	addControl (new RubySlider (67, this, kParamInertiaId, Pal::ledRed), kParamInertiaId);
	addControl (new RubySlider (137, this, kParamReactionId, Pal::ledWhite), kParamReactionId);
	addControl (new RubySlider (730, this, kParamTrackingId, Pal::ledWhite), kParamTrackingId);
	addControl (new RubySlider (799, this, kParamTransShieldId, Pal::ledRed), kParamTransShieldId);

	// LED buttons (12, as on the artwork)
	using Style = LedButton::Style;
	auto addButton = [this] (CCoord x, int row, const CColor& led, Style style, std::function<bool ()> on,
	                         std::function<void ()> click) {
		auto* b = new LedButton (CPoint (x, kRowY[row]), led, style, std::move (on), std::move (click));
		frame->addView (b);
		paramViews.push_back (b);
	};
	auto toggleParam = [this, &addButton] (CCoord x, int row, const CColor& led, Style style, Vst::ParamID id) {
		addButton (x, row, led, style, [this, id] { return getParam (id) > 0.5; },
		           [this, id] { setParamFromGui (id, getParam (id) > 0.5 ? 0.0 : 1.0); });
	};
	auto choiceParam = [this, &addButton] (CCoord x, int row, const CColor& led, Style style, Vst::ParamID id, double plain) {
		addButton (x, row, led, style, [this, id, plain] { return std::abs (getPlain (id) - plain) < 0.5; },
		           [this, id, plain] { setPlainFromGui (id, plain); });
	};
	auto viewOption = [this, &addButton] (CCoord x, int row, const CColor& led, Style style, bool HudState::*flag) {
		addButton (x, row, led, style, [this, flag] { return hud.*flag; }, [this, flag] {
			hud.*flag = !(hud.*flag);
			hud.idleTicks = std::min (hud.idleTicks, 100);
			for (auto* v : hudViews)
				v->invalid ();
		});
	};
	toggleParam (kLeftCols[0], 0, Pal::ledRed, Style::Bar, kParamPhaseLinkId);
	toggleParam (kLeftCols[1], 0, Pal::ledRed, Style::Bar, kParamDeltaSoloId);
	toggleParam (kLeftCols[2], 0, Pal::ledWhite, Style::Bar, kParamBypassId);
	choiceParam (kLeftCols[0], 1, Pal::ledRed, Style::Bar, kParamTargetModeId, 0.0);
	choiceParam (kLeftCols[1], 1, Pal::ledWhite, Style::Bar, kParamTargetModeId, 1.0);
	viewOption (kLeftCols[2], 1, Pal::ledWhite, Style::Bar, &HudState::freeze);
	choiceParam (kRightCols[0], 0, Pal::ledRed, Style::Jewel, kParamOversamplingId, 0.0);
	choiceParam (kRightCols[1], 0, Pal::ledRed, Style::Bar, kParamOversamplingId, 1.0);
	choiceParam (kRightCols[2], 0, Pal::ledWhite, Style::Bar, kParamOversamplingId, 2.0);
	viewOption (kRightCols[0], 1, Pal::ledWhite, Style::Jewel, &HudState::showMaster);
	viewOption (kRightCols[1], 1, Pal::ledWhite, Style::Bar, &HudState::showSlave);
	viewOption (kRightCols[2], 1, Pal::ledRed, Style::Bar, &HudState::showInput);

	// menus
	auto* sidechain = new DropdownView (
	    CRect (22, 58, 168, 75),
	    [this] { return std::string (hud.masterPresent ? "[SIDECHAIN: MASTER]" : "[SIDECHAIN: NONE]"); },
	    [] (DropdownView* v) {
		    popupMenu (v,
		               {{"Route the master track (kick, lead vocal)", CMenuItem::kDisabled},
		                {"to the sidechain input of Symbiosis.", CMenuItem::kDisabled}},
		               nullptr);
	    });
	frame->addView (sidechain);
	hudViews.push_back (sidechain);

	auto* routage = new DropdownView (
	    CRect (22, 82, 168, 99),
	    [this] { return std::string (getPlain (kParamTargetModeId) > 0.5 ? "ROUTAGE : HARMONIC" : "ROUTAGE : EXACT"); },
	    [this] (DropdownView* v) {
		    const bool harmonic = getPlain (kParamTargetModeId) > 0.5;
		    popupMenu (v,
		               {{"Exact Peaks", harmonic ? 0 : CMenuItem::kChecked},
		                {"Harmonic Series", harmonic ? CMenuItem::kChecked : 0}},
		               [this] (int i) { setPlainFromGui (kParamTargetModeId, i); });
	    });
	frame->addView (routage);
	paramViews.push_back (routage);

	auto* system = new DropdownView (
	    CRect (693, 58, 838, 75),
	    [this] {
		    static const char* names[] = {"SYSTEM : OS 1X", "SYSTEM : OS 2X", "SYSTEM : OS 4X"};
		    return std::string (names[std::clamp (static_cast<int> (getPlain (kParamOversamplingId)), 0, 2)]);
	    },
	    [this] (DropdownView* v) {
		    const int os = static_cast<int> (getPlain (kParamOversamplingId));
		    popupMenu (v,
		               {{"Oversampling Off", os == 0 ? CMenuItem::kChecked : 0},
		                {"Oversampling 2x", os == 1 ? CMenuItem::kChecked : 0},
		                {"Oversampling 4x", os == 2 ? CMenuItem::kChecked : 0}},
		               [this] (int i) { setPlainFromGui (kParamOversamplingId, i); });
	    });
	frame->addView (system);
	paramViews.push_back (system);

	auto* systeme = new DropdownView (
	    CRect (693, 82, 838, 99), [this] { return "SYSTEME : " + currentPresetName (); },
	    [this] (DropdownView* v) {
		    const std::string current = currentPresetName ();
		    std::vector<std::pair<std::string, int32_t>> entries;
		    for (const auto& p : kFactoryPresets)
			    entries.emplace_back (p.name, current == p.name ? CMenuItem::kChecked : 0);
		    popupMenu (v, entries, [this] (int i) {
			    if (i >= 0 && i < static_cast<int> (std::size (kFactoryPresets)))
				    applyPreset (kFactoryPresets[i]);
		    });
	    });
	frame->addView (systeme);
	paramViews.push_back (systeme);

	// header: meters, gear (about), power (bypass)
	auto* meters = new MeterView (CRect (650, 10, 795, 36), hud);
	frame->addView (meters);
	hudViews.push_back (meters);
	frame->addView (new IconButton (CRect (797, 13, 817, 33), IconButton::Kind::Gear, nullptr, [] (IconButton* v) {
		popupMenu (v,
		           {{"Symbiosis " VERSION_STR, CMenuItem::kDisabled},
		            {"Latency 2048 samples", CMenuItem::kDisabled},
		            {stringCompanyWeb, CMenuItem::kDisabled}},
		           nullptr);
	}));
	auto* power = new IconButton (
	    CRect (824, 14, 843, 33), IconButton::Kind::Power, [this] { return getParam (kParamBypassId) > 0.5; },
	    [this] (IconButton*) { setParamFromGui (kParamBypassId, getParam (kParamBypassId) > 0.5 ? 0.0 : 1.0); });
	frame->addView (power);
	paramViews.push_back (power);

	// scopes
	auto* scopeTop = new ScopeView (kScopeTop, hud, false);
	auto* scopeBottom = new ScopeView (kScopeBottom, hud, true);
	frame->addView (scopeTop);
	frame->addView (scopeBottom);
	hudViews.push_back (scopeTop);
	hudViews.push_back (scopeBottom);

	frame->setZoom (kZoom);

#if SMTG_OS_LINUX
	// VSTGUI's X11 backend connects to the display only through a FrameConfig run loop
	// (VST3Editor does the same). The run loop is the host's IRunLoop.
	X11::FrameConfig x11Config;
	if (auto linuxFactory = getPlatformFactory ().asLinuxFactory ())
	{
		if (!linuxFactory->getRunLoop () && plugFrame)
			Steinberg::Linux::setupVSTGUIRunloop (plugFrame);
		x11Config.runLoop = linuxFactory->getRunLoop ();
	}
	frame->open (parent, platformType, x11Config.runLoop ? &x11Config : nullptr);
#else
	frame->open (parent, platformType);
#endif

	timer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { tick (); }, 16, true); // ~60 FPS
	symbiosisController->editorOpened (this);
	return true;
}

//------------------------------------------------------------------------
void PLUGIN_API SymbiosisEditor::close ()
{
	symbiosisController->editorClosed (this);
	timer = nullptr;
	controls.clear ();
	paramViews.clear ();
	hudViews.clear ();
	if (frame)
	{
		frame->forget ();
		frame = nullptr;
		releaseFonts ();
	}
}

//------------------------------------------------------------------------
void SymbiosisEditor::tick ()
{
	DSP::HudFrame frameData;
	const bool fresh = symbiosisController->popLatestHudFrame (frameData);
	if (fresh)
	{
		hud.idleTicks = 0;
		hud.receiving = true;
	}
	else if (++hud.idleTicks > 45)
		hud.receiving = false;
	if (!fresh && hud.idleTicks > 160)
		return; // nothing moves any more: stop repainting until data returns

	hud.time += 0.016;
	auto follow = [] (float& v, float target, float k) { v += (target - v) * k; };
	const bool update = fresh && !hud.freeze;
	const bool decay = !hud.receiving && !hud.freeze;
	for (int b = 0; b < DSP::kHudBands; ++b)
	{
		const auto i = static_cast<size_t> (b);
		if (update)
		{
			follow (hud.master[i], frameData.master[b], 0.45f);
			follow (hud.slaveIn[i], frameData.slaveIn[b], 0.45f);
			follow (hud.slaveOut[i], frameData.slaveOut[b], 0.45f);
			const double cs = 0.7 * std::cos (hud.phase[i]) + 0.3 * std::cos (frameData.phaseDelta[b]);
			const double sn = 0.7 * std::sin (hud.phase[i]) + 0.3 * std::sin (frameData.phaseDelta[b]);
			hud.phase[i] = static_cast<float> (std::atan2 (sn, cs));
		}
		else if (decay)
		{
			hud.master[i] *= 0.92f;
			hud.slaveIn[i] *= 0.92f;
			hud.slaveOut[i] *= 0.92f;
		}
	}
	if (update)
	{
		for (int i = 0; i < DSP::kHudWavePoints; ++i)
		{
			hud.slaveWave[static_cast<size_t> (i)] = frameData.slaveWave[i];
			hud.masterWave[static_cast<size_t> (i)] = frameData.masterWave[i];
		}
		follow (hud.influence, frameData.phaseTransfer, 0.12f);
		follow (hud.slaveOutDb, frameData.slaveOutDb, 0.35f);
		follow (hud.masterDb, frameData.masterDb, 0.35f);
		hud.masterPresent = frameData.masterDb > -80.f;
	}
	else if (decay)
	{
		hud.influence *= 0.95f;
		hud.slaveOutDb = std::max (-120.f, hud.slaveOutDb - 1.5f);
		hud.masterDb = std::max (-120.f, hud.masterDb - 1.5f);
		hud.masterPresent = false;
		for (auto& w : hud.slaveWave)
			w *= 0.9f;
		for (auto& w : hud.masterWave)
			w *= 0.9f;
	}
	for (auto* v : hudViews)
		v->invalid ();
}

//------------------------------------------------------------------------
void SymbiosisEditor::valueChanged (CControl* control)
{
	const auto tag = static_cast<Vst::ParamID> (control->getTag ());
	if (tag >= kNumParams)
		return;
	symbiosisController->editFromGui (tag, control->getValueNormalized ());
	refreshParamViews ();
}

//------------------------------------------------------------------------
void SymbiosisEditor::parameterChanged (Vst::ParamID tag, double normalized)
{
	auto it = controls.find (tag);
	if (it != controls.end () && std::abs (it->second->getValueNormalized () - normalized) > 1e-6)
	{
		it->second->setValueNormalized (static_cast<float> (normalized));
		it->second->invalid ();
	}
	refreshParamViews ();
}

} // namespace Symbiosis
