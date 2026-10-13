//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_processor.h
// Audio processor: bus layout, parameters, state, STFT engine, HUD feed.
//------------------------------------------------------------------------
#pragma once

#include "dsp/spectral_engine.h"
#include "hud_mailbox.h"
#include "symbiosis_cids.h"

#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>

namespace Symbiosis {

class SymbiosisProcessor : public Steinberg::Vst::AudioEffect
{
public:
	SymbiosisProcessor ();
	~SymbiosisProcessor () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return static_cast<Steinberg::Vst::IAudioProcessor*> (new SymbiosisProcessor);
	}

	//--- AudioEffect / IComponent -------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getState (Steinberg::IBStream* state) SMTG_OVERRIDE;

	//--- IConnectionPoint (data exchange with the controller) --------------
	Steinberg::tresult PLUGIN_API connect (Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API disconnect (Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;

	//--- IAudioProcessor ------------------------------------------------------
	Steinberg::tresult PLUGIN_API setBusArrangements (Steinberg::Vst::SpeakerArrangement* inputs,
	                                                  Steinberg::int32 numIns,
	                                                  Steinberg::Vst::SpeakerArrangement* outputs,
	                                                  Steinberg::int32 numOuts) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getLatencySamples () SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getTailSamples () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

	static constexpr int kFFTSize = DSP::SpectralEngine::kFFTSize;

private:
	DSP::EngineParams makeEngineParams () const;
	void sendHudFrame (Steinberg::int32 numSamples);

	DSP::SpectralEngine engine;
	// normalized values; written by process() and by setState() (UI thread), hence atomic
	std::array<std::atomic<double>, kNumParams> paramValues;
	std::atomic<bool> paramsDirty {true};
	Steinberg::Vst::DataExchangeHandler hudExchange;
	std::shared_ptr<HudMailbox> hudMailbox; // direct path to the controller (same process)
	int64_t hudMailboxId {0};
	Steinberg::uint32 lastHudSerial {0};
	double samplesSinceHud {0.0};
};

} // namespace Symbiosis
