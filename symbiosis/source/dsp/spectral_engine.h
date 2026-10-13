//------------------------------------------------------------------------
// SYMBIOSIS — spectral_engine.h
// Energy-conserving spectral transfer and constructive phase alignment.
//
// Slave  x2 (main input)      : the track that makes room (808, pad, sample)
// Master x1 (aux / sidechain) : the priority track (kick, lead vocal)
//
// Per STFT frame (N = 2048, H = 512, periodic Hann, 75 % overlap):
//   1. X1, X2 = FFT(x . w), zero-padded by the Oversampling factor (1, 2, 4)
//   2. masked energy  E[k] = min(|X2|^2, gamma |X1|^2)      (PULL = gamma)
//      as a ratio rho[k] = E[k] / |X2|^2, smoothed by INERTIA (attack)
//      and REACTION (release); residual |X2|^2 (1 - rho)
//   3. sum_k E[k] is redistributed on the slave's own partials:
//        Exact Peaks    : spectral peaks, weights |X2[m]|^2 / sum |X2[mj]|^2
//        Harmonic Series: 2 f0, 3 f0, 4 f0 (YIN), weights (1/h) / sum (1/i)
//      scaled by eta (HARMONICS). A partial is raised as a whole: every bin
//      of its basin gets the same gain g = sqrt(1 + dE / R), R being the
//      basin's residual energy, so dE lands on the partial without changing
//      the shape of its lobe. A harmonic missing from the spectrum (pure-sine
//      808) is synthesised as a Hann lobe phase-locked to the fundamental.
//      With eta = 1 the frame energy is conserved:
//        sum |Y|^2 = sum |X2|^2 (1 - rho) + sum dE = sum |X2|^2
//   4. phase alignment (PHASE LINK): phi2 += beta . wrap(phi1 - phi2) on the
//      conflict band (bins where master and slave are within 20-40 dB of
//      each other), applied by rigid blocks on each peak's basin (spectral
//      phase locking) so a partial's lobe rotates as one piece, by the same
//      angle on both channels (stereo image kept). The rotation is a per-bin
//      state that moves toward that target by at most 30 degrees per hop:
//      when wrap() jumps from +pi to -pi (detuned master, beta < 1) the
//      overlapping frames never disagree by more than that, so the
//      overlap-add cannot cancel itself (no tremolo, no crackle)
//   5. TRANS-SHIELD: an onset in the newest hop of the frame (N samples
//      before it is heard) lowers rho and beta on the 4 frames that contain
//      it, so the STFT does not smear the attack
//   6. iFFT, synthesis window w / 1.5 (sum of w^2 over 4 hops = 1.5), OLA
//
// Latency: exactly N samples. With PULL = 0 and PHASE LINK off the output
// is the input delayed by N (perfect reconstruction).
//
// prepare() allocates everything. process() allocates nothing and takes no lock.
//------------------------------------------------------------------------
#pragma once

#include "fft_engine.h"
#include "ring_buffer.h"
#include "yin_pitch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace Symbiosis::DSP {

enum class TargetMode : int
{
	ExactPeaks = 0,
	HarmonicSeries = 1
};

struct EngineParams
{
	float pull {0.68f};          // gamma, 0..1
	float harmonics {0.42f};     // eta, 0..1
	float inertiaMs {35.f};      // attack of the transfer, 1..150 ms
	float reactionMs {80.f};     // release of the transfer, 5..500 ms
	float tracking {0.75f};      // adherence to the real partials, 0..1
	float transShield {0.20f};   // transient protection, 0..1
	bool phaseLink {true};
	bool deltaSolo {false};
	bool bypass {false};
	TargetMode targetMode {TargetMode::ExactPeaks};
	int oversampling {4};        // spectral oversampling (zero-padding): 1, 2 or 4
};

inline constexpr int kHudBands = 96;
inline constexpr int kHudWavePoints = 128;

/** One snapshot for the Orbit HUD. Plain data: copied through the data-exchange queue. */
struct HudFrame
{
	uint32_t serial {0};
	float sampleRate {0.f};
	float phaseTransfer {0.5f}; // (1 + energy-weighted cos(phi1 - phi2_out)) / 2 on the conflict band
	float energyMoved {0.f};    // share of the slave energy extracted on this frame
	float shield {0.f};         // transient-shield protection, 0..1
	float f0Hz {0.f};           // fundamental (Harmonic Series mode), 0 if none
	float slaveOutDb {-120.f};  // RMS of the output over the last hop, dBFS
	float masterDb {-120.f};    // RMS of the sidechain over the last hop, dBFS
	float master[kHudBands] {};    // band level, 0..1 = -90..0 dBFS
	float slaveIn[kHudBands] {};
	float slaveOut[kHudBands] {};
	float phaseDelta[kHudBands] {}; // angle(X1 . conj Y) per band, radians
	float slaveWave[kHudWavePoints] {};  // output waveform, last N samples (peak per bucket)
	float masterWave[kHudWavePoints] {}; // sidechain waveform, last N samples
};

/** Energy accounting of the last frame (one-sided spectrum, all channels). */
struct FrameStats
{
	double energyIn {0.0};
	double energyOut {0.0};
	double extracted {0.0};
	double budgetBase {0.0}; // extracted energy smoothed over a few hops: injected = eta x budgetBase
	double injected {0.0};
	int numPeaks {0};
	int numTargets {0};
	bool harmonicFallback {false};
};

//------------------------------------------------------------------------
class SpectralEngine
{
public:
	static constexpr int kFFTSize = 2048;
	static constexpr int kHopSize = 512;
	static constexpr int kMaxChannels = 2;
	static constexpr int kMaxOversampling = 4;
	static constexpr int kMaxFFTSize = kFFTSize * kMaxOversampling;
	static constexpr int kMaxBins = kMaxFFTSize / 2 + 1;
	static constexpr int kNumHarmonics = 3; // 2 f0, 3 f0, 4 f0
	// Most a real partial is raised (both modes): boosting a weak partial further mostly raises
	// the noise of its basin (a rubbery sound). Energy beyond that is not redistributed in
	// Exact Peaks; Harmonic Series synthesises a clean lobe instead.
	static constexpr float kMaxPartialGainDb = 6.f;
	static constexpr double kExactMaxOctaves = 3.0; // Exact Peaks: targets up to 3 octaves above the extraction
	static constexpr float kMaxPhaseStep = 0.5235988f; // PHASE LINK slew, rad per hop (30 degrees)
	// A rotation that changes by d rad per hop shifts the partial by d / (2 pi H / fs) Hz: the
	// slew is also limited so that no partial is detuned by more than 2 % (a sixth of a tone)
	static constexpr double kMaxPhaseDetune = 0.02;
	static constexpr double kPhaseLinkFullHz = 200.0; // PHASE LINK band (see phaseBandWeight)
	static constexpr double kPhaseLinkMaxHz = 400.0;
	static constexpr int kF0HoldHops = 10;          // keep the last f0 through short YIN dropouts
	static constexpr double kSynthSmoothing = 0.5;  // per-hop smoothing of the synthesised harmonics
	static constexpr double kBudgetSmoothing = 0.3; // per-hop smoothing of the energy to redistribute
	static constexpr double kBudgetHeadroom = 1.5;  // smoothed budget <= +1.8 dB over this frame's

