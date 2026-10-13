//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_editor.cpp
//------------------------------------------------------------------------
#include "symbiosis_editor.h"

#include "dsp/spectral_engine.h"
#include "symbiosis_controller.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"

#if SMTG_OS_LINUX
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/linux/x11frame.h"
#include "vstgui/lib/platform/platformfactory.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace VSTGUI;
using namespace Steinberg;

namespace Symbiosis {

namespace {

constexpr double kPi = 3.14159265358979323846;

//------------------------------------------------------------------------
// Art direction: oneiric x abrasion
namespace Palette {
const CColor chassis (0x0D, 0x08, 0x09, 0xFF);
const CColor bordeaux (0x4A, 0x03, 0x07, 0xFF);
const CColor ember (0xE2, 0x34, 0x44, 0xFF);
const CColor cyan (0x17, 0xD4, 0xC6, 0xFF);
const CColor text (0xFF, 0xEC, 0xEE, 0xFF);
const CColor blood (0xFF, 0x1A, 0x2B, 0xFF);
const CColor marker (0xFF, 0x6A, 0x3D, 0xFF);
} // namespace Palette

CColor alpha (CColor c, double a)
{
	c.alpha = static_cast<uint8_t> (std::clamp (a, 0.0, 1.0) * 255.0 + 0.5);
	return c;
}

CColor mix (const CColor& a, const CColor& b, double t)
{
	t = std::clamp (t, 0.0, 1.0);
	auto lerp = [t] (uint8_t x, uint8_t y) { return static_cast<uint8_t> (x + (y - x) * t + 0.5); };
	return CColor (lerp (a.red, b.red), lerp (a.green, b.green), lerp (a.blue, b.blue), lerp (a.alpha, b.alpha));
}

CPoint polar (const CPoint& c, double radius, double angleRad)
{
	return CPoint (c.x + radius * std::cos (angleRad), c.y + radius * std::sin (angleRad));
}

double deg (double d) { return d * kPi / 180.0; }

//------------------------------------------------------------------------
/** Fonts shipped in Resources/Fonts (SIL OFL), with the system fallback if missing. */
struct UiFonts
{
	SharedPointer<CFontDesc> title = makeOwned<CFontDesc> ("Unbounded", 25, kBoldFace);
	SharedPointer<CFontDesc> label = makeOwned<CFontDesc> ("Unbounded", 9, kBoldFace);
	SharedPointer<CFontDesc> caption = makeOwned<CFontDesc> ("Unbounded", 8);
	SharedPointer<CFontDesc> mono = makeOwned<CFontDesc> ("JetBrains Mono", 12);
	SharedPointer<CFontDesc> monoSmall = makeOwned<CFontDesc> ("JetBrains Mono", 9);
	SharedPointer<CFontDesc> readout = makeOwned<CFontDesc> ("JetBrains Mono", 46, kBoldFace);
};

// Shared by every open editor, released with the last one (never outlives VSTGUI).
UiFonts* fontsInstance = nullptr;
int fontsUsers = 0;

void acquireFonts ()
{
	if (fontsUsers++ == 0)
		fontsInstance = new UiFonts;
}

void releaseFonts ()
{
	if (--fontsUsers == 0)
	{
		delete fontsInstance;
		fontsInstance = nullptr;
	}
}

UiFonts& fonts () { return *fontsInstance; }

/** UTF-8 text with letter spacing (the 0.16em tracking of the titles). Baseline origin. */
CCoord trackedWidth (CDrawContext* ctx, const std::string& s, CCoord tracking)
{
	CCoord w = 0;
	size_t i = 0;
	int glyphs = 0;
	while (i < s.size ())
	{
		const auto lead = static_cast<unsigned char> (s[i]);
		const size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
		w += ctx->getStringWidth (s.substr (i, len).c_str ());
		i += len;
		++glyphs;
	}
	return w + tracking * std::max (0, glyphs - 1);
}

void drawTracked (CDrawContext* ctx, const std::string& s, CPoint origin, CCoord tracking)
{
	size_t i = 0;
	while (i < s.size ())
	{
		const auto lead = static_cast<unsigned char> (s[i]);
		const size_t len = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : 4;
		const std::string glyph = s.substr (i, len);
		ctx->drawString (glyph.c_str (), origin);
		origin.x += ctx->getStringWidth (glyph.c_str ()) + tracking;
		i += len;
	}
}

void drawTrackedCentered (CDrawContext* ctx, const std::string& s, CCoord cx, CCoord baseline, CCoord tracking)
{
	drawTracked (ctx, s, CPoint (cx - trackedWidth (ctx, s, tracking) * 0.5, baseline), tracking);
}

void drawCentered (CDrawContext* ctx, const std::string& s, CCoord cx, CCoord baseline)
{
	ctx->drawString (s.c_str (), CPoint (cx - ctx->getStringWidth (s.c_str ()) * 0.5, baseline));
}

/** Arc as a polyline (angles in degrees, screen coordinates: 0 = right, clockwise). */
void addArc (CGraphicsPath* path, const CPoint& c, double radius, double fromDeg, double toDeg)
{
	const int steps = std::max (2, static_cast<int> (std::abs (toDeg - fromDeg) / 3.0));
	for (int i = 0; i <= steps; ++i)
	{
		const CPoint p = polar (c, radius, deg (fromDeg + (toDeg - fromDeg) * i / steps));
		if (i == 0)
			path->beginSubpath (p);
		else
			path->addLine (p);
	}
}

void strokePath (CDrawContext* ctx, CGraphicsPath* path, const CColor& color, CCoord width)
{
	ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
	ctx->setLineWidth (width);
	ctx->setFrameColor (color);
	ctx->drawGraphicsPath (path, CDrawContext::kPathStroked);
}

void strokeArc (CDrawContext* ctx, const CPoint& c, double radius, double fromDeg, double toDeg, const CColor& color,
                CCoord width)
{
	if (auto path = VSTGUI::owned (ctx->createGraphicsPath ()))
	{
		addArc (path, c, radius, fromDeg, toDeg);
		strokePath (ctx, path, color, width);
	}
}

void fillCircle (CDrawContext* ctx, const CPoint& c, double radius, const CColor& color)
{
	ctx->setFillColor (color);
	ctx->drawEllipse (CRect (c.x - radius, c.y - radius, c.x + radius, c.y + radius), kDrawFilled);
}

void drawLine (CDrawContext* ctx, const CPoint& a, const CPoint& b, const CColor& color, CCoord width)
{
	ctx->setLineStyle (CLineStyle (CLineStyle::kLineCapRound));
	ctx->setLineWidth (width);
	ctx->setFrameColor (color);
	ctx->drawLine (a, b);
}

/** Text with an incandescent halo (soft glow made of offset copies). */
void drawGlowText (CDrawContext* ctx, const std::string& s, CCoord cx, CCoord baseline, const CColor& color)
{
	static const CPoint offsets[] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}, {-1.4, -1.4}, {1.4, 1.4}, {-1.4, 1.4}, {1.4, -1.4}};
	const CCoord w = ctx->getStringWidth (s.c_str ());
	ctx->setFontColor (alpha (color, 0.16));
	for (const auto& o : offsets)
		ctx->drawString (s.c_str (), CPoint (cx - w * 0.5 + o.x * 2.0, baseline + o.y * 2.0));
	ctx->setFontColor (alpha (color, 0.32));
	for (const auto& o : offsets)
		ctx->drawString (s.c_str (), CPoint (cx - w * 0.5 + o.x, baseline + o.y));
	ctx->setFontColor (color);
	ctx->drawString (s.c_str (), CPoint (cx - w * 0.5, baseline));
}

