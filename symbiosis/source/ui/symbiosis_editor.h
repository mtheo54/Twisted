//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_editor.h
// Native VSTGUI4 editor, built in code (no .uidesc):
//   - chassis: absolute black #0D0809, laser-abraded grain, bordeaux top glow
//   - Orbit HUD: master (cyan) and redistributed slave (ember) spectra wrapped
//     on a 0..360 degree reticle, phase-coherence ring, PHASE TRANSFER readout
//   - brushed black anodised aluminium knobs with incandescent markers
//------------------------------------------------------------------------
#pragma once

#include "symbiosis_cids.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <map>

namespace VSTGUI {
class CControl;
class CVSTGUITimer;
}

namespace Symbiosis {

class SymbiosisController;
class OrbitView;
class MetalKnob;

class SymbiosisEditor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
	static constexpr int kWidth = 980;
	static constexpr int kHeight = 600;

	explicit SymbiosisEditor (SymbiosisController* controller);

	//--- VSTGUIEditor ---------------------------------------------------------
	bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
	void PLUGIN_API close () SMTG_OVERRIDE;

	//--- IControlListener -----------------------------------------------------
	void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;

	/** Host automation / state load: called by the controller on the GUI thread. */
	void parameterChanged (Steinberg::Vst::ParamID tag, double normalized);

private:
	void updateValueText (Steinberg::Vst::ParamID tag);

	SymbiosisController* symbiosisController {nullptr};
	std::map<Steinberg::Vst::ParamID, VSTGUI::CControl*> controls;
	std::map<Steinberg::Vst::ParamID, MetalKnob*> knobs;
};

} // namespace Symbiosis
