//------------------------------------------------------------------------
// SYMBIOSIS — ring_buffer.h
//
// SpscRingBuffer: lock-free FIFO, one producer thread and one consumer
//   thread, no mutex. Carries the Orbit HUD frames from the data-exchange
//   thread to the GUI thread (60 FPS).
//
// CircularBuffer: power-of-two sample history used by the STFT engine
//   (analysis frames, output overlap-add, dry delay line). Single thread.
//
// Both allocate only in their constructor / prepare().
//------------------------------------------------------------------------
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>

namespace Symbiosis::DSP {

//------------------------------------------------------------------------
template <typename T>
class SpscRingBuffer
{
public:
	/** capacity is rounded up to a power of two. One slot stays empty. */
	explicit SpscRingBuffer (size_t requestedCapacity = 8) { prepare (requestedCapacity); }

	/** Not thread safe: call before the producer and the consumer start. */
	void prepare (size_t requestedCapacity)
	{
		size_t cap = 2;
		while (cap < requestedCapacity + 1)
			cap <<= 1;
		slots.reset (new T[cap]);
		mask = cap - 1;
		head.store (0, std::memory_order_relaxed);
		tail.store (0, std::memory_order_relaxed);
	}

	/** Producer. Returns false when full (the item is dropped). */
	bool push (const T& item) noexcept
	{
		const size_t h = head.load (std::memory_order_relaxed);
		const size_t next = (h + 1) & mask;
		if (next == tail.load (std::memory_order_acquire))
			return false;
		slots[h] = item;
		head.store (next, std::memory_order_release);
		return true;
	}

	/** Consumer. Returns false when empty. */
	bool pop (T& item) noexcept
	{
		const size_t t = tail.load (std::memory_order_relaxed);
		if (t == head.load (std::memory_order_acquire))
			return false;
		item = slots[t];
		tail.store ((t + 1) & mask, std::memory_order_release);
		return true;
	}

	/** Consumer. Drains the queue and keeps only the most recent item. */
	bool popLatest (T& item) noexcept
	{
		bool any = false;
		while (pop (item))
			any = true;
		return any;
	}

	size_t capacity () const noexcept { return mask; }

private:
	std::unique_ptr<T[]> slots;
	size_t mask {0};
	static constexpr size_t kLine = 64; // cache line: head and tail never share one
	alignas (kLine) std::atomic<size_t> head {0};
	alignas (kLine) std::atomic<size_t> tail {0};
};

//------------------------------------------------------------------------
class CircularBuffer
{
public:
	/** size: power of two. */
	void prepare (int newSize)
	{
		size = newSize;
		mask = newSize - 1;
		data.reset (new float[static_cast<size_t> (newSize)]);
		clear ();
	}

	void clear () noexcept { std::fill (data.get (), data.get () + size, 0.f); }

	float& operator[] (int index) noexcept { return data[static_cast<size_t> (index & mask)]; }
	float operator[] (int index) const noexcept { return data[static_cast<size_t> (index & mask)]; }

	int getSize () const noexcept { return size; }
	int getMask () const noexcept { return mask; }

private:
	std::unique_ptr<float[]> data;
	int size {0};
	int mask {0};
};

} // namespace Symbiosis::DSP