//------------------------------------------------------------------------
/** Laser-abraded grain, one tile, generated once. Pixels are written premultiplied. */
SharedPointer<CBitmap> makeGrainTile (int size)
{
	auto bitmap = makeOwned<CBitmap> (CPoint (size, size));
	auto access = VSTGUI::owned (CBitmapPixelAccess::create (bitmap, true));
	if (!access)
		return nullptr;
	auto premultiplied = [] (uint8_t r, uint8_t g, uint8_t b, double a) {
		const double k = std::clamp (a, 0.0, 1.0);
		return CColor (static_cast<uint8_t> (r * k + 0.5), static_cast<uint8_t> (g * k + 0.5),
		               static_cast<uint8_t> (b * k + 0.5), static_cast<uint8_t> (255 * k + 0.5));
	};
	std::mt19937 rng (0x5359u);
	std::uniform_real_distribution<double> u (0.0, 1.0);
	for (int y = 0; y < size; ++y)
	{
		const bool laserPass = (y % 3) == 0;
		for (int x = 0; x < size; ++x)
		{
			access->setPosition (static_cast<uint32_t> (x), static_cast<uint32_t> (y));
			const double n = u (rng);
			if (n < 0.45)
				access->setColor (premultiplied (0, 0, 0, 0.06 + 0.12 * u (rng)));
			else if (n > 0.94)
				access->setColor (premultiplied (255, 236, 238, 0.025 + 0.035 * u (rng)));
			else
				access->setColor (premultiplied (255, 236, 238, laserPass ? 0.012 : 0.0));
		}
	}
	return bitmap;
}

//------------------------------------------------------------------------
class ChassisView : public CView
{
public:
	explicit ChassisView (const CRect& size) : CView (size) { setMouseEnabled (false); }

