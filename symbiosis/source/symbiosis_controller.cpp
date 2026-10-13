//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_controller.cpp
//------------------------------------------------------------------------
#include "symbiosis_controller.h"

#include "ui/symbiosis_editor.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Symbiosis {

namespace {

void addRange (ParameterContainer& params, const TChar* title, ParamID id, const TChar* units, int32 precision)
{
	const ParamSpec& s = kParamSpecs[id];
	auto* p = new RangeParameter (title, id, units, s.minPlain, s.maxPlain, s.defaultPlain);
	p->setPrecision (precision);
	params.addParameter (p);
}

void addList (ParameterContainer& params, const TChar* title, ParamID id, std::initializer_list<const TChar*> entries,
              int32 extraFlags = 0)
{
	auto* p = new StringListParameter (title, id, nullptr,
	                                   ParameterInfo::kCanAutomate | ParameterInfo::kIsList | extraFlags);
	for (const TChar* e : entries)
		p->appendString (e);
	const double def = defaultNormalized (id);
	p->getInfo ().defaultNormalizedValue = def;
	p->setNormalized (def);
	params.addParameter (p);
}

} // namespace

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisController::initialize (FUnknown* context)
{
	const tresult result = EditController::initialize (context);
	if (result != kResultOk)
		return result;

	addRange (parameters, STR16 ("Pull"), kParamPullId, nullptr, 2);
	addRange (parameters, STR16 ("Harmonics"), kParamHarmonicsId, nullptr, 2);
	addRange (parameters, STR16 ("Inertia"), kParamInertiaId, STR16 ("ms"), 1);
	addRange (parameters, STR16 ("Reaction"), kParamReactionId, STR16 ("ms"), 1);
	addRange (parameters, STR16 ("Tracking"), kParamTrackingId, nullptr, 2);
	addRange (parameters, STR16 ("Trans-Shield"), kParamTransShieldId, nullptr, 2);
	addList (parameters, STR16 ("Phase Link"), kParamPhaseLinkId, {STR16 ("Off"), STR16 ("On")});
	addList (parameters, STR16 ("Delta Solo"), kParamDeltaSoloId, {STR16 ("Off"), STR16 ("On")});
	addList (parameters, STR16 ("Target Mode"), kParamTargetModeId, {STR16 ("Exact"), STR16 ("Harmonic")});
	addList (parameters, STR16 ("Oversampling"), kParamOversamplingId, {STR16 ("Off"), STR16 ("2x"), STR16 ("4x")});
	addList (parameters, STR16 ("Bypass"), kParamBypassId, {STR16 ("Off"), STR16 ("On")}, ParameterInfo::kIsBypass);
	return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisController::terminate ()
{
	activeEditor = nullptr;
	hudMailbox.reset ();
	return EditController::terminate ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisController::setComponentState (IBStream* state)
{
	// same layout as SymbiosisProcessor::getState
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
			setParamNormalized (static_cast<ParamID> (i), value);
	}
	return kResultOk;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API SymbiosisController::createView (FIDString name)
{
	if (name && std::strcmp (name, ViewType::kEditor) == 0)
		return new SymbiosisEditor (this);
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisController::setParamNormalized (ParamID tag, ParamValue value)
{
	const tresult result = EditController::setParamNormalized (tag, value);
	if (result == kResultOk && activeEditor)
		activeEditor->parameterChanged (tag, getParamNormalized (tag));
	return result;
}

//------------------------------------------------------------------------
void SymbiosisController::editFromGui (ParamID tag, ParamValue value)
{
	EditController::setParamNormalized (tag, value);
	performEdit (tag, getParamNormalized (tag));
}

//------------------------------------------------------------------------
tresult PLUGIN_API SymbiosisController::notify (IMessage* message)
{
	if (message && message->getMessageID () && std::strcmp (message->getMessageID (), kHudMailboxMessageId) == 0)
	{
		int64 id = 0;
		if (auto attributes = message->getAttributes ())
			if (attributes->getInt (kHudMailboxIdAttr, id) == kResultOk)
				hudMailbox = HudMailboxRegistry::find (id);
		return kResultOk;
	}
	if (hudReceiver.onMessage (message))
		return kResultTrue;
	return EditController::notify (message);
}

//------------------------------------------------------------------------
void PLUGIN_API SymbiosisController::queueOpened (DataExchangeUserContextID userContextID, uint32 /*blockSize*/,
                                                  TBool& dispatchOnBackgroundThread)
{
	// the HUD queue is only a producer for the SPSC ring: safe on any thread
	dispatchOnBackgroundThread = userContextID == kHudExchangeContextId;
}

//------------------------------------------------------------------------
void PLUGIN_API SymbiosisController::queueClosed (DataExchangeUserContextID /*userContextID*/) {}

//------------------------------------------------------------------------
void PLUGIN_API SymbiosisController::onDataExchangeBlocksReceived (DataExchangeUserContextID userContextID,
                                                                   uint32 numBlocks, DataExchangeBlock* blocks,
                                                                   TBool /*onBackgroundThread*/)
{
	if (userContextID != kHudExchangeContextId)
		return;
	for (uint32 i = 0; i < numBlocks; ++i)
	{
		if (!blocks[i].data || blocks[i].size < sizeof (DSP::HudFrame))
			continue;
		DSP::HudFrame frame;
		std::memcpy (&frame, blocks[i].data, sizeof (DSP::HudFrame));
		hudQueue.push (frame); // dropped if the GUI is not consuming (editor closed)
	}
}

} // namespace Symbiosis
