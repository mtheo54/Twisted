//------------------------------------------------------------------------
// SYMBIOSIS — yin_pitch.h
// Real-time YIN pitch detector (de Cheveigné & Kawahara, 2002), used by the
// "Harmonic Series" target mode to find the fundamental f0 of a bass.
//
// The input is low-pass filtered and decimated to about 11-12 kHz: bass
// fundamentals stay below 1 kHz, and the difference function then costs
// about 230k multiply-adds per analysis (once per STFT hop).
//
// prepare() allocates. push() and analyze() allocate nothing.
//------------------------------------------------------------------------
#pragma once

#include "ring_buffer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Symbiosis::DSP {

class YinPitch
{
public:
	void prepare (double sampleRate, float minHz = 25.f, float maxHz = 1000.f)
	{
		decimation = std::max (1, static_cast<int> (std::floor (sampleRate / 11000.0)));
		rate = sampleRate / decimation;
		tauMax = static_cast<int> (std::ceil (rate / minHz));
		tauMin = std::max (2, static_cast<int> (std::floor (rate / maxHz)));
		window = tauMax;

		int histSize = 2;
		while (histSize < window + tauMax + 2)
			histSize <<= 1;
		history.prepare (histSize);
		frame.assign (static_cast<size_t> (window + tauMax + 2), 0.f);
		diff.assign (static_cast<size_t> (tauMax + 2), 0.f);

		// 4th-order Butterworth low-pass (two biquads, Q = 0.5412 and 1.3066)
		const double fc = std::min (1500.0, 0.35 * rate);
		designLowPass (lp[0], sampleRate, fc, 0.54119610);
		designLowPass (lp[1], sampleRate, fc, 1.30656296);
		reset ();
	}

	void reset () noexcept
	{
		history.clear ();
		writePos = 0;
		phase = 0;
		for (auto& b : lp)
			b.z1 = b.z2 = 0.0;
		lastF0 = 0.f;
		lastConfidence = 0.f;
	}

	/** One input sample at the plug-in sample rate. */
	void push (float x) noexcept
	{
		double y = x;
		for (auto& b : lp)
			y = b.process (y);
		if (++phase >= decimation)
		{
			phase = 0;
			history[writePos] = static_cast<float> (y);
			writePos = (writePos + 1) & history.getMask ();
		}
	}

	/** Analyses the latest window. Returns f0 in Hz, or 0 when unvoiced. */
	float analyze (float threshold = 0.15f) noexcept
	{
		const int span = window + tauMax + 1;
		float energy = 0.f;
		for (int i = 0; i < span; ++i)
		{
			const float v = history[writePos - span + i];
			frame[static_cast<size_t> (i)] = v;
			energy += v * v;
		}
		lastF0 = 0.f;
		lastConfidence = 0.f;
		if (energy < 1e-9f * static_cast<float> (span))
			return 0.f;

		// difference function and cumulative mean normalisation
		const float* x = frame.data ();
		diff[0] = 1.f;
		double running = 0.0;
		for (int tau = 1; tau <= tauMax; ++tau)
		{
			float d = 0.f;
			const float* y = x + tau;
			for (int j = 0; j < window; ++j)
			{
				const float delta = x[j] - y[j];
				d += delta * delta;
			}
			running += d;
			diff[static_cast<size_t> (tau)] =
			    running > 0.0 ? static_cast<float> (static_cast<double> (d) * tau / running) : 1.f;
		}

		int best = -1;
		for (int tau = tauMin; tau < tauMax; ++tau)
		{
			if (diff[static_cast<size_t> (tau)] < threshold)
			{
				while (tau + 1 < tauMax && diff[static_cast<size_t> (tau + 1)] < diff[static_cast<size_t> (tau)])
					++tau;
				best = tau;
				break;
			}
		}
		if (best < 0)
		{
			// no dip under the threshold: accept the global minimum only if it is clearly periodic
			float minVal = 1e9f;
			for (int tau = tauMin; tau < tauMax; ++tau)
			{
				if (diff[static_cast<size_t> (tau)] < minVal)
				{
					minVal = diff[static_cast<size_t> (tau)];
					best = tau;
				}
			}
			if (minVal > 2.f * threshold)
				return 0.f;
		}

		// parabolic interpolation
		float refined = static_cast<float> (best);
		if (best > 1 && best < tauMax)
		{
			const float a = diff[static_cast<size_t> (best - 1)];
			const float b = diff[static_cast<size_t> (best)];
			const float c = diff[static_cast<size_t> (best + 1)];
			const float den = a - 2.f * b + c;
			if (std::abs (den) > 1e-12f)
				refined += std::clamp (0.5f * (a - c) / den, -0.5f, 0.5f);
		}
		lastConfidence = std::clamp (1.f - diff[static_cast<size_t> (best)], 0.f, 1.f);
		lastF0 = static_cast<float> (rate / refined);
		return lastF0;
	}

	float getLastF0 () const noexcept { return lastF0; }
	float getConfidence () const noexcept { return lastConfidence; }

private:
	struct Biquad
	{
		double b0 {1}, b1 {0}, b2 {0}, a1 {0}, a2 {0};
		double z1 {0}, z2 {0};
		double process (double x) noexcept
		{
			const double y = b0 * x + z1;
			z1 = b1 * x - a1 * y + z2;
			z2 = b2 * x - a2 * y;
			return y;
		}
	};

	static void designLowPass (Biquad& b, double sampleRate, double fc, double q)
	{
		const double w0 = 6.283185307179586 * fc / sampleRate;
		const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
		const double a0 = 1.0 + alpha;
		b.b0 = (1.0 - cw) * 0.5 / a0;
		b.b1 = (1.0 - cw) / a0;
		b.b2 = b.b0;
		b.a1 = -2.0 * cw / a0;
		b.a2 = (1.0 - alpha) / a0;
	}

	Biquad lp[2];
	CircularBuffer history;
	std::vector<float> frame, diff;
	double rate {11025.0};
	int decimation {4};
	int phase {0};
	int writePos {0};
	int tauMin {2}, tauMax {441}, window {441};
	float lastF0 {0.f};
	float lastConfidence {0.f};
};

} // namespace Symbiosis::DSP