	void draw (CDrawContext* ctx) override
	{
		const CRect r = getViewSize ();
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);

		ctx->setFillColor (Palette::chassis);
		ctx->drawRect (r, kDrawFilled);

		// bordeaux glow from the top edge
		if (auto path = VSTGUI::owned (ctx->createGraphicsPath ()))
		{
			path->addRect (r);
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, alpha (Palette::bordeaux, 0.95), alpha (Palette::bordeaux, 0.0))))
			{
				g->addColorStop (0.18, alpha (Palette::bordeaux, 0.55));
				g->addColorStop (0.42, alpha (Palette::bordeaux, 0.10));
				ctx->fillLinearGradient (path, *g, r.getTopLeft (), CPoint (r.left, r.top + r.getHeight () * 0.62));
			}
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, alpha (Palette::ember, 0.20), alpha (Palette::ember, 0.0))))
				ctx->fillRadialGradient (path, *g, CPoint (r.getCenter ().x, r.top - 40), r.getWidth () * 0.42);
		}

		// grain
		if (!grain)
			grain = makeGrainTile (256);
		if (grain)
			for (CCoord y = r.top; y < r.bottom; y += 256)
				for (CCoord x = r.left; x < r.right; x += 256)
					grain->draw (ctx, CRect (x, y, std::min (x + 256, r.right), std::min (y + 256, r.bottom)));

		// abrasion streaks: long faint diagonal scratches
		std::mt19937 rng (0xAB5u);
		std::uniform_real_distribution<double> u (0.0, 1.0);
		for (int i = 0; i < 150; ++i)
		{
			const CPoint a (r.left + u (rng) * r.getWidth (), r.top + u (rng) * r.getHeight ());
			const double len = 18.0 + 150.0 * u (rng) * u (rng);
			const double ang = deg (-17.0 + 4.0 * (u (rng) - 0.5));
			drawLine (ctx, a, polar (a, len, ang), alpha (Palette::text, 0.012 + 0.035 * u (rng)), 0.6);
		}

		// machined separators: an engraved dark groove and its lit edge
		auto groove = [&] (CCoord y) {
			drawLine (ctx, CPoint (r.left + 24, y), CPoint (r.right - 24, y), CColor (0, 0, 0, 200), 1.0);
			drawLine (ctx, CPoint (r.left + 24, y + 1), CPoint (r.right - 24, y + 1), alpha (Palette::ember, 0.16), 1.0);
		};
		groove (r.top + 70);
		groove (r.bottom - 104);

		// title and engraving
		auto& f = fonts ();
		ctx->setFont (f.title);
		ctx->setFontColor (Palette::text);
		drawTracked (ctx, "SYMBIOSIS", CPoint (r.left + 34, r.top + 45), 25 * 0.16);
		ctx->setFont (f.monoSmall);
		ctx->setFontColor (alpha (Palette::text, 0.42));
		ctx->drawString ("ENERGY-CONSERVING SPECTRAL TRANSFER  /  VECTOR PHASE LINK", CPoint (r.left + 36, r.top + 61));
		ctx->setFont (f.caption);
		ctx->setFontColor (alpha (Palette::ember, 0.55));
		const std::string sides = "SLAVE \xE2\x86\x92 MAIN IN    MASTER \xE2\x86\x92 SIDECHAIN";
		drawTracked (ctx, sides, CPoint (r.left + 36, r.bottom - 20), 8 * 0.16);

		// outer machining line
		ctx->setLineStyle (kLineSolid);
		ctx->setLineWidth (1.0);
		ctx->setFrameColor (CColor (226, 52, 68, static_cast<uint8_t> (0.45 * 255)));
		CRect border (r);
		border.inset (0.5, 0.5);
		ctx->drawRect (border, kDrawStroked);
	}

private:
	SharedPointer<CBitmap> grain;
};

} // namespace

//------------------------------------------------------------------------
/** Brushed black anodised aluminium knob with an incandescent ember marker. */
class MetalKnob : public CKnobBase
{
public:
	MetalKnob (const CRect& size, IControlListener* listener, int32_t tag, std::string labelText)
	: CKnobBase (size, listener, tag, nullptr), label (std::move (labelText))
	{
	}

