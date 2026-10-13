//------------------------------------------------------------------------
// SYMBIOSIS — symbiosis_controller.h
// Edit controller: parameters, editor, and the receiving end of the HUD feed.
//
// HUD paths (no mutex in the audio path):
//   1. audio thread --HudMailbox (SPSC ring, same process)--> editor timer at
//      60 FPS on the GUI thread; the mailbox id comes in one IMessage at connect.
//   2. backup: audio thread --DataExchangeHandler--> onDataExchangeBlocksReceived()
//      --SpscRingBuffer--> editor timer (bridged plug-ins, other processes).
//   The editor takes the most recent frame of either path (by serial).
//------------------------------------------------------------------------
#pragma once

#include "dsp/ring_buffer.h"
#include "dsp/spectral_engine.h"
#include "hud_mailbox.h"
#include "symbiosis_cids.h"

#include "public.sdk/source/vst/utility/dataexchange.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace Symbiosis {

class SymbiosisEditor;

class SymbiosisController : public Steinberg::Vst::EditController, public Steinberg::Vst::IDataExchangeReceiver
{
public:
	SymbiosisController () = default;
	~SymbiosisController () SMTG_OVERRIDE = default;

	static Steinberg::FUnknown* createInstance (void* /*context*/)
	{
		return static_cast<Steinberg::Vst::IEditController*> (new SymbiosisController);
	}

	//--- EditController -------------------------------------------------------
	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setComponentState (Steinberg::IBStream* state) SMTG_OVERRIDE;
	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID tag,
	                                                  Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API notify (Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

	//--- IDataExchangeReceiver -------------------------------------------------
	void PLUGIN_API queueOpened (Steinberg::Vst::DataExchangeUserContextID userContextID,
	                             Steinberg::uint32 blockSize,
	                             Steinberg::TBool& dispatchOnBackgroundThread) SMTG_OVERRIDE;
	void PLUGIN_API queueClosed (Steinberg::Vst::DataExchangeUserContextID userContextID) SMTG_OVERRIDE;
	void PLUGIN_API onDataExchangeBlocksReceived (Steinberg::Vst::DataExchangeUserContextID userContextID,
	                                              Steinberg::uint32 numBlocks,
	                                              Steinberg::Vst::DataExchangeBlock* blocks,
	                                              Steinberg::TBool onBackgroundThread) SMTG_OVERRIDE;

	//--- editor support (GUI thread) ------------------------------------------
	/** Consumer side of the HUD queue: the most recent frame, if any arrived. */
	bool popLatestHudFrame (DSP::HudFrame& frame) noexcept
	{
		bool got = hudQueue.popLatest (frame);
		if (hudMailbox && hudMailbox->ring.popLatest (mailboxFrame) && (!got || mailboxFrame.serial >= frame.serial))
		{
			frame = mailboxFrame;
			got = true;
		}
		return got;
	}
	void editorOpened (SymbiosisEditor* editor) { activeEditor = editor; }
	void editorClosed (SymbiosisEditor* editor)
	{
		if (activeEditor == editor)
			activeEditor = nullptr;
	}
	/** GUI edit (the controls send beginEdit / endEdit through the frame). */
	void editFromGui (Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value);

	OBJ_METHODS (SymbiosisController, EditController)
	DEFINE_INTERFACES
		DEF_INTERFACE (Steinberg::Vst::IDataExchangeReceiver)
	END_DEFINE_INTERFACES (EditController)
	REFCOUNT_METHODS (EditController)

private:
	Steinberg::Vst::DataExchangeReceiverHandler hudReceiver {this};
	DSP::SpscRingBuffer<DSP::HudFrame> hudQueue {8};
	std::shared_ptr<HudMailbox> hudMailbox; // set by the processor's message (GUI thread)
	DSP::HudFrame mailboxFrame;
	SymbiosisEditor* activeEditor {nullptr};
};

} // namespace Symbiosis