	static constexpr int getLatencySamples () noexcept { return kFFTSize; }

	/** Allocates every buffer. Call from setupProcessing / setActive, never from process. */
	void prepare (double newSampleRate)
	{
		sampleRate = newSampleRate;
		for (int i = 0; i < 3; ++i)
			ffts[i].prepare (kFFTSize << i);

		window.assign (kFFTSize, 0.f);
		synthesisWindow.assign (kFFTSize, 0.f);
		const double twoPi = 6.283185307179586476925286766559;
		for (int j = 0; j < kFFTSize; ++j)
		{
			const double w = 0.5 - 0.5 * std::cos (twoPi * j / kFFTSize); // periodic Hann (COLA)
			window[static_cast<size_t> (j)] = static_cast<float> (w);
			synthesisWindow[static_cast<size_t> (j)] = static_cast<float> (w / 1.5);
		}
		windowProduct.assign (kFFTSize, 0.f); // analysis x synthesis: the OLA of an untouched frame
		for (int j = 0; j < kFFTSize; ++j)
			windowProduct[static_cast<size_t> (j)] = window[static_cast<size_t> (j)] * synthesisWindow[static_cast<size_t> (j)];

		for (int ch = 0; ch < kMaxChannels; ++ch)
		{
			slaveHist[ch].prepare (kFFTSize);
			masterHist[ch].prepare (kFFTSize);
			ola[ch].prepare (kFFTSize);
			outHist[ch].prepare (kFFTSize);
			x2Re[ch].assign (kMaxBins, 0.f);
			x2Im[ch].assign (kMaxBins, 0.f);
			x1Re[ch].assign (kMaxBins, 0.f);
			x1Im[ch].assign (kMaxBins, 0.f);
		}
		thetaState.assign (kMaxBins, 0.f);
		binGain.assign (kMaxBins, 1.f);
		binCos.assign (kMaxBins, 1.f);
		binSin.assign (kMaxBins, 0.f);
		yRe.assign (kMaxBins, 0.f);
		yIm.assign (kMaxBins, 0.f);
		frameBuf.assign (kMaxFFTSize, 0.f);
		timeBuf.assign (kMaxFFTSize, 0.f);
		p1.assign (kMaxBins, 0.f);
		p2.assign (kMaxBins, 0.f);
		pOut.assign (kMaxBins, 0.f);
		crossRe.assign (kMaxBins, 0.f);
		crossIm.assign (kMaxBins, 0.f);
		rhoSmooth.assign (kMaxBins, 0.f);
		conflictSmooth.assign (kMaxBins, 0.f);
		rho.assign (kMaxBins, 0.f);
		beta.assign (kMaxBins, 0.f);
		basinOf.assign (kMaxBins, -1);
		peaks.assign (kMaxBins / 2 + 1, 0);
		peakLo.assign (kMaxBins / 2 + 1, 0);
		peakHi.assign (kMaxBins / 2 + 1, 0);
		peakResidual.assign (kMaxBins / 2 + 1, 0.f);
		peakAdd.assign (kMaxBins / 2 + 1, 0.f);
		peakGain.assign (kMaxBins / 2 + 1, 1.f);
		peakTheta.assign (kMaxBins / 2 + 1, 0.f);
		bypassRamp.assign (kHopSize, 0.f);
		deltaRamp.assign (kHopSize, 0.f);
		remapScratch.assign (kMaxBins, 0.f);

		// HUD bands: log-spaced 20 Hz .. 20 kHz (or Nyquist)
		for (int o = 0; o < 3; ++o)
		{
			const int m = kFFTSize << o;
			const int numBins = m / 2 + 1;
			const double fTop = std::min (20000.0, 0.5 * sampleRate);
			for (int b = 0; b < kHudBands; ++b)
			{
				const double fLo = 20.0 * std::pow (fTop / 20.0, static_cast<double> (b) / kHudBands);
				const double fHi = 20.0 * std::pow (fTop / 20.0, static_cast<double> (b + 1) / kHudBands);
				int lo = static_cast<int> (std::floor (fLo * m / sampleRate + 0.5));
				int hi = static_cast<int> (std::floor (fHi * m / sampleRate + 0.5));
				lo = std::clamp (lo, 1, numBins - 2);
				hi = std::clamp (hi, lo, numBins - 2);
				bandLo[o][static_cast<size_t> (b)] = lo;
				bandHi[o][static_cast<size_t> (b)] = hi;
			}
		}

		yin.prepare (sampleRate);
		prepared = true;
		reset ();
	}

	/** Clears the signal history. Call from setActive (true). */
	void reset () noexcept
	{
		for (int ch = 0; ch < kMaxChannels; ++ch)
		{
			slaveHist[ch].clear ();
			masterHist[ch].clear ();
			ola[ch].clear ();
			outHist[ch].clear ();
		}
		std::fill (rhoSmooth.begin (), rhoSmooth.end (), 0.f);
		std::fill (conflictSmooth.begin (), conflictSmooth.end (), 0.f);
		std::fill (thetaState.begin (), thetaState.end (), 0.f);
		pos = 0;
		hopCounter = 0;
		shieldEnv = 0.f;
		shieldHold = 0;
		bypassMix = params.bypass ? 1.f : 0.f;
		deltaMix = params.deltaSolo ? 1.f : 0.f;
		f0Smooth = 0.0;
		f0Hold = 0;
		budgetState = 0.0;
		budgetStateCh.fill (0.0);
		synthF0Hz = 0.0;
		synthPhase.fill (0.0);
		for (auto& c : synthScale)
			c.fill (0.0);
		yin.reset ();
		hud = HudFrame {};
		stats = FrameStats {};
	}

	/** Same thread as process(). Takes effect at the next frame (bypass: next sample). */
	void setParams (const EngineParams& newParams) noexcept { params = newParams; }
	const EngineParams& getParams () const noexcept { return params; }