	void setValueText (std::string t)
	{
		valueText = std::move (t);
		invalid ();
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
		const CRect r = getViewSize ();
		const CPoint c (r.getCenter ().x, r.top + 76);
		const double radius = 37.0;
		const double v = getValueNormalized ();
		const double a0 = 135.0, a1 = 135.0 + 270.0 * v;
		auto& f = fonts ();

		ctx->setFont (f.label);
		ctx->setFontColor (alpha (Palette::text, 0.78));
		drawTrackedCentered (ctx, label, c.x, r.top + 16, 9 * 0.16);

		// scale ticks and track
		for (int i = 0; i <= 10; ++i)
		{
			const double a = deg (135.0 + 27.0 * i);
			drawLine (ctx, polar (c, radius + 13, a), polar (c, radius + (i % 5 == 0 ? 18 : 16), a),
			          alpha (Palette::text, i % 5 == 0 ? 0.35 : 0.16), 1.0);
		}
		strokeArc (ctx, c, radius + 8, 135.0, 405.0, CColor (0x2A, 0x10, 0x13, 0xFF), 3.0);
		if (v > 0.001)
		{
			strokeArc (ctx, c, radius + 8, a0, a1, alpha (Palette::ember, 0.22), 9.0);
			strokeArc (ctx, c, radius + 8, a0, a1, Palette::ember, 2.6);
		}

		// drop shadow and body
		fillCircle (ctx, CPoint (c.x, c.y + 3), radius + 4, CColor (0, 0, 0, 170));
		if (auto path = VSTGUI::owned (ctx->createGraphicsPath ()))
		{
			path->addEllipse (CRect (c.x - radius, c.y - radius, c.x + radius, c.y + radius));
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0x3A, 0x38, 0x3E, 0xFF), CColor (0x06, 0x05, 0x06, 0xFF))))
			{
				g->addColorStop (0.45, CColor (0x15, 0x14, 0x17, 0xFF));
				ctx->fillRadialGradient (path, *g, CPoint (c.x - radius * 0.35, c.y - radius * 0.45), radius * 1.7);
			}
		}
		// brushing: concentric micro-grooves
		for (double rr = 3.0; rr < radius - 2.0; rr += 1.7)
		{
			const auto h = static_cast<unsigned> (rr * 97.0) % 7u;
			const CColor col = h < 3 ? alpha (Palette::text, 0.020 + 0.010 * h) : CColor (0, 0, 0, static_cast<uint8_t> (20 + 6 * h));
			ctx->setLineStyle (kLineSolid);
			ctx->setLineWidth (0.7);
			ctx->setFrameColor (col);
			ctx->drawEllipse (CRect (c.x - rr, c.y - rr, c.x + rr, c.y + rr), kDrawStroked);
		}
		// anisotropic highlights of the brushed metal
		for (double rr = radius * 0.30; rr < radius * 0.96; rr += 2.2)
		{
			strokeArc (ctx, c, rr, 200.0, 250.0, alpha (Palette::text, 0.07), 1.0);
			strokeArc (ctx, c, rr, 20.0, 70.0, alpha (Palette::text, 0.035), 1.0);
		}
		// bevel
		ctx->setLineWidth (1.0);
		ctx->setFrameColor (CColor (0x55, 0x50, 0x58, 0xA0));
		ctx->drawEllipse (CRect (c.x - radius, c.y - radius, c.x + radius, c.y + radius), kDrawStroked);
		ctx->setFrameColor (CColor (0, 0, 0, 0xC0));
		ctx->drawEllipse (CRect (c.x - radius + 3, c.y - radius + 3, c.x + radius - 3, c.y + radius - 3), kDrawStroked);

		// incandescent marker
		const double am = deg (a1);
		drawLine (ctx, polar (c, radius * 0.36, am), polar (c, radius * 0.86, am), alpha (Palette::ember, 0.35), 6.5);
		drawLine (ctx, polar (c, radius * 0.36, am), polar (c, radius * 0.86, am), Palette::marker, 2.4);
		fillCircle (ctx, polar (c, radius * 0.86, am), 1.8, CColor (0xFF, 0xD2, 0xB0, 0xFF));

		// value, tabular figures
		ctx->setFont (f.mono);
		ctx->setFontColor (Palette::text);
		drawCentered (ctx, valueText, c.x, r.top + 144);
	}

	CLASS_METHODS_NOCOPY (MetalKnob, CKnobBase)

private:
	std::string label;
	std::string valueText;
};

//------------------------------------------------------------------------
/** Segmented selector for the list parameters. */
class SegmentSelector : public CControl
{
public:
	SegmentSelector (const CRect& size, IControlListener* listener, int32_t tag, std::string titleText,
	                 std::vector<std::string> entries)
	: CControl (size, listener, tag), title (std::move (titleText)), labels (std::move (entries))
	{
	}

	int selected () const
	{
		const int n = static_cast<int> (labels.size ());
		return n > 1 ? std::clamp (static_cast<int> (std::lround (getValueNormalized () * (n - 1))), 0, n - 1) : 0;
	}

