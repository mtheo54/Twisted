//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_processor.cpp
//------------------------------------------------------------------------
#include "symbiosis_processor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cstring>

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <pmmintrin.h>
#include <xmmintrin.h>
#define SYMBIOSIS_X86 1
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Symbiosis {

namespace {

//------------------------------------------------------------------------
/** Denormals to zero for the duration of process() (FTZ + DAZ on x86, FZ on ARM64). */
class ScopedFlushDenormals
{
public:
	ScopedFlushDenormals () noexcept
	{
#if SYMBIOSIS_X86
		saved = _mm_getcsr ();
		_MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
		_MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
		asm volatile ("mrs %0, fpcr" : "=r"(saved));
		const uint64_t withFz = saved | (uint64_t (1) << 24);
		asm volatile ("msr fpcr, %0" : : "r"(withFz));
#endif
	}
	~ScopedFlushDenormals () noexcept
	{
#if SYMBIOSIS_X86
		_mm_setcsr (saved);
#elif defined(__aarch64__)
		asm volatile ("msr fpcr, %0" : : "r"(saved));
#endif
	}

private:
#if SYMBIOSIS_X86
	unsigned int saved {0};
#elif defined(__aarch64__)
	uint64_t saved {0};
#endif
};

bool isMonoOrStereo (SpeakerArrangement arr)
{
	return arr == SpeakerArr::kMono || arr == SpeakerArr::kStereo;
}

} // namespace

//------------------------------------------------------------------------
SymbiosisProcessor::SymbiosisProcessor ()
: hudExchange (this, [] (DataExchangeHandler::Config& config, const ProcessSetup&) {
	config.blockSize = static_cast<uint32> (sizeof (DSP::HudFrame));
	config.numBlocks = 16;
	config.alignment = 32;
	config.userContextID = kHudExchangeContextId;
	return true;
})
{
	setControllerClass (kSymbiosisControllerUID);
	for (ParamID id = 0; id < kNumParams; ++id)
		paramValues[id].store (defaultNormalized (id));
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::initialize (FUnknown* context)
{
	const tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// Bus 0: slave x2(t), the track that makes room. Bus 1: master x1(t), sidechain.
	addAudioInput (STR16 ("Slave In"), SpeakerArr::kStereo);
	addAudioInput (STR16 ("Master Sidechain"), SpeakerArr::kStereo, kAux, 0);
	addAudioOutput (STR16 ("Slave Out"), SpeakerArr::kStereo);
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::terminate ()
{
	return AudioEffect::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::connect (IConnectionPoint* other)
{
	const tresult result = AudioEffect::connect (other);
	if (result == kResultTrue)
		hudExchange.onConnect (other, getHostContext ());
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::disconnect (IConnectionPoint* other)
{
	hudExchange.onDisconnect (other);
	return AudioEffect::disconnect (other);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::setBusArrangements (SpeakerArrangement* inputs, int32 numIns,
                                                           SpeakerArrangement* outputs, int32 numOuts)
{
	// main in == main out (mono or stereo), sidechain mono or stereo
	if (numIns < 1 || numIns > 2 || numOuts != 1)
		return kResultFalse;
	if (!isMonoOrStereo (inputs[0]) || inputs[0] != outputs[0])
		return kResultFalse;
	if (numIns == 2 && !isMonoOrStereo (inputs[1]))
		return kResultFalse;
	return AudioEffect::setBusArrangements (inputs, numIns, outputs, numOuts);
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::setupProcessing (ProcessSetup& setup)
{
	const tresult result = AudioEffect::setupProcessing (setup);
	if (result == kResultOk)
		engine.prepare (setup.sampleRate); // every buffer is allocated here
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::setActive (TBool state)
{
	if (state)
	{
		paramsDirty.store (false);
		engine.setParams (makeEngineParams ());
		engine.reset ();
		lastHudSerial = 0;
		samplesSinceHud = 0.0;
		hudExchange.onActivate (processSetup);
	}
	else
		hudExchange.onDeactivate ();
	return AudioEffect::setActive (state);
}

//------------------------------------------------------------------------
uint32 PLUGIN_API SymbiosisProcessor::getLatencySamples ()
{
	return static_cast<uint32> (kFFTSize); // 2048 samples
}

//------------------------------------------------------------------------
uint32 PLUGIN_API SymbiosisProcessor::getTailSamples ()
{
	return static_cast<uint32> (kFFTSize);
}

//------------------------------------------------------------------------
DSP::EngineParams SymbiosisProcessor::makeEngineParams () const
{
	auto paramValue = [this] (ParamID id) { return paramValues[id].load (std::memory_order_relaxed); };
	DSP::EngineParams p;
	p.pull = static_cast<float> (toPlain (kParamPullId, paramValue (kParamPullId)));
	p.harmonics = static_cast<float> (toPlain (kParamHarmonicsId, paramValue (kParamHarmonicsId)));
	p.inertiaMs = static_cast<float> (toPlain (kParamInertiaId, paramValue (kParamInertiaId)));
	p.reactionMs = static_cast<float> (toPlain (kParamReactionId, paramValue (kParamReactionId)));
	p.tracking = static_cast<float> (toPlain (kParamTrackingId, paramValue (kParamTrackingId)));
	p.transShield = static_cast<float> (toPlain (kParamTransShieldId, paramValue (kParamTransShieldId)));
	p.phaseLink = toPlain (kParamPhaseLinkId, paramValue (kParamPhaseLinkId)) > 0.5;
	p.deltaSolo = toPlain (kParamDeltaSoloId, paramValue (kParamDeltaSoloId)) > 0.5;
	p.bypass = toPlain (kParamBypassId, paramValue (kParamBypassId)) > 0.5;
	p.targetMode = toPlain (kParamTargetModeId, paramValue (kParamTargetModeId)) > 0.5
	                   ? DSP::TargetMode::HarmonicSeries
	                   : DSP::TargetMode::ExactPeaks;
	const int os = static_cast<int> (toPlain (kParamOversamplingId, paramValue (kParamOversamplingId)));
	p.oversampling = os >= 2 ? 4 : (os == 1 ? 2 : 1);
	return p;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::process (ProcessData& data)
{
	ScopedFlushDenormals flushDenormals;

	//--- parameter changes (last point of each queue: the engine works per hop) ---
	if (IParameterChanges* changes = data.inputParameterChanges)
	{
		bool changed = false;
		const int32 numQueues = changes->getParameterCount ();
		for (int32 i = 0; i < numQueues; ++i)
		{
			IParamValueQueue* queue = changes->getParameterData (i);
			if (!queue)
				continue;
			const ParamID id = queue->getParameterId ();
			const int32 numPoints = queue->getPointCount ();
			ParamValue value = 0.0;
			int32 offset = 0;
			if (id < kNumParams && numPoints > 0 && queue->getPoint (numPoints - 1, offset, value) == kResultTrue)
			{
				paramValues[id].store (value, std::memory_order_relaxed);
				changed = true;
			}
		}
		if (changed)
			paramsDirty.store (true, std::memory_order_relaxed);
	}
	if (paramsDirty.exchange (false, std::memory_order_acquire))
		engine.setParams (makeEngineParams ());

	if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1)
		return kResultOk;
	if (data.symbolicSampleSize != kSample32)
		return kResultFalse;

	AudioBusBuffers& in = data.inputs[0];
	AudioBusBuffers& out = data.outputs[0];
	if (!in.channelBuffers32 || !out.channelBuffers32)
		return kResultOk;
	int32 numChannels = std::min (in.numChannels, out.numChannels);
	for (int32 ch = 0; ch < numChannels; ++ch)
		if (!in.channelBuffers32[ch] || !out.channelBuffers32[ch])
			numChannels = ch; // never touch a missing channel buffer

	const float* const* master = nullptr;
	int32 numMaster = 0;
	if (data.numInputs > 1)
	{
		AudioBusBuffers& side = data.inputs[1];
		AudioBus* sideBus = getAudioInput (1);
		if (sideBus && sideBus->isActive () && side.numChannels > 0 && side.channelBuffers32)
		{
			master = side.channelBuffers32;
			numMaster = std::min<int32> (side.numChannels, DSP::SpectralEngine::kMaxChannels);
			for (int32 ch = 0; ch < numMaster; ++ch)
				if (!master[ch])
					numMaster = ch; // a missing channel ends the sidechain
			if (numMaster == 0)
				master = nullptr;
		}
	}

	engine.process (in.channelBuffers32, master, numMaster, out.channelBuffers32, numChannels, data.numSamples);

	for (int32 ch = numChannels; ch < out.numChannels; ++ch)
		if (out.channelBuffers32[ch])
			std::memset (out.channelBuffers32[ch], 0, sizeof (float) * static_cast<size_t> (data.numSamples));
	out.silenceFlags = 0;

	sendHudFrame (data.numSamples);
	return kResultOk;
}

//------------------------------------------------------------------------
void SymbiosisProcessor::sendHudFrame (int32 numSamples)
{
	// at most 60 frames per second to the editor, through the lock-free data-exchange queue
	samplesSinceHud += numSamples;
	const DSP::HudFrame& hud = engine.getHudFrame ();
	if (hud.serial == lastHudSerial || samplesSinceHud < processSetup.sampleRate / 60.0)
		return;
	DataExchangeBlock block = hudExchange.getCurrentOrNewBlock ();
	if (block.blockID == InvalidDataExchangeBlockID || !block.data || block.size < sizeof (DSP::HudFrame))
		return;
	std::memcpy (block.data, &hud, sizeof (DSP::HudFrame));
	hudExchange.sendCurrentBlock ();
	lastHudSerial = hud.serial;
	samplesSinceHud = 0.0;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::setState (IBStream* state)
{
	if (!state)
		return kResultFalse;
	IBStreamer streamer (state, kLittleEndian);
	int32 version = 0, count = 0;
	if (!streamer.readInt32 (version) || version < 1 || !streamer.readInt32 (count))
		return kResultFalse;
	for (int32 i = 0; i < count; ++i)
	{
		double value = 0.0;
		if (!streamer.readDouble (value))
			return kResultFalse;
		if (i < kNumParams)
			paramValues[static_cast<size_t> (i)].store (std::clamp (value, 0.0, 1.0), std::memory_order_relaxed);
	}
	paramsDirty.store (true, std::memory_order_release); // applied by the next process()
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisProcessor::getState (IBStream* state)
{
	if (!state)
		return kResultFalse;
	IBStreamer streamer (state, kLittleEndian);
	streamer.writeInt32 (kStateVersion);
	streamer.writeInt32 (kNumParams);
	for (ParamID id = 0; id < kNumParams; ++id)
		streamer.writeDouble (paramValues[id].load (std::memory_order_relaxed));
	return kResultOk;
}

} // namespace Symbiosis
