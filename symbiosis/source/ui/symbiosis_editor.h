//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_editor.h
// Native VSTGUI4 editor, drawn in code with pre-rendered PNG textures (no .uidesc):
// ivory ink-marbled paper, gunmetal astrolabe Orbit visualizer with zodiac
// ring and gears, smoky crystal and steel knobs, ruby sliders, LED buttons,
// menus, level meters and scopes. Authored on an 860 x 560 canvas, shown at 120 %.
//------------------------------------------------------------------------
#pragma once

#include "dsp/spectral_engine.h"
#include "symbiosis_cids.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace VSTGUI {
class CControl;
class CView;
class CVSTGUITimer;
}

namespace Symbiosis {

class SymbiosisController;

/** Smoothed HUD data shared by the Orbit, the meters and the scopes (GUI thread only). */
struct HudState
{
	std::array<float, DSP::kHudBands> master {}, slaveIn {}, slaveOut {}, phase {};
	std::array<float, DSP::kHudWavePoints> slaveWave {}, masterWave {};
	float influence {0.f};
	float slaveOutDb {-120.f};
	float masterDb {-120.f};
	double time {0.0};
	int idleTicks {1000};
	bool receiving {false};
	bool masterPresent {false};

	// display options (FREEZE / MASTER / SLAVE / INPUT buttons)
	bool freeze {false};
	bool showMaster {true};
	bool showSlave {true};
	bool showInput {false};
};

class SymbiosisEditor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
	static constexpr int kCanvasWidth = 860;
	static constexpr int kCanvasHeight = 560;
	static constexpr double kZoom = 1.2;
	static constexpr int kWidth = 1032;  // kCanvasWidth * kZoom
	static constexpr int kHeight = 672;  // kCanvasHeight * kZoom

	explicit SymbiosisEditor (SymbiosisController* controller);

	//--- VSTGUIEditor ---------------------------------------------------------
	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;

	//--- IControlListener -----------------------------------------------------
	void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;

	/** Host automation / state load: called by the controller on the GUI thread. */
	void parameterChanged (Steinberg::Vst::ParamID tag, double normalized);

	//--- used by the views ----------------------------------------------------
	double getParam (Steinberg::Vst::ParamID tag) const;
	double getPlain (Steinberg::Vst::ParamID tag) const { return toPlain (tag, getParam (tag)); }
	/** One complete gesture: beginEdit, performEdit, endEdit. */
	void setParamFromGui (Steinberg::Vst::ParamID tag, double normalized);
	void setPlainFromGui (Steinberg::Vst::ParamID tag, double plain)
	{
		setParamFromGui (tag, toNormalized (tag, plain));
	}
	void applyPreset (const FactoryPreset& preset);
	/** Name of the factory preset matching the current settings, or "CUSTOM". */
	std::string currentPresetName () const;
	HudState& getHud () { return hud; }
	void refreshParamViews ();

private:
	void tick ();

	SymbiosisController* symbiosisController {nullptr};
	HudState hud;
	VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;
	std::map<Steinberg::Vst::ParamID, VSTGUI::CControl*> controls;
	std::vector<VSTGUI::CView*> paramViews; // buttons and menus drawn from parameter values
	std::vector<VSTGUI::CView*> hudViews;   // redrawn on each HUD tick
};

} // namespace Symbiosis