	CRect box () const
	{
		CRect b = getViewSize ();
		b.top += 18;
		b.bottom = b.top + 26;
		return b;
	}

	void onMouseDownEvent (MouseDownEvent& event) override
	{
		if (!event.buttonState.isLeft ())
			return;
		const CRect b = box ();
		if (!b.pointInside (event.mousePosition))
			return;
		const int n = static_cast<int> (labels.size ());
		const int idx = std::clamp (static_cast<int> ((event.mousePosition.x - b.left) / (b.getWidth () / n)), 0, n - 1);
		beginEdit ();
		setValueNormalized (n > 1 ? static_cast<float> (idx) / static_cast<float> (n - 1) : 0.f);
		valueChanged ();
		endEdit ();
		invalid ();
		event.consumed = true;
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect r = getViewSize ();
		const CRect b = box ();
		auto& f = fonts ();

		ctx->setFont (f.caption);
		ctx->setFontColor (alpha (Palette::text, 0.6));
		drawTracked (ctx, title, CPoint (r.left + 2, r.top + 10), 8 * 0.16);

		if (auto path = VSTGUI::owned (ctx->createGraphicsPath ()))
		{
			path->addRoundRect (b, 4.0);
			ctx->setFillColor (CColor (0x07, 0x04, 0x05, 0xE0));
			ctx->drawGraphicsPath (path, CDrawContext::kPathFilled);
		}

		const int n = static_cast<int> (labels.size ());
		const int sel = selected ();
		const CCoord segW = b.getWidth () / n;
		for (int i = 0; i < n; ++i)
		{
			CRect s (b.left + segW * i, b.top, b.left + segW * (i + 1), b.bottom);
			if (i == sel)
			{
				CRect glow (s);
				glow.inset (1.5, 1.5);
				if (auto path = VSTGUI::owned (ctx->createGraphicsPath ()))
				{
					path->addRoundRect (glow, 3.0);
					if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, alpha (Palette::ember, 0.95), CColor (0x6A, 0x0A, 0x12, 0xFF))))
						ctx->fillLinearGradient (path, *g, glow.getTopLeft (), glow.getBottomLeft ());
					strokePath (ctx, path, alpha (Palette::ember, 0.35), 3.0);
				}
			}
			else if (i > 0 && i != sel + 1)
				drawLine (ctx, CPoint (s.left, s.top + 6), CPoint (s.left, s.bottom - 6), alpha (Palette::text, 0.12), 1.0);

			ctx->setFont (f.mono);
			ctx->setFontColor (i == sel ? Palette::text : alpha (Palette::text, 0.42));
			drawCentered (ctx, labels[static_cast<size_t> (i)], s.getCenter ().x, s.bottom - 8.5);
		}

		if (auto path = VSTGUI::owned (ctx->createGraphicsPath ()))
		{
			path->addRoundRect (b, 4.0);
			strokePath (ctx, path, alpha (Palette::ember, 0.38), 1.0);
		}
	}

	CLASS_METHODS_NOCOPY (SegmentSelector, CControl)

private:
	std::string title;
	std::vector<std::string> labels;
};

//------------------------------------------------------------------------
/** Orbit HUD: spectra wrapped on the 0..360 reticle, phase coherence ring, PHASE TRANSFER. */
class OrbitView : public CView
{
public:
	OrbitView (const CRect& size, SymbiosisController* c) : CView (size), controller (c)
	{
		setMouseEnabled (false);
		master.fill (0.f);
		slaveIn.fill (0.f);
		slaveOut.fill (0.f);
		phase.fill (0.f);
	}