	/**
	 * slave: numChannels input channels (1 or 2), out: numChannels output channels.
	 * master: numMasterChannels (0 = no sidechain). In-place (out == slave) is allowed.
	 */
	void process (const float* const* slave, const float* const* master, int numMasterChannels,
	              float* const* out, int numChannels, int numSamples) noexcept
	{
		if (!prepared)
			return;
		const int nCh = std::clamp (numChannels, 0, kMaxChannels);
		const int nMaster = master ? std::clamp (numMasterChannels, 0, kMaxChannels) : 0;
		const float rampStep = static_cast<float> (1.0 / (0.02 * sampleRate)); // 20 ms
		const float bypassTarget = params.bypass ? 1.f : 0.f;
		const float deltaTarget = params.deltaSolo ? 1.f : 0.f;
		auto ramp = [rampStep] (float& v, float target) {
			v = v < target ? std::min (target, v + rampStep) : std::max (target, v - rampStep);
			return v;
		};

		int done = 0;
		while (done < numSamples)
		{
			const int n = std::min (numSamples - done, kHopSize - hopCounter);

			// master history first: out may alias the slave input
			for (int ch = 0; ch < kMaxChannels; ++ch)
			{
				CircularBuffer& h = masterHist[ch];
				if (ch < nMaster)
				{
					const float* m = master[ch] + done;
					for (int i = 0; i < n; ++i)
						h[pos + i] = m[i];
				}
				else
				{
					for (int i = 0; i < n; ++i)
						h[pos + i] = 0.f;
				}
			}

			// YIN runs on the mono sum of the slave
			if (nCh > 0)
			{
				const float norm = 1.f / static_cast<float> (nCh);
				for (int i = 0; i < n; ++i)
				{
					float mono = 0.f;
					for (int ch = 0; ch < nCh; ++ch)
						mono += slave[ch][done + i];
					yin.push (mono * norm);
				}
			}

			for (int i = 0; i < n; ++i)
			{
				bypassRamp[static_cast<size_t> (i)] = ramp (bypassMix, bypassTarget);
				deltaRamp[static_cast<size_t> (i)] = ramp (deltaMix, deltaTarget);
			}

			for (int ch = 0; ch < nCh; ++ch)
			{
				const float* in = slave[ch] + done;
				float* o = out[ch] + done;
				CircularBuffer& h = slaveHist[ch];
				CircularBuffer& acc = ola[ch];
				for (int i = 0; i < n; ++i)
				{
					const int idx = pos + i;
					const float dry = h[idx]; // input delayed by exactly N samples
					h[idx] = in[i];
					const float wet = acc[idx];
					acc[idx] = 0.f;
					const float y = wet - dry * deltaRamp[static_cast<size_t> (i)]; // Delta Solo: wet - dry
					o[i] = y + (dry - y) * bypassRamp[static_cast<size_t> (i)];
					outHist[ch][idx] = o[i];
				}
			}

			pos = (pos + n) & (kFFTSize - 1);
			hopCounter += n;
			done += n;
			if (hopCounter == kHopSize)
			{
				hopCounter = 0;
				processFrame (nCh, nMaster);
			}
		}
	}

	const HudFrame& getHudFrame () const noexcept { return hud; }
	const FrameStats& getLastFrameStats () const noexcept { return stats; }
	float getShieldEnvelope () const noexcept { return shieldEnv; }

private:
	//------------------------------------------------------------------------
	// Fourier transform of the periodic Hann window, zero-padded to M bins,
	// evaluated at a fractional bin offset delta: W(delta) = sum_n w[n] e^{-j 2pi delta n / M}
	std::complex<double> hannKernel (double delta, int m) const noexcept
	{
		constexpr double pi = 3.14159265358979323846;
		const double n = kFFTSize;
		auto dirichlet = [&] (double d) -> std::complex<double> {
			const double s = std::sin (pi * d / m);
			if (std::abs (s) < 1e-12)
				return {n, 0.0};
			// signed magnitude: std::polar requires rho >= 0 (libc++ returns NaN otherwise)
			const double magnitude = std::sin (pi * d * n / m) / s;
			const double phase = -pi * d * (n - 1.0) / m;
			return {magnitude * std::cos (phase), magnitude * std::sin (phase)};
		};
		const double shift = static_cast<double> (m) / n;
		return 0.5 * dirichlet (delta) - 0.25 * dirichlet (delta - shift) - 0.25 * dirichlet (delta + shift);
	}

	/**
	 * PHASE LINK acts where two waves really add up into one: the bass. Full below 200 Hz,
	 * raised-cosine taper to 0 at 400 Hz. Rotating the phases of treble partials brings
	 * nothing audible and disperses transients (one-sample spikes on the 808 at each kick).
	 */
	float phaseBandWeight (int bin, int m) const noexcept
	{
		const double f = bin * sampleRate / m;
		if (f <= kPhaseLinkFullHz)
			return 1.f;
		if (f >= kPhaseLinkMaxHz)
			return 0.f;
		const double x = (f - kPhaseLinkFullHz) / (kPhaseLinkMaxHz - kPhaseLinkFullHz);
		return static_cast<float> (0.5 + 0.5 * std::cos (3.14159265358979323846 * x));
	}

	/** Re-samples a per-bin state from one oversampling resolution to another (same frequencies). */
	void remapBins (std::vector<float>& v, int fromIdx, int toIdx) noexcept
	{
		const int fromBins = (kFFTSize << fromIdx) / 2 + 1;
		const int toBins = (kFFTSize << toIdx) / 2 + 1;
		std::copy (v.begin (), v.begin () + fromBins, remapScratch.begin ());
		for (int k = 0; k < toBins; ++k)
		{
			const int src = std::min (fromBins - 1, (k << fromIdx) >> toIdx);
			v[static_cast<size_t> (k)] = remapScratch[static_cast<size_t> (src)];
		}
	}

	//------------------------------------------------------------------------
	/** Levels of the last hop and waveforms of the last N samples, for the HUD meters and scopes. */
	void captureTimeDomain (int nCh, int nMaster) noexcept
	{
		auto mono = [&] (const CircularBuffer* hist, int count, int j) {
			float v = 0.f;
			for (int ch = 0; ch < count; ++ch)
				v += hist[ch][pos + j];
			return count > 0 ? v / static_cast<float> (count) : 0.f;
		};
		auto levelDb = [&] (const CircularBuffer* hist, int count) {
			double e = 0.0;
			for (int ch = 0; ch < count; ++ch)
				for (int j = kFFTSize - kHopSize; j < kFFTSize; ++j)
					e += static_cast<double> (hist[ch][pos + j]) * hist[ch][pos + j];
			const double meanSquare = count > 0 ? e / (kHopSize * count) : 0.0;
			return static_cast<float> (10.0 * std::log10 (meanSquare + 1e-12));
		};
		hud.slaveOutDb = levelDb (outHist, nCh);
		hud.masterDb = levelDb (masterHist, nMaster);

		constexpr int bucket = kFFTSize / kHudWavePoints;
		for (int b = 0; b < kHudWavePoints; ++b)
		{
			float so = 0.f, ma = 0.f;
			for (int j = b * bucket; j < (b + 1) * bucket; ++j)
			{
				const float vs = mono (outHist, nCh, j);
				const float vm = mono (masterHist, nMaster, j);
				if (std::abs (vs) > std::abs (so))
					so = vs;
				if (std::abs (vm) > std::abs (ma))
					ma = vm;
			}
			hud.slaveWave[b] = so;
			hud.masterWave[b] = ma;
		}
	}

