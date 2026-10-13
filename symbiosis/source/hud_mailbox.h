//------------------------------------------------------------------------
// SYMBIOSIS — hud_mailbox.h
// Direct HUD channel between the processor and the controller of one plug-in
// instance when both live in the same process (every host, unless the plug-in
// is bridged). The processor creates a mailbox and sends its id to the
// controller with one IMessage when they connect; process() then pushes HUD
// frames into a lock-free SPSC ring that the editor drains at 60 FPS.
//
// Why: without host support for IDataExchangeHandler, the SDK's fallback sends
// IMessages from a timer created on the thread that activates the plug-in.
// Some hosts activate from a thread without a message loop: that timer never
// fires and the Orbit stays empty. The data-exchange path stays as a backup
// (bridged plug-ins, other processes).
//
// The registry (mutex) is only used at connect / disconnect, never in process().
//------------------------------------------------------------------------
#pragma once

#include "dsp/ring_buffer.h"
#include "dsp/spectral_engine.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <unordered_map>

namespace Symbiosis {

struct HudMailbox
{
	DSP::SpscRingBuffer<DSP::HudFrame> ring {16};
};

class HudMailboxRegistry
{
public:
	/** Processor side (initialize): a new mailbox and its id. */
	static std::shared_ptr<HudMailbox> create (int64_t& id)
	{
		auto& r = instance ();
		std::lock_guard<std::mutex> lock (r.mutex);
		id = r.salt + (++r.counter);
		auto box = std::make_shared<HudMailbox> ();
		r.boxes[id] = box;
		return box;
	}

	/** Controller side (notify): the mailbox of that id, if it lives in this process. */
	static std::shared_ptr<HudMailbox> find (int64_t id)
	{
		auto& r = instance ();
		std::lock_guard<std::mutex> lock (r.mutex);
		auto it = r.boxes.find (id);
		return it != r.boxes.end () ? it->second.lock () : nullptr;
	}

	/** Processor side (terminate). */
	static void remove (int64_t id)
	{
		auto& r = instance ();
		std::lock_guard<std::mutex> lock (r.mutex);
		r.boxes.erase (id);
	}

private:
	HudMailboxRegistry ()
	{
		// random high bits: an id from another process never matches a mailbox here
		std::random_device rd;
		salt = (static_cast<int64_t> (rd () & 0x7FFFFFFFu) << 31) ^ static_cast<int64_t> (rd ());
		salt &= ~int64_t (0xFFFFF); // low bits for the counter
	}
	static HudMailboxRegistry& instance ()
	{
		static HudMailboxRegistry registry;
		return registry;
	}

	std::mutex mutex;
	std::unordered_map<int64_t, std::weak_ptr<HudMailbox>> boxes;
	int64_t salt {0};
	int64_t counter {0};
};

} // namespace Symbiosis