	bool attached (CView* parent) override
	{
		timer = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { tick (); }, 16, true); // ~60 FPS
		return CView::attached (parent);
	}

	bool removed (CView* parent) override
	{
		timer = nullptr;
		return CView::removed (parent);
	}

	void draw (CDrawContext* ctx) override
	{
		ctx->setDrawMode (kAntiAliasing | kNonIntegralMode);
		const CRect r = getViewSize ();
		if (!reticle || reticleSize != r.getSize ())
			renderReticle (ctx);
		if (reticle)
			reticle->draw (ctx, r);
		else
			drawReticle (ctx, r);

		const CPoint c = r.getCenter ();
		const double outer = outerRadius (r);
		const double inner = outer * 0.42;
		auto bandRadius = [&] (float level) { return inner + (outer * 0.97 - inner) * std::clamp (level, 0.f, 1.f); };
		auto bandAngle = [] (int b) { return deg (-90.0 + 360.0 * (b + 0.5) / DSP::kHudBands); };

		// phase coherence ring: cyan = constructive, ember = destructive
		const double ringR = inner * 0.86;
		for (int b = 0; b < DSP::kHudBands; ++b)
		{
			const float presence = std::min (master[static_cast<size_t> (b)], slaveOut[static_cast<size_t> (b)]);
			if (presence < 0.05f)
				continue;
			const double coherence = 0.5 * (1.0 + std::cos (phase[static_cast<size_t> (b)]));
			const double a = bandAngle (b);
			drawLine (ctx, polar (c, ringR - 7, a), polar (c, ringR + 1, a),
			          alpha (mix (Palette::ember, Palette::cyan, coherence), 0.25 + 0.75 * presence), 2.0);
		}

		auto trace = [&] (const std::array<float, DSP::kHudBands>& levels, bool twist) {
			auto path = VSTGUI::owned (ctx->createGraphicsPath ());
			if (!path)
				return path;
			for (int b = 0; b < DSP::kHudBands; ++b)
			{
				double a = bandAngle (b);
				if (twist) // phase error twists the ember trace off its band
					a += 0.9 * (2.0 * kPi / DSP::kHudBands) * std::sin (phase[static_cast<size_t> (b)]);
				const CPoint p = polar (c, bandRadius (levels[static_cast<size_t> (b)]), a);
				if (b == 0)
					path->beginSubpath (p);
				else
					path->addLine (p);
			}
			path->closeSubpath ();
			return path;
		};

		if (auto p = trace (slaveIn, false))
			strokePath (ctx, p, alpha (Palette::ember, 0.20), 1.0);
		if (auto p = trace (master, false))
		{
			strokePath (ctx, p, alpha (Palette::cyan, 0.16), 7.0);
			strokePath (ctx, p, alpha (Palette::cyan, 0.95), 1.6);
		}
		if (auto p = trace (slaveOut, true))
		{
			strokePath (ctx, p, alpha (Palette::ember, 0.20), 7.0);
			strokePath (ctx, p, Palette::ember, 1.7);
		}

		// readout
		auto& f = fonts ();
		char buf[64];
		std::snprintf (buf, sizeof buf, "%d%%", static_cast<int> (std::lround (100.0 * phaseTransfer)));
		ctx->setFont (f.readout);
		drawGlowText (ctx, buf, c.x, c.y + 10, Palette::blood);
		ctx->setFont (f.caption);
		ctx->setFontColor (alpha (Palette::blood, 0.9));
		drawTrackedCentered (ctx, "PHASE TRANSFER", c.x, c.y + 30, 8 * 0.16);

		ctx->setFont (f.monoSmall);
		ctx->setFontColor (alpha (Palette::text, 0.55));
		std::snprintf (buf, sizeof buf, "MOVED %2d%%", static_cast<int> (std::lround (100.0 * energyMoved)));
		drawCentered (ctx, buf, c.x, c.y - 44);
		if (f0Hz > 0.f)
			std::snprintf (buf, sizeof buf, "f0 %.1f Hz", static_cast<double> (f0Hz));
		else
			std::snprintf (buf, sizeof buf, "SHIELD %2d%%", static_cast<int> (std::lround (100.0 * shield)));
		drawCentered (ctx, buf, c.x, c.y + 48);

		if (!receiving)
		{
			ctx->setFontColor (alpha (Palette::text, 0.35));
			drawCentered (ctx, "NO SIGNAL", c.x, c.y + 62);
		}
	}

	CLASS_METHODS_NOCOPY (OrbitView, CView)