	//------------------------------------------------------------------------
	void processFrame (int nCh, int nMaster) noexcept
	{
		const int os = params.oversampling >= 4 ? 4 : (params.oversampling >= 2 ? 2 : 1);
		const int osIdx = os == 4 ? 2 : (os == 2 ? 1 : 0);
		if (osIdx != currentOsIndex)
		{
			// keep the smoothed transfer and the phase rotations across the change of
			// resolution (resetting them would release the whole transfer at once: a pump)
			if (currentOsIndex >= 0)
			{
				remapBins (rhoSmooth, currentOsIndex, osIdx);
				remapBins (conflictSmooth, currentOsIndex, osIdx);
				remapBins (thetaState, currentOsIndex, osIdx);
				const double energyRatio = static_cast<double> (1 << osIdx) / static_cast<double> (1 << currentOsIndex);
				budgetState *= energyRatio;
				for (auto& b : budgetStateCh)
					b *= energyRatio;
			}
			currentOsIndex = osIdx;
		}
		FftEngine& fft = ffts[osIdx];
		const int m = fft.getSize ();
		const int numBins = m / 2 + 1;
		const int r = m / kFFTSize;
		const bool masterConnected = nMaster > 0;
		stats = FrameStats {};

		if (nCh == 0)
			return;

		//--- 1. analysis -----------------------------------------------------
		const float fullScaleBin = 0.25f * kFFTSize; // |X| of a 0 dBFS sine on its bin (Hann)
		const float powerFloor = fullScaleBin * fullScaleBin * 1e-12f * static_cast<float> (nCh); // -120 dBFS
		double eNew = 0.0, eOld = 0.0;
		std::fill (frameBuf.begin () + kFFTSize, frameBuf.begin () + m, 0.f);

		// A silent sidechain frame (every bin far below the -120 dBFS floor, from Parseval:
		// no bin exceeds m x the windowed energy) is not analysed: between two kicks, or with
		// nothing on the sidechain, that saves one FFT per channel.
		bool masterActive = false;
		if (masterConnected)
		{
			double e = 0.0;
			for (int c = 0; c < nMaster; ++c)
				for (int j = 0; j < kFFTSize; ++j)
				{
					const double v = masterHist[c][pos + j] * window[static_cast<size_t> (j)];
					e += v * v;
				}
			masterActive = e * m >= 1e-3 * powerFloor;
		}
		const bool hasMaster = masterActive; // this frame
		for (int ch = 0; ch < nCh; ++ch)
		{
			const CircularBuffer& h = slaveHist[ch];
			for (int j = 0; j < kFFTSize; ++j)
			{
				const float s = h[pos + j];
				if (j >= kFFTSize - kHopSize)
					eNew += static_cast<double> (s) * s;
				else
					eOld += static_cast<double> (s) * s;
				frameBuf[static_cast<size_t> (j)] = s * window[static_cast<size_t> (j)];
			}
			fft.forward (frameBuf.data (), x2Re[ch].data (), x2Im[ch].data ());

			if (hasMaster)
			{
				if (nCh == 1 && nMaster > 1)
				{
					// mono slave, stereo sidechain: analyse the master's mono downmix
					for (int j = 0; j < kFFTSize; ++j)
						frameBuf[static_cast<size_t> (j)] =
						    0.5f * (masterHist[0][pos + j] + masterHist[1][pos + j]) * window[static_cast<size_t> (j)];
				}
				else if (ch > 0 && nMaster == 1)
				{
					// mono sidechain: same spectrum for both slave channels
					std::copy_n (x1Re[0].begin (), numBins, x1Re[ch].begin ());
					std::copy_n (x1Im[0].begin (), numBins, x1Im[ch].begin ());
					continue;
				}
				else
				{
					const CircularBuffer& hm = masterHist[std::min (ch, nMaster - 1)];
					for (int j = 0; j < kFFTSize; ++j)
						frameBuf[static_cast<size_t> (j)] = hm[pos + j] * window[static_cast<size_t> (j)];
				}
				fft.forward (frameBuf.data (), x1Re[ch].data (), x1Im[ch].data ());
			}
		}

		//--- 2. stereo-linked powers -----------------------------------------
		float maxP2 = 0.f;
		for (int k = 0; k < numBins; ++k)
		{
			float s2 = 0.f, s1 = 0.f;
			for (int ch = 0; ch < nCh; ++ch)
			{
				s2 += x2Re[ch][k] * x2Re[ch][k] + x2Im[ch][k] * x2Im[ch][k];
				if (hasMaster)
					s1 += x1Re[ch][k] * x1Re[ch][k] + x1Im[ch][k] * x1Im[ch][k];
			}
			p2[k] = s2;
			p1[k] = s1;
			maxP2 = std::max (maxP2, s2);
			pOut[k] = 0.f;
			crossRe[k] = 0.f;
			crossIm[k] = 0.f;
		}

		//--- 3. transient shield (onset in the newest hop of the frame) -------
		{
			const double newPower = eNew / kHopSize + 1e-12;
			const double oldPower = eOld / (kFFTSize - kHopSize) + 1e-12;
			const float ratioDb = static_cast<float> (10.0 * std::log10 (newPower / oldPower));
			const float onset = std::clamp ((ratioDb - 4.f) / 8.f, 0.f, 1.f);
			if (onset >= shieldEnv && onset > 0.f)
			{
				shieldEnv = onset;
				shieldHold = kFFTSize / kHopSize - 1; // the 4 frames that contain the onset
			}
			else if (shieldHold > 0)
				--shieldHold;
			else
				shieldEnv *= 0.5f;
		}
		const float protect = 1.f - std::clamp (params.transShield, 0.f, 1.f) * shieldEnv;

		//--- 4. masked energy, conflict band, smoothing -----------------------
		const double framePeriod = kHopSize / sampleRate;
		const float aAttack = static_cast<float> (std::exp (-framePeriod / (std::max (params.inertiaMs, 0.01f) * 1e-3)));
		const float aRelease = static_cast<float> (std::exp (-framePeriod / (std::max (params.reactionMs, 0.01f) * 1e-3)));
		const float gamma = std::clamp (params.pull, 0.f, 1.f);
		const float eta = std::clamp (params.harmonics, 0.f, 1.f);
		const bool phaseOn = params.phaseLink && hasMaster;

		rho[0] = rho[numBins - 1] = 0.f;
		beta[0] = beta[numBins - 1] = 0.f;
		for (int k = 1; k < numBins - 1; ++k)
		{
			const float s2 = p2[k], s1 = p1[k];
			const float rhoRaw = s2 > powerFloor ? std::min (1.f, gamma * s1 / s2) : 0.f;
			float& rs = rhoSmooth[k];
			rs = rhoRaw + (rhoRaw > rs ? aAttack : aRelease) * (rs - rhoRaw);

			float conflictRaw = 0.f;
			if (s1 > powerFloor && s2 > powerFloor)
			{
				// both tracks within 20 dB: full conflict; 40 dB apart: none
				const float ratioDb = 10.f * std::log10 (std::min (s1, s2) / std::max (s1, s2));
				conflictRaw = std::clamp ((ratioDb + 40.f) / 20.f, 0.f, 1.f);
			}
			float& cs = conflictSmooth[k];
			cs = conflictRaw + (conflictRaw > cs ? aAttack : aRelease) * (cs - conflictRaw);

			rho[k] = rs * protect;
			beta[k] = phaseOn ? cs * protect * phaseBandWeight (k, m) : 0.f;
		}

		//--- 5. peak picking and basins (spectral phase locking regions) ------
		const float tracking = std::clamp (params.tracking, 0.f, 1.f);
		const float threshold = maxP2 * std::pow (10.f, -(30.f + 50.f * tracking) / 10.f);
		const int halfWidth = r * (1 + static_cast<int> (std::lround (5.f * (1.f - tracking))));
		int numPeaks = 0;
		for (int k = 2; k < numBins - 2; ++k)
		{
			const float v = p2[k];
			if (v > threshold && v > powerFloor && v > p2[k - 1] && v >= p2[k + 1])
				peaks[static_cast<size_t> (numPeaks++)] = k;
		}
		std::fill (basinOf.begin (), basinOf.begin () + numBins, -1);
		for (int i = 0; i < numPeaks; ++i)
		{
			const int pk = peaks[static_cast<size_t> (i)];
			int lo = 1, hi = numBins - 2;
			if (i > 0)
			{
				const int prev = peaks[static_cast<size_t> (i - 1)];
				int valley = prev + 1;
				for (int k = prev + 1; k < pk; ++k)
					if (p2[k] < p2[valley])
						valley = k;
				lo = valley;
			}
			if (i + 1 < numPeaks)
			{
				const int next = peaks[static_cast<size_t> (i + 1)];
				int valley = pk + 1;
				for (int k = pk + 1; k < next; ++k)
					if (p2[k] < p2[valley])
						valley = k;
				hi = valley - 1;
			}
			lo = std::max (lo, pk - halfWidth);
			hi = std::min (hi, pk + halfWidth);
			peakLo[static_cast<size_t> (i)] = lo;
			peakHi[static_cast<size_t> (i)] = hi;
			float residual = 0.f;
			for (int k = lo; k <= hi; ++k)
			{
				basinOf[k] = i;
				residual += (1.f - rho[k]) * p2[k];
			}
			peakResidual[static_cast<size_t> (i)] = residual;
			peakAdd[static_cast<size_t> (i)] = 0.f;
			peakGain[static_cast<size_t> (i)] = 1.f;
		}
		stats.numPeaks = numPeaks;

		//--- 6. energy budget ---------------------------------------------------
		double extracted = 0.0;
		std::array<double, kMaxChannels> extractedCh {};
		for (int k = 1; k < numBins - 1; ++k)
		{
			if (rho[k] <= 0.f)
				continue;
			extracted += static_cast<double> (rho[k]) * p2[k];
			for (int ch = 0; ch < nCh; ++ch)
				extractedCh[static_cast<size_t> (ch)] +=
				    static_cast<double> (rho[k]) * (x2Re[ch][k] * x2Re[ch][k] + x2Im[ch][k] * x2Im[ch][k]);
		}
		stats.extracted = extracted;
		// The power of a low partial measured on one frame depends on its phase in the window
		// (leakage with its negative-frequency image: a 33 Hz 808 varies by about 1 dB from
		// frame to frame). Smoothed over ~3 hops, the budget no longer amplitude-modulates the
		// partials it raises (a rough, rubbery sound around the harmonics).
		// Never more than +1.8 dB above what this frame really extracted (the frame-to-frame
		// fluctuation is about 1 dB): when the slave stops (end of a note), the budget drops
		// with it instead of injecting the energy of the previous frames into a quiet signal.
		auto smoothBudget = [] (double& state, double now) {
			state += kBudgetSmoothing * (now - state);
			state = std::min (state, kBudgetHeadroom * now);
		};
		smoothBudget (budgetState, extracted);
		for (int ch = 0; ch < kMaxChannels; ++ch)
			smoothBudget (budgetStateCh[static_cast<size_t> (ch)], extractedCh[static_cast<size_t> (ch)]);
		stats.budgetBase = budgetState;
		const double budget = eta * budgetState;
		const float maxGainSq = std::pow (10.f, kMaxPartialGainDb / 10.f);

		//--- 7. redistribution targets ------------------------------------------
		struct SynthTarget
		{
			double bin;
			int harmonic;
			double weight;
		};
		std::array<SynthTarget, kNumHarmonics> synth {};
		int numSynth = 0;
		double b0 = 0.0;
		bool harmonicDone = false;
		hud.f0Hz = 0.f;

		if (params.targetMode == TargetMode::HarmonicSeries)
		{
			// f0: YIN, smoothed (a jump of more than 6 % is a new note and is taken at once) and
			// held through short dropouts, so the targets do not jitter or switch every frame
			const float yinF0 = yin.analyze ();
			if (yinF0 > 0.f)
			{
				if (f0Smooth <= 0.0 || std::abs (yinF0 / f0Smooth - 1.0) > 0.06)
					f0Smooth = yinF0;
				else
					f0Smooth += 0.35 * (yinF0 - f0Smooth);
				f0Hold = kF0HoldHops;
			}
			else if (f0Hold > 0)
				--f0Hold;
			else
				f0Smooth = 0.0;
			const double f0 = f0Hold > 0 ? f0Smooth : 0.0;
			if (f0 > 0.0 && budget > 0.0)
			{
				b0 = f0 * m / sampleRate;
				// Refine on the spectral peak closest to the YIN estimate, only when the
				// fundamental's lobe is resolved (clear of DC): below that (a 30-80 Hz 808 in a
				// 2048-sample frame), leakage makes the peak wander and YIN is the better estimate.
				const int c = static_cast<int> (std::lround (b0));
				int best = -1;
				if (b0 >= 4.0 * r)
					for (int k = std::max (2, c - r); k <= std::min (numBins - 3, c + r); ++k)
						if (best < 0 || p2[k] > p2[best])
							best = k;
				if (best > 0 && p2[best] > p2[best - 1] && p2[best] >= p2[best + 1] && p2[best] > powerFloor)
				{
					const double a = std::log (p2[best - 1] + 1e-30), b = std::log (p2[best] + 1e-30),
					             g = std::log (p2[best + 1] + 1e-30);
					const double den = a - 2.0 * b + g;
					const double frac = std::abs (den) > 1e-12 ? std::clamp (0.5 * (a - g) / den, -0.5, 0.5) : 0.0;
					if (std::abs (best + frac - b0) <= r)
						b0 = best + frac;
				}
				hud.f0Hz = static_cast<float> (b0 * sampleRate / m);

				// A target is kept when the master would not mask it again once the energy is
				// there: gamma |X1|^2 below the predicted slave power on the target bin
				// (residual + the share of dE that a Hann lobe puts on its centre bin, 2 / 3r).
				double candidateSum = 0.0;
				for (int h = 2; h <= kNumHarmonics + 1; ++h)
					if (h * b0 < numBins - 3.0 * r - 2.0)
						candidateSum += 1.0 / h;
				double weightSum = 0.0;
				std::array<double, kNumHarmonics> weights {};
				for (int h = 2; h <= kNumHarmonics + 1 && candidateSum > 0.0; ++h)
				{
					const double bh = h * b0;
					const int kh = static_cast<int> (std::lround (bh));
					if (bh >= numBins - 3.0 * r - 2.0 || kh <= 1)
						continue;
					const double dE = budget * (1.0 / h) / candidateSum;
					const double predicted = p2[kh] * (1.0 - rho[kh]) + dE * 2.0 / (3.0 * r);
					if (gamma * p1[kh] < predicted)
					{
						weights[static_cast<size_t> (h - 2)] = 1.0 / h;
						weightSum += 1.0 / h;
					}
				}
				// with a valid f0 the frame stays in Harmonic Series even when every target is
				// masked (nothing is injected then): no switching to Exact Peaks and back
				harmonicDone = true;
				if (weightSum > 0.0)
				{
					for (int h = 2; h <= kNumHarmonics + 1; ++h)
					{
						const double w = weights[static_cast<size_t> (h - 2)] / weightSum;
						if (w <= 0.0)
							continue;
						const double bh = h * b0;
						const double dE = budget * w;
						// a real partial near h f0 that can take dE without hitting the gain cap?
						int nearest = -1;
						for (int i = 0; i < numPeaks; ++i)
						{
							const int pk = peaks[static_cast<size_t> (i)];
							if (std::abs (pk - bh) <= r && (nearest < 0 || std::abs (pk - bh) < std::abs (peaks[static_cast<size_t> (nearest)] - bh)))
								nearest = i;
						}
						if (nearest >= 0 && peakResidual[static_cast<size_t> (nearest)] * (maxGainSq - 1.f) >= dE)
							peakAdd[static_cast<size_t> (nearest)] += static_cast<float> (dE);
						else
							synth[static_cast<size_t> (numSynth++)] = {bh, h, w};
						++stats.numTargets;
					}
				}
			}
			stats.harmonicFallback = !harmonicDone;
		}

		if (!harmonicDone && budget > 0.0)
		{
			// Exact Peaks (or fallback): weights proportional to the partial's own energy, among
			// the unmasked partials at most kExactMaxOctaves above where the energy was taken
			// (energy-weighted mean bin of the extraction). Without that limit, a kick masking
			// the low partials of a distorted 808 sent all its sub energy to the weak treble
			// partials of the distortion (+20 dB of fizz at 1-12 kHz).
			double centroidNum = 0.0, centroidDen = 0.0;
			for (int k = 1; k < numBins - 1; ++k)
			{
				const double e = static_cast<double> (rho[k]) * p2[k];
				centroidNum += e * k;
				centroidDen += e;
			}
			const double topBin = centroidDen > 0.0 ? (centroidNum / centroidDen) * std::exp2 (kExactMaxOctaves) : 0.0;
			auto eligible = [&] (int i) {
				const int pk = peaks[static_cast<size_t> (i)];
				return rho[pk] < 0.5f && peakResidual[static_cast<size_t> (i)] > powerFloor && pk <= topBin;
			};
			double weightSum = 0.0;
			for (int i = 0; i < numPeaks; ++i)
				if (eligible (i))
					weightSum += p2[peaks[static_cast<size_t> (i)]];
			if (weightSum > 0.0)
			{
				for (int i = 0; i < numPeaks; ++i)
				{
					const int pk = peaks[static_cast<size_t> (i)];
					if (eligible (i))
					{
						peakAdd[static_cast<size_t> (i)] = static_cast<float> (budget * p2[pk] / weightSum);
						++stats.numTargets;
					}
				}
			}
		}

		double injected = 0.0;
		for (int i = 0; i < numPeaks; ++i)
		{
			const float add = peakAdd[static_cast<size_t> (i)];
			const float res = peakResidual[static_cast<size_t> (i)];
			if (add > 0.f && res > 0.f)
			{
				const float gSq = std::min (1.f + add / res, maxGainSq);
				peakGain[static_cast<size_t> (i)] = std::sqrt (gSq);
				injected += (gSq - 1.f) * res;
			}
		}

		//--- 8. phase rotation, stereo-linked ------------------------------------
		// One rotation per bin for all channels, from the cross-spectrum summed over the
		// channels (the inter-channel phase, hence the stereo image, is kept). Target
		// measured on each peak bin, slewed from that bin's previous rotation, then shared
		// by the whole basin (rigid block).
		float* state = thetaState.data ();
		{
			const float detunePerBin = static_cast<float> (6.283185307179586 * kMaxPhaseDetune * kHopSize / m);
			auto slew = [detunePerBin] (float current, float target, int bin) {
				const float pi = 3.14159265f;
				const float maxStep = std::min (kMaxPhaseStep, detunePerBin * static_cast<float> (bin));
				float step = target - current;
				step -= 2.f * pi * std::floor ((step + pi) / (2.f * pi)); // shortest arc
				float next = current + std::clamp (step, -maxStep, maxStep);
				if (next > pi)
					next -= 2.f * pi;
				else if (next < -pi)
					next += 2.f * pi;
				return next;
			};
			auto target = [&] (int k) {
				if (beta[k] <= 1e-4f)
					return 0.f;
				float cr = 0.f, ci = 0.f; // sum over channels of X1 . conj X2
				for (int ch = 0; ch < nCh; ++ch)
				{
					const float x1r = x1Re[ch][k], x1i = x1Im[ch][k], x2r = x2Re[ch][k], x2i = x2Im[ch][k];
					cr += x1r * x2r + x1i * x2i;
					ci += x1i * x2r - x1r * x2i;
				}
				return beta[k] * std::atan2 (ci, cr);
			};
			for (int i = 0; i < numPeaks; ++i)
			{
				const int pk = peaks[static_cast<size_t> (i)];
				peakTheta[static_cast<size_t> (i)] = slew (state[pk], target (pk), pk);
			}
			for (int k = 0; k < numBins; ++k)
			{
				const int basin = basinOf[k];
				if (basin >= 0)
					state[k] = peakTheta[static_cast<size_t> (basin)];
				else if (beta[k] > 1e-4f || state[k] != 0.f)
					state[k] = slew (state[k], target (k), k);
			}
		}

		//--- 9. synthesised harmonics: one continuous oscillator per harmonic -----
		// Phase advanced by h * 2 pi f0 H / fs at each hop, so a harmonic is a steady sinusoid
		// whatever the frame-to-frame accuracy of f0. When the fundamental is resolved its
		// measured phase pulls the oscillators (psi_h -> h psi0, phase-locked to the
		// fundamental); a 30-80 Hz 808 is not resolved in a 2048-sample frame and keeps the
		// free-running phase. Amplitudes are smoothed: harmonics fade in and out.
		std::array<double, kNumHarmonics> synthWeight {};
		for (int s = 0; s < numSynth; ++s)
			synthWeight[static_cast<size_t> (synth[static_cast<size_t> (s)].harmonic - 2)] = synth[static_cast<size_t> (s)].weight;
		bool synthSounding = numSynth > 0;
		for (const auto& c : synthScale)
			for (double v : c)
				synthSounding = synthSounding || v > 0.0;
		if (b0 > 0.0)
			synthF0Hz = b0 * sampleRate / m;
		const double synthBin = synthF0Hz * m / sampleRate;
		if (synthSounding && synthF0Hz > 0.0)
		{
			const double twoPi = 6.283185307179586;
			auto wrap = [twoPi] (double x) { return x - twoPi * std::floor ((x + 0.5 * twoPi) / twoPi); };
			const double advance = twoPi * synthF0Hz * kHopSize / sampleRate;
			const bool resolved = b0 > 0.0 && synthBin >= 4.0 * r;
			double psi0 = 0.0;
			if (resolved)
			{
				const int k0 = std::clamp (static_cast<int> (std::lround (synthBin)), 1, numBins - 2);
				double sr0 = 0.0, si0 = 0.0;
				for (int ch = 0; ch < nCh; ++ch)
					sr0 += x2Re[ch][k0], si0 += x2Im[ch][k0];
				psi0 = std::atan2 (si0, sr0) - std::arg (hannKernel (k0 - synthBin, m)) + state[k0];
			}
			for (int hi = 0; hi < kNumHarmonics; ++hi)
			{
				double& ph = synthPhase[static_cast<size_t> (hi)];
				ph = wrap (ph + (hi + 2) * advance);
				if (resolved)
					ph = wrap (ph + 0.25 * wrap ((hi + 2) * psi0 - ph));
			}
		}

		//--- 10. synthesis of the output spectrum, per channel ----------------
		// gain and rotation of each bin, once for all channels; a frame with no change at all
		// (nothing pulled, raised, rotated or synthesised) skips the inverse FFT: its
		// overlap-add is the windowed input itself
		bool neutral = !synthSounding;
		for (int k = 0; k < numBins; ++k)
		{
			const int basin = basinOf[k];
			float gain = std::sqrt (std::max (0.f, 1.f - rho[k]));
			if (basin >= 0)
				gain *= peakGain[static_cast<size_t> (basin)];
			binGain[k] = gain;
			const float theta = state[k];
			binCos[k] = theta != 0.f ? std::cos (theta) : 1.f;
			binSin[k] = theta != 0.f ? std::sin (theta) : 0.f;
			neutral = neutral && std::abs (gain - 1.f) < 1e-6f && theta == 0.f;
		}

		double cosNum = 0.0, cosDen = 0.0;
		for (int ch = 0; ch < nCh; ++ch)
		{
			const float* ar = x2Re[ch].data ();
			const float* ai = x2Im[ch].data ();
			const float* mr = x1Re[ch].data ();
			const float* mi = x1Im[ch].data ();

			if (neutral)
			{
				std::copy_n (ar, numBins, yRe.begin ());
				std::copy_n (ai, numBins, yIm.begin ());
			}
			else
				for (int k = 0; k < numBins; ++k)
				{
					const float re = ar[k] * binGain[k], im = ai[k] * binGain[k];
					yRe[k] = re * binCos[k] - im * binSin[k];
					yIm[k] = re * binSin[k] + im * binCos[k];
				}

			// harmonics absent from the spectrum: Hann lobes of the oscillators above
			if (synthSounding && synthF0Hz > 0.0)
			{
				for (int hi = 0; hi < kNumHarmonics; ++hi)
				{
					double& sc = synthScale[static_cast<size_t> (ch)][static_cast<size_t> (hi)];
					const double bin = (hi + 2) * synthBin;
					const double weight = synthWeight[static_cast<size_t> (hi)];
					if (bin >= numBins - 3.0 * r - 2.0 || (weight <= 0.0 && sc <= 0.0))
					{
						sc = 0.0;
						continue;
					}
					const int lo = std::max (1, static_cast<int> (std::floor (bin)) - 3 * r);
					const int hiBin = std::min (numBins - 2, static_cast<int> (std::ceil (bin)) + 3 * r);
					std::array<std::complex<double>, 8 * kMaxOversampling> lobe;
					double unit = 0.0;
					for (int k = lo; k <= hiBin; ++k)
					{
						lobe[static_cast<size_t> (k - lo)] = hannKernel (k - bin, m);
						unit += std::norm (lobe[static_cast<size_t> (k - lo)]);
					}
					if (unit <= 0.0)
						continue;
					const double dE = eta * weight * budgetStateCh[static_cast<size_t> (ch)];
					const double targetScale = dE > 0.0 ? std::sqrt (dE / unit) : 0.0;
					sc += kSynthSmoothing * (targetScale - sc);
					// bounded by the frame's own (capped) budget, not by this harmonic's target,
					// which toggles with the masking check: a note-off fades the lobe at once,
					// a masked target still fades smoothly
					sc = std::min (sc, std::sqrt (0.5 * eta * budgetStateCh[static_cast<size_t> (ch)] / unit));
					if (targetScale <= 0.0 && sc < 1e-6) // faded out (about -120 dB)
					{
						sc = 0.0;
						continue;
					}
					const std::complex<double> rot = std::polar (sc, synthPhase[static_cast<size_t> (hi)]);
					for (int k = lo; k <= hiBin; ++k)
					{
						const std::complex<double> v = rot * lobe[static_cast<size_t> (k - lo)];
						yRe[k] += static_cast<float> (v.real ());
						yIm[k] += static_cast<float> (v.imag ());
					}
					injected += sc * sc * unit;
				}
			}

			// accounting and HUD measurements
			for (int k = 0; k < numBins; ++k)
			{
				const float yr = yRe[k], yi = yIm[k];
				const float py = yr * yr + yi * yi;
				pOut[k] += py;
				if (hasMaster)
				{
					const float cr = mr[k] * yr + mi[k] * yi; // Re(X1 conj Y)
					const float ci = mi[k] * yr - mr[k] * yi; // Im(X1 conj Y)
					crossRe[k] += cr;
					crossIm[k] += ci;
					if (conflictSmooth[k] > 0.25f)
					{
						cosNum += cr;
						cosDen += std::sqrt ((mr[k] * mr[k] + mi[k] * mi[k]) * py);
					}
				}
			}

			//--- 11. inverse FFT and overlap-add -----------------------------------
			CircularBuffer& acc = ola[ch];
			if (neutral)
			{
				const CircularBuffer& h = slaveHist[ch];
				for (int j = 0; j < kFFTSize; ++j)
					acc[pos + j] += h[pos + j] * windowProduct[static_cast<size_t> (j)];
			}
			else
			{
				fft.inverse (yRe.data (), yIm.data (), timeBuf.data ());
				for (int j = 0; j < kFFTSize; ++j)
					acc[pos + j] += timeBuf[static_cast<size_t> (j)] * synthesisWindow[static_cast<size_t> (j)];
			}
		}

		//--- 12. statistics and HUD ---------------------------------------------
		double eIn = 0.0, eOut = 0.0;
		for (int k = 0; k < numBins; ++k)
		{
			eIn += p2[k];
			eOut += pOut[k];
		}
		stats.energyIn = eIn;
		stats.energyOut = eOut;
		stats.injected = injected;

		hud.serial++;
		hud.sampleRate = static_cast<float> (sampleRate);
		hud.energyMoved = eIn > 0.0 ? static_cast<float> (extracted / eIn) : 0.f;
		hud.phaseTransfer = cosDen > 0.0 ? static_cast<float> (0.5 * (1.0 + cosNum / cosDen)) : 0.5f;
		hud.shield = 1.f - protect;
		captureTimeDomain (nCh, nMaster);
		const float ref = 1.f / (fullScaleBin * fullScaleBin * static_cast<float> (nCh));
		auto level = [] (float power) {
			const float db = 10.f * std::log10 (power + 1e-20f);
			return std::clamp ((db + 90.f) / 90.f, 0.f, 1.f);
		};
		for (int b = 0; b < kHudBands; ++b)
		{
			const int lo = bandLo[osIdx][static_cast<size_t> (b)];
			const int hi = bandHi[osIdx][static_cast<size_t> (b)];
			float m1 = 0.f, m2 = 0.f, mo = 0.f;
			double sr = 0.0, si = 0.0;
			for (int k = lo; k <= hi; ++k)
			{
				m1 = std::max (m1, p1[k]);
				m2 = std::max (m2, p2[k]);
				mo = std::max (mo, pOut[k]);
				sr += crossRe[k];
				si += crossIm[k];
			}
			hud.master[b] = level (m1 * ref);
			hud.slaveIn[b] = level (m2 * ref);
			hud.slaveOut[b] = level (mo * ref);
			hud.phaseDelta[b] = (sr != 0.0 || si != 0.0) ? static_cast<float> (std::atan2 (si, sr)) : 0.f;
		}
	}