private:
	static double outerRadius (const CRect& r) { return std::min (r.getWidth (), r.getHeight ()) * 0.5 - 30.0; }

	void tick ()
	{
		DSP::HudFrame frame;
		const bool fresh = controller && controller->popLatestHudFrame (frame);
		if (fresh)
		{
			idleTicks = 0;
			receiving = true;
		}
		else if (++idleTicks > 45)
			receiving = false;
		if (!fresh && idleTicks > 160) // traces have faded out: stop repainting until data returns
			return;

		auto follow = [] (float& v, float target, float k) { v += (target - v) * k; };
		for (int b = 0; b < DSP::kHudBands; ++b)
		{
			const auto i = static_cast<size_t> (b);
			if (fresh)
			{
				follow (master[i], frame.master[b], 0.45f);
				follow (slaveIn[i], frame.slaveIn[b], 0.45f);
				follow (slaveOut[i], frame.slaveOut[b], 0.45f);
				// circular smoothing of the phase
				const double cs = 0.7 * std::cos (phase[i]) + 0.3 * std::cos (frame.phaseDelta[b]);
				const double sn = 0.7 * std::sin (phase[i]) + 0.3 * std::sin (frame.phaseDelta[b]);
				phase[i] = static_cast<float> (std::atan2 (sn, cs));
			}
			else if (!receiving)
			{
				master[i] *= 0.92f;
				slaveIn[i] *= 0.92f;
				slaveOut[i] *= 0.92f;
			}
		}
		if (fresh)
		{
			follow (phaseTransfer, frame.phaseTransfer, 0.12f);
			follow (energyMoved, frame.energyMoved, 0.2f);
			follow (shield, frame.shield, 0.3f);
			f0Hz = frame.f0Hz;
		}
		invalid ();
	}

	void renderReticle (CDrawContext* ctx)
	{
		const CRect r = getViewSize ();
		reticleSize = r.getSize ();
		const double scale = getFrame () ? getFrame ()->getScaleFactor () : 1.0;
		if (auto off = COffscreenContext::create (r.getSize (), scale))
		{
			off->beginDraw ();
			off->setDrawMode (kAntiAliasing | kNonIntegralMode);
			drawReticle (off, CRect (0, 0, r.getWidth (), r.getHeight ()));
			off->endDraw ();
			reticle = off->getBitmap ();
		}
		(void)ctx;
	}

	void drawReticle (CDrawContext* ctx, const CRect& r) const
	{
		const CPoint c = r.getCenter ();
		const double outer = outerRadius (r);
		const double inner = outer * 0.42;
		auto& f = fonts ();

		// dark lens
		if (auto path = VSTGUI::owned (ctx->createGraphicsPath ()))
		{
			path->addEllipse (CRect (c.x - outer - 14, c.y - outer - 14, c.x + outer + 14, c.y + outer + 14));
			if (auto g = VSTGUI::owned (CGradient::create (0.0, 1.0, CColor (0x1C, 0x08, 0x0B, 0xF0), CColor (0x0D, 0x08, 0x09, 0x00))))
				ctx->fillRadialGradient (path, *g, c, outer + 14);
		}

		// discreet dotted rings (level grid: -72, -54, -36, -18, 0 dBFS) and inner orbit
		auto dottedRing = [&] (double radius, double a, int dots) {
			for (int i = 0; i < dots; ++i)
				fillCircle (ctx, polar (c, radius, 2.0 * kPi * i / dots), 0.75, alpha (Palette::text, a));
		};
		for (int k = 1; k <= 5; ++k)
			dottedRing (inner + (outer * 0.97 - inner) * k / 5.0, k == 5 ? 0.30 : 0.16, static_cast<int> (outer * 1.4));
		dottedRing (inner, 0.30, 90);
		dottedRing (inner * 0.86 - 3, 0.10, 70);

		// angular reticle 0..360 degrees
		for (int d = 0; d < 360; d += 5)
		{
			const double a = deg (d - 90.0);
			const bool major = d % 30 == 0;
			drawLine (ctx, polar (c, outer + 4, a), polar (c, outer + (major ? 12 : (d % 10 == 0 ? 8 : 6)), a),
			          alpha (Palette::text, major ? 0.45 : 0.18), 1.0);
			if (major)
			{
				for (double rr = inner + 4; rr < outer; rr += 6)
					fillCircle (ctx, polar (c, rr, a), 0.6, alpha (Palette::text, 0.13));
				ctx->setFont (f.monoSmall);
				ctx->setFontColor (alpha (Palette::text, 0.45));
				char buf[16];
				std::snprintf (buf, sizeof buf, "%d\xC2\xB0", d);
				const CPoint p = polar (c, outer + 22, a);
				drawCentered (ctx, buf, p.x, p.y + 3.5);
			}
		}

		// band scale: 20 Hz at the top, rising clockwise to 20 kHz
		ctx->setFont (f.monoSmall);
		ctx->setFontColor (alpha (Palette::cyan, 0.45));
		const CPoint lo = polar (c, inner + 9, deg (-84.0));
		ctx->drawString ("20Hz", CPoint (lo.x + 2, lo.y + 3));
		ctx->setFontColor (alpha (Palette::ember, 0.45));
		const CPoint hi = polar (c, inner + 9, deg (-96.0));
		ctx->drawString ("20k", CPoint (hi.x - 2 - ctx->getStringWidth ("20k"), hi.y + 3));
	}

	SymbiosisController* controller {nullptr};
	SharedPointer<CVSTGUITimer> timer;
	SharedPointer<CBitmap> reticle;
	CPoint reticleSize;
	std::array<float, DSP::kHudBands> master {}, slaveIn {}, slaveOut {}, phase {};
	float phaseTransfer {0.5f};
	float energyMoved {0.f};
	float shield {0.f};
	float f0Hz {0.f};
	int idleTicks {1000};
	bool receiving {false};
};

//------------------------------------------------------------------------
SymbiosisEditor::SymbiosisEditor (SymbiosisController* controller)
: VSTGUIEditor (controller), symbiosisController (controller)
{
	ViewRect viewRect (0, 0, kWidth, kHeight);
	setRect (viewRect);
}

//------------------------------------------------------------------------
bool PLUGIN_API SymbiosisEditor::open (void* parent, const PlatformType& platformType)
{
	if (frame)
		return false;

	acquireFonts ();
	frame = new CFrame (CRect (0, 0, kWidth, kHeight), this);
	frame->setBackgroundColor (Palette::chassis);
	frame->addView (new ChassisView (CRect (0, 0, kWidth, kHeight)));

	// Orbit HUD
	frame->addView (new OrbitView (CRect (280, 72, 700, 492), symbiosisController));

	// knobs: left = transfer, right = dynamics / protection
	struct KnobDef
	{
		Vst::ParamID id;
		const char* label;
		CCoord x, y;
	};
	const KnobDef knobDefs[] = {
	    {kParamPullId, "PULL", 40, 92},           {kParamHarmonicsId, "HARMONICS", 40, 252},
	    {kParamTrackingId, "TRACKING", 146, 172}, {kParamInertiaId, "INERTIA", 810, 92},
	    {kParamReactionId, "REACTION", 810, 252}, {kParamTransShieldId, "TRANS-SHIELD", 704, 172},
	};
	for (const auto& k : knobDefs)
	{
		auto* knob = new MetalKnob (CRect (k.x, k.y, k.x + 130, k.y + 156), this, static_cast<int32_t> (k.id), k.label);
		knob->setDefaultValue (static_cast<float> (defaultNormalized (k.id)));
		knob->setValueNormalized (static_cast<float> (getController ()->getParamNormalized (k.id)));
		frame->addView (knob);
		controls[k.id] = knob;
		knobs[k.id] = knob;
		updateValueText (k.id);
	}

	// list parameters
	struct SelectorDef
	{
		Vst::ParamID id;
		const char* title;
		std::vector<std::string> entries;
		CCoord x, w;
	};
	const SelectorDef selectorDefs[] = {
	    {kParamPhaseLinkId, "PHASE LINK", {"OFF", "ON"}, 36, 150},
	    {kParamTargetModeId, "TARGET MODE", {"EXACT", "HARMONIC"}, 210, 200},
	    {kParamOversamplingId, "OVERSAMPLING", {"OFF", "2X", "4X"}, 434, 186},
	    {kParamDeltaSoloId, "DELTA SOLO", {"OFF", "ON"}, 644, 140},
	    {kParamBypassId, "BYPASS", {"OFF", "ON"}, 808, 136},
	};
	for (const auto& s : selectorDefs)
	{
		auto* sel = new SegmentSelector (CRect (s.x, kHeight - 92, s.x + s.w, kHeight - 46), this,
		                                 static_cast<int32_t> (s.id), s.title, s.entries);
		sel->setValueNormalized (static_cast<float> (getController ()->getParamNormalized (s.id)));
		frame->addView (sel);
		controls[s.id] = sel;
	}

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
	symbiosisController->editorOpened (this);
	return true;
}

//------------------------------------------------------------------------
void PLUGIN_API SymbiosisEditor::close ()
{
	symbiosisController->editorClosed (this);
	controls.clear ();
	knobs.clear ();
	if (frame)
	{
		frame->forget ();
		frame = nullptr;
		releaseFonts ();
	}
}

//------------------------------------------------------------------------
void SymbiosisEditor::valueChanged (CControl* control)
{
	const auto tag = static_cast<Vst::ParamID> (control->getTag ());
	if (tag >= kNumParams)
		return;
	symbiosisController->editFromGui (tag, control->getValueNormalized ());
	updateValueText (tag);
}

//------------------------------------------------------------------------
void SymbiosisEditor::parameterChanged (Vst::ParamID tag, double normalized)
{
	auto it = controls.find (tag);
	if (it == controls.end ())
		return;
	if (std::abs (it->second->getValueNormalized () - normalized) > 1e-6)
	{
		it->second->setValueNormalized (static_cast<float> (normalized));
		it->second->invalid ();
	}
	updateValueText (tag);
}

//------------------------------------------------------------------------
void SymbiosisEditor::updateValueText (Vst::ParamID tag)
{
	auto it = knobs.find (tag);
	if (it == knobs.end ())
		return;
	const double plain = toPlain (tag, getController ()->getParamNormalized (tag));
	char buf[32];
	if (tag == kParamInertiaId || tag == kParamReactionId)
		std::snprintf (buf, sizeof buf, "%.1f ms", plain);
	else
		std::snprintf (buf, sizeof buf, "%d %%", static_cast<int> (std::lround (plain * 100.0)));
	it->second->setValueText (buf);
}

} // namespace Symbiosis