	//------------------------------------------------------------------------
	double sampleRate {48000.0};
	bool prepared {false};
	EngineParams params;

	FftEngine ffts[3]; // N, 2N, 4N
	int currentOsIndex {-1};
	std::vector<float> window, synthesisWindow, windowProduct;

	CircularBuffer slaveHist[kMaxChannels];
	CircularBuffer masterHist[kMaxChannels];
	CircularBuffer ola[kMaxChannels];
	CircularBuffer outHist[kMaxChannels];
	int pos {0};
	int hopCounter {0};

	std::vector<float> x2Re[kMaxChannels], x2Im[kMaxChannels];
	std::vector<float> x1Re[kMaxChannels], x1Im[kMaxChannels];
	std::vector<float> yRe, yIm, frameBuf, timeBuf;
	std::vector<float> p1, p2, pOut, crossRe, crossIm;
	std::vector<float> rhoSmooth, conflictSmooth, rho, beta;
	std::vector<int> basinOf, peaks, peakLo, peakHi;
	std::vector<float> peakResidual, peakAdd, peakGain, peakTheta;
	std::vector<float> bypassRamp, deltaRamp, remapScratch;
	std::vector<float> thetaState; // PHASE LINK rotation per bin, shared by the channels (rad)
	std::vector<float> binGain, binCos, binSin; // per-bin change of the current frame
	std::array<std::array<int, kHudBands>, 3> bandLo {}, bandHi {};

	float shieldEnv {0.f};
	int shieldHold {0};
	float bypassMix {0.f};
	float deltaMix {0.f};

	double budgetState {0.0};                           // smoothed extracted energy (all channels)
	std::array<double, kMaxChannels> budgetStateCh {}; // per channel
	double f0Smooth {0.0};  // smoothed YIN f0 (Hz)
	int f0Hold {0};          // hops the last f0 is kept through a dropout
	double synthF0Hz {0.0};  // fundamental of the harmonic oscillators
	std::array<double, kNumHarmonics> synthPhase {};                            // rad, at frame start
	std::array<std::array<double, kNumHarmonics>, kMaxChannels> synthScale {}; // smoothed lobe amplitudes

	YinPitch yin;
	HudFrame hud;
	FrameStats stats;
};

} // namespace Symbiosis::DSP
