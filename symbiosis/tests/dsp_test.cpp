//------------------------------------------------------------------------
// SYMBIOSIS — DSP unit tests (headers only, no VST3 SDK needed)
//------------------------------------------------------------------------
#include "dsp/fft_engine.h"
#include "dsp/ring_buffer.h"
#include "dsp/spectral_engine.h"
#include "dsp/yin_pitch.h"

#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace Symbiosis::DSP;

namespace {

int failures = 0;

void check (bool ok, const char* what, double value = 0.0)
{
	std::printf ("[%s] %s (%g)\n", ok ? " ok " : "FAIL", what, value);
	if (!ok)
		++failures;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;
constexpr int kN = SpectralEngine::kFFTSize;

EngineParams neutralParams ()
{
	EngineParams p;
	p.pull = 0.f;
	p.harmonics = 0.f;
	p.phaseLink = false;
	p.transShield = 0.f;
	return p;
}

/** Runs the engine on whole signals, in blocks of an awkward size. */
void run (SpectralEngine& engine, std::vector<float> slaveL, std::vector<float> slaveR,
          const std::vector<float>* masterL, const std::vector<float>* masterR,
          std::vector<float>& outL, std::vector<float>& outR, int block = 333)
{
	const int total = static_cast<int> (slaveL.size ());
	outL.assign (static_cast<size_t> (total), 0.f);
	outR.assign (static_cast<size_t> (total), 0.f);
	for (int start = 0; start < total; start += block)
	{
		const int n = std::min (block, total - start);
		const float* in[2] = {slaveL.data () + start, slaveR.data () + start};
		float* out[2] = {outL.data () + start, outR.data () + start};
		if (masterL)
		{
			const float* m[2] = {masterL->data () + start, masterR->data () + start};
			engine.process (in, m, 2, out, 2, n);
		}
		else
			engine.process (in, nullptr, 0, out, 2, n);
	}
}

std::vector<float> sine (int length, double freq, double amp, double phase = 0.0)
{
	std::vector<float> v (static_cast<size_t> (length));
	for (int i = 0; i < length; ++i)
		v[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * kPi * freq * i / kRate + phase));
	return v;
}

/** Amplitude of one frequency over [start, start+len) (Goertzel-like projection). */
double toneAmplitude (const std::vector<float>& x, int start, int len, double freq)
{
	double re = 0.0, im = 0.0;
	for (int i = 0; i < len; ++i)
	{
		const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * i / len);
		const double a = 2.0 * kPi * freq * (start + i) / kRate;
		re += w * x[static_cast<size_t> (start + i)] * std::cos (a);
		im += w * x[static_cast<size_t> (start + i)] * std::sin (a);
	}
	return 2.0 * std::sqrt (re * re + im * im) / (0.5 * len);
}

double rms (const std::vector<float>& x, int start, int len)
{
	double s = 0.0;
	for (int i = 0; i < len; ++i)
		s += static_cast<double> (x[static_cast<size_t> (start + i)]) * x[static_cast<size_t> (start + i)];
	return std::sqrt (s / len);
}

//------------------------------------------------------------------------
void testFft ()
{
	std::mt19937 rng (1);
	std::uniform_real_distribution<float> dist (-1.f, 1.f);
	for (int size : {8, 2048, 8192})
	{
		FftEngine fft;
		fft.prepare (size);
		std::vector<float> x (static_cast<size_t> (size)), re (static_cast<size_t> (size / 2 + 1)),
		    im (static_cast<size_t> (size / 2 + 1)), back (static_cast<size_t> (size));
		for (auto& v : x)
			v = dist (rng);
		fft.forward (x.data (), re.data (), im.data ());

		double maxErr = 0.0;
		for (int k : {0, 1, 3, size / 4, size / 2 - 1, size / 2})
		{
			std::complex<double> ref = 0.0;
			for (int n = 0; n < size; ++n)
				ref += static_cast<double> (x[static_cast<size_t> (n)]) * std::polar (1.0, -2.0 * kPi * k * n / size);
			maxErr = std::max (maxErr, std::abs (ref - std::complex<double> (re[static_cast<size_t> (k)], im[static_cast<size_t> (k)])));
		}
		char msg[96];
		std::snprintf (msg, sizeof msg, "FFT %d matches the direct DFT", size);
		check (maxErr < 2e-3 * std::sqrt (size), msg, maxErr);

		fft.inverse (re.data (), im.data (), back.data ());
		double rt = 0.0;
		for (int n = 0; n < size; ++n)
			rt = std::max (rt, static_cast<double> (std::abs (back[static_cast<size_t> (n)] - x[static_cast<size_t> (n)])));
		std::snprintf (msg, sizeof msg, "FFT %d round trip", size);
		check (rt < 1e-5, msg, rt);
	}
}

void testSpscRing ()
{
	SpscRingBuffer<int> ring (64);
	constexpr int count = 200000;
	long long sum = 0;
	bool ordered = true;
	std::thread consumer ([&] {
		int expected = 0, v = 0;
		while (expected < count)
		{
			if (ring.pop (v))
			{
				ordered = ordered && (v == expected);
				sum += v;
				++expected;
			}
		}
	});
	for (int i = 0; i < count;)
		if (ring.push (i))
			++i;
	consumer.join ();
	check (ordered && sum == static_cast<long long> (count) * (count - 1) / 2, "SPSC ring: order and content across threads");
}

void testYin ()
{
	YinPitch yin;
	yin.prepare (kRate);
	for (double f : {41.2, 55.0, 98.0, 220.0})
	{
		yin.reset ();
		auto s = sine (8192, f, 0.5);
		for (float v : s)
			yin.push (v);
		const float est = yin.analyze ();
		char msg[96];
		std::snprintf (msg, sizeof msg, "YIN finds %.1f Hz within 1 %%", f);
		check (std::abs (est - f) / f < 0.01, msg, est);
	}
}

void testPerfectReconstruction ()
{
	for (int os : {1, 2, 4})
	{
		SpectralEngine engine;
		engine.prepare (kRate);
		auto p = neutralParams ();
		p.oversampling = os;
		engine.setParams (p);
		std::mt19937 rng (7);
		std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
		const int len = 20000;
		std::vector<float> l (len), r (len), ol, orr;
		for (int i = 0; i < len; ++i)
		{
			l[static_cast<size_t> (i)] = dist (rng);
			r[static_cast<size_t> (i)] = dist (rng);
		}
		auto ml = sine (len, 60.0, 0.9), mr = sine (len, 60.0, 0.9);
		run (engine, l, r, &ml, &mr, ol, orr);
		double err = 0.0;
		for (int i = kN; i < len; ++i)
			err = std::max (err, static_cast<double> (std::abs (ol[static_cast<size_t> (i)] - l[static_cast<size_t> (i - kN)])));
		char msg[96];
		std::snprintf (msg, sizeof msg, "PULL 0, Phase Link off, x%d: output = input delayed by N", os);
		check (err < 1e-4, msg, err);
	}
}

void testLatency ()
{
	SpectralEngine engine;
	engine.prepare (kRate);
	engine.setParams (neutralParams ());
	const int len = 6000;
	std::vector<float> x (len, 0.f), ol, orr;
	x[100] = 1.f;
	run (engine, x, x, nullptr, nullptr, ol, orr, 64);
	int peak = 0;
	for (int i = 0; i < len; ++i)
		if (std::abs (ol[static_cast<size_t> (i)]) > std::abs (ol[static_cast<size_t> (peak)]))
			peak = i;
	check (peak == 100 + SpectralEngine::getLatencySamples () && std::abs (ol[static_cast<size_t> (peak)] - 1.f) < 1e-4,
	       "impulse comes out exactly getLatencySamples() = 2048 later", peak);
}

/** 808-like slave (60 Hz + partials) against a 60 Hz master. */
struct Scene
{
	std::vector<float> slave, master;
	explicit Scene (int len)
	{
		slave.assign (static_cast<size_t> (len), 0.f);
		auto a = sine (len, 60.0, 0.5);
		auto b = sine (len, 420.0, 0.12);
		auto c = sine (len, 780.0, 0.08);
		auto d = sine (len, 1310.0, 0.05);
		for (int i = 0; i < len; ++i)
			slave[static_cast<size_t> (i)] = a[static_cast<size_t> (i)] + b[static_cast<size_t> (i)] +
			                                 c[static_cast<size_t> (i)] + d[static_cast<size_t> (i)];
		master = sine (len, 60.0, 0.8, 1.0);
	}
};

void testEnergyConservation ()
{
	for (int os : {1, 4})
	{
		SpectralEngine engine;
		engine.prepare (kRate);
		EngineParams p;
		p.pull = 1.f;
		p.harmonics = 1.f; // eta = 1
		p.phaseLink = true;
		p.transShield = 0.f;
		p.inertiaMs = 1.f;
		p.oversampling = os;
		p.targetMode = TargetMode::ExactPeaks;
		engine.setParams (p);
		const int len = 48000;
		Scene s (len);
		std::vector<float> ol, orr;
		run (engine, s.slave, s.slave, &s.master, &s.master, ol, orr);
		const auto& st = engine.getLastFrameStats ();
		const double relErr = std::abs (st.energyOut - st.energyIn) / st.energyIn;
		char msg[128];
		std::snprintf (msg, sizeof msg, "x%d eta=1: frame energy conserved (extracted %.0f %%, %d targets)", os,
		               100.0 * st.extracted / st.energyIn, st.numTargets);
		check (st.extracted > 0.2 * st.energyIn && st.numTargets >= 3 && relErr < 1e-3, msg, relErr);

		// the masked 60 Hz is pulled down, the partials go up
		const int start = len - 8192;
		const double low = toneAmplitude (ol, start, 8192, 60.0);
		const double hi = toneAmplitude (ol, start, 8192, 420.0);
		std::snprintf (msg, sizeof msg, "x%d 60 Hz pulled (%.3f -> %.3f), 420 Hz raised (0.12 -> %.3f)", os, 0.5, low, hi);
		check (low < 0.25 && hi > 0.2, msg);

		// time-domain loudness preserved within 1 dB
		const double rIn = rms (s.slave, start - kN, 8192), rOut = rms (ol, start, 8192);
		std::snprintf (msg, sizeof msg, "x%d RMS preserved (%.2f dB)", os, 20.0 * std::log10 (rOut / rIn));
		check (std::abs (20.0 * std::log10 (rOut / rIn)) < 1.0, msg);
	}

	// eta = 0.42: only that share comes back
	SpectralEngine engine;
	engine.prepare (kRate);
	EngineParams p;
	p.pull = 1.f;
	p.harmonics = 0.42f;
	p.transShield = 0.f;
	engine.setParams (p);
	Scene s (48000);
	std::vector<float> ol, orr;
	run (engine, s.slave, s.slave, &s.master, &s.master, ol, orr);
	const auto& st = engine.getLastFrameStats ();
	check (std::abs (st.injected - 0.42 * st.extracted) < 1e-3 * st.extracted, "eta = 0.42: injected = 0.42 x extracted",
	       st.injected / st.extracted);
}

void testPhaseAlignment ()
{
	// slave in phase opposition with the master: the direct sum cancels
	const int len = 48000;
	auto master = sine (len, 100.0, 0.5);
	auto slave = sine (len, 100.0, 0.5, kPi);
	for (bool link : {false, true})
	{
		SpectralEngine engine;
		engine.prepare (kRate);
		EngineParams p = neutralParams ();
		p.phaseLink = link;
		p.inertiaMs = 1.f;
		engine.setParams (p);
		std::vector<float> ol, orr;
		run (engine, slave, slave, &master, &master, ol, orr);
		// master delayed by the latency + processed slave
		std::vector<float> sum (static_cast<size_t> (len), 0.f);
		for (int i = kN; i < len; ++i)
			sum[static_cast<size_t> (i)] = master[static_cast<size_t> (i - kN)] + ol[static_cast<size_t> (i)];
		const double a = rms (sum, len - 8192, 8192) * std::sqrt (2.0);
		char msg[96];
		if (link)
		{
			std::snprintf (msg, sizeof msg, "Phase Link on: |x1 + x2| = A1 + A2 (%.3f, expected 1.0)", a);
			check (a > 0.95, msg);
		}
		else
		{
			std::snprintf (msg, sizeof msg, "Phase Link off: opposed phases cancel (%.3f)", a);
			check (a < 0.02, msg);
		}
	}
}

void testHarmonicSeries ()
{
	// pure-sine 808 at 55 Hz, master kick tone at 55 Hz: the energy must reappear at 110/165/220 Hz
	const int len = 48000;
	auto slave = sine (len, 55.0, 0.5);
	auto master = sine (len, 55.0, 0.8, 0.7);
	SpectralEngine engine;
	engine.prepare (kRate);
	EngineParams p;
	p.pull = 1.f;
	p.harmonics = 1.f;
	p.transShield = 0.f;
	p.inertiaMs = 1.f;
	p.targetMode = TargetMode::HarmonicSeries;
	engine.setParams (p);
	std::vector<float> ol, orr;
	run (engine, slave, slave, &master, &master, ol, orr);
	const auto& st = engine.getLastFrameStats ();
	const auto& hud = engine.getHudFrame ();
	check (!st.harmonicFallback && std::abs (hud.f0Hz - 55.f) < 1.f, "Harmonic Series: f0 tracked", hud.f0Hz);
	const int start = len - 8192;
	const double h1 = toneAmplitude (ol, start, 8192, 55.0);
	const double h2 = toneAmplitude (ol, start, 8192, 110.0);
	const double h3 = toneAmplitude (ol, start, 8192, 165.0);
	const double h4 = toneAmplitude (ol, start, 8192, 220.0);
	char msg[160];
	std::snprintf (msg, sizeof msg, "harmonics synthesised: f0 %.3f, 2f0 %.3f, 3f0 %.3f, 4f0 %.3f (1/h rolloff)", h1, h2, h3, h4);
	check (h1 < 0.2 && h2 > h3 && h3 > h4 && h4 > 0.05, msg);
	// 1/h weights: energy ratio 2f0 : 4f0 = (1/2) / (1/4) = 2  ->  amplitude ratio sqrt(2)
	check (std::abs (h2 / h4 - std::sqrt (2.0)) < 0.15, "2f0 / 4f0 amplitude ratio = sqrt(2)", h2 / h4);
	const double rIn = rms (slave, start - kN, 8192), rOut = rms (ol, start, 8192);
	check (std::abs (20.0 * std::log10 (rOut / rIn)) < 1.0, "Harmonic Series eta=1: RMS preserved (dB)",
	       20.0 * std::log10 (rOut / rIn));
}

void testTransientShield ()
{
	const int len = 30000;
	std::vector<float> slave (len, 0.f), master = sine (len, 80.0, 0.5);
	auto tone = sine (len, 80.0, 0.5);
	for (int i = 20000; i < len; ++i)
		slave[static_cast<size_t> (i)] = tone[static_cast<size_t> (i)]; // hard attack at 20000
	SpectralEngine engine;
	engine.prepare (kRate);
	EngineParams p;
	p.transShield = 1.f;
	engine.setParams (p);
	std::vector<float> l (slave), ol, orr;
	float maxShield = 0.f;
	const int block = 256;
	ol.assign (len, 0.f);
	orr.assign (len, 0.f);
	for (int start = 0; start < len; start += block)
	{
		const float* in[2] = {slave.data () + start, slave.data () + start};
		const float* m[2] = {master.data () + start, master.data () + start};
		float* out[2] = {ol.data () + start, orr.data () + start};
		engine.process (in, m, 2, out, 2, std::min (block, len - start));
		maxShield = std::max (maxShield, engine.getHudFrame ().shield);
	}
	check (maxShield > 0.9f, "Trans-Shield engages on a hard attack", maxShield);
	// nothing before the attack: no pre-echo ahead of the (delayed) onset
	const double pre = rms (ol, 20000 + kN - 1024, 1000);
	check (pre < 1e-3, "no pre-echo before the attack", pre);
}

void testRobustness ()
{
	SpectralEngine engine;
	engine.prepare (kRate);
	EngineParams p;
	p.pull = 1.f;
	p.harmonics = 1.f;
	p.targetMode = TargetMode::HarmonicSeries;
	p.oversampling = 2;
	engine.setParams (p);
	std::mt19937 rng (3);
	std::uniform_real_distribution<float> dist (-1.f, 1.f);
	const int len = 40000;
	std::vector<float> a (len), b (len, 0.f), ol, orr;
	for (int i = 0; i < len; ++i)
		a[static_cast<size_t> (i)] = (i / 4000) % 2 ? dist (rng) : 0.f; // noise bursts and silence
	std::vector<float> m (len);
	for (int i = 0; i < len; ++i)
		m[static_cast<size_t> (i)] = dist (rng) * 4.f; // hot master
	run (engine, a, b, &m, &m, ol, orr, 1);
	bool finite = true;
	for (int i = 0; i < len; ++i)
		finite = finite && std::isfinite (ol[static_cast<size_t> (i)]) && std::isfinite (orr[static_cast<size_t> (i)]);
	check (finite, "no NaN/Inf: silence, noise, hot master, 1-sample blocks");

	// Delta Solo = processed - dry
	SpectralEngine e1, e2;
	e1.prepare (kRate);
	e2.prepare (kRate);
	EngineParams q;
	e1.setParams (q);
	q.deltaSolo = true;
	e2.setParams (q);
	Scene s (20000);
	std::vector<float> o1, o1r, o2, o2r;
	run (e1, s.slave, s.slave, &s.master, &s.master, o1, o1r);
	run (e2, s.slave, s.slave, &s.master, &s.master, o2, o2r);
	double err = 0.0;
	for (int i = kN; i < 20000; ++i)
		err = std::max (err, static_cast<double> (std::abs (o2[static_cast<size_t> (i)] -
		                                                    (o1[static_cast<size_t> (i)] - s.slave[static_cast<size_t> (i - kN)]))));
	check (err < 1e-5, "Delta Solo outputs x2~ - x2", err);

	// bypass: latency-compensated dry once the 20 ms ramp is over
	SpectralEngine e3;
	e3.prepare (kRate);
	EngineParams by;
	by.bypass = true;
	e3.setParams (by);
	e3.reset ();
	std::vector<float> o3, o3r;
	run (e3, s.slave, s.slave, &s.master, &s.master, o3, o3r);
	err = 0.0;
	for (int i = kN; i < 20000; ++i)
		err = std::max (err, static_cast<double> (std::abs (o3[static_cast<size_t> (i)] - s.slave[static_cast<size_t> (i - kN)])));
	check (err < 1e-6, "bypass = dry delayed by the reported latency", err);
}


/** Short-term peak envelope (256-sample windows) from `from` to the end. */
void envelope (const std::vector<float>& x, int from, double& mn, double& mx)
{
	mn = 1e9;
	mx = 0.0;
	for (int s = from; s + 256 <= static_cast<int> (x.size ()); s += 256)
	{
		double p = 0.0;
		for (int i = 0; i < 256; ++i)
			p = std::max (p, static_cast<double> (std::abs (x[static_cast<size_t> (s + i)])));
		mn = std::min (mn, p);
		mx = std::max (mx, p);
	}
}

double maxStep (const std::vector<float>& x, int from, int to)
{
	double d = 0.0;
	for (int i = std::max (from, 1); i < to; ++i)
		d = std::max (d, static_cast<double> (std::abs (x[static_cast<size_t> (i)] - x[static_cast<size_t> (i - 1)])));
	return d;
}

double phaseAt (const std::vector<float>& x, int start, int len, double freq)
{
	double re = 0.0, im = 0.0;
	for (int i = 0; i < len; ++i)
	{
		const double a = 2.0 * kPi * freq * (start + i) / kRate;
		re += x[static_cast<size_t> (start + i)] * std::cos (a);
		im += x[static_cast<size_t> (start + i)] * std::sin (a);
	}
	return std::atan2 (im, re);
}

void testArtefacts ()
{
	// Phase Link, detuned master 30 dB under the slave (beta ~ 0.5): wrap(phi1 - phi2) jumps
	// from +pi to -pi; the slewed rotation must not let the overlap-add cancel itself
	{
		const int len = 144000;
		auto slave = sine (len, 100.0, 0.5);
		auto master = sine (len, 101.3, 0.5 * std::pow (10.0, -30.0 / 20.0));
		SpectralEngine engine;
		engine.prepare (kRate);
		EngineParams p = neutralParams ();
		p.phaseLink = true;
		engine.setParams (p);
		std::vector<float> ol, orr;
		run (engine, slave, slave, &master, &master, ol, orr, 512);
		double mn, mx;
		envelope (ol, 3 * kN, mn, mx);
		char msg[112];
		std::snprintf (msg, sizeof msg, "Phase Link, detuned master: no dropout (envelope %.3f..%.3f, input 0.5)", mn, mx);
		check (mn > 0.45 && mx < 0.52, msg);
	}

	// Phase Link keeps the stereo image: L / R 90 degrees apart stay 90 degrees apart
	{
		const int len = 96000;
		auto l = sine (len, 200.0, 0.4), r = sine (len, 200.0, 0.4, kPi / 2), master = sine (len, 200.0, 0.4, 2.0);
		SpectralEngine engine;
		engine.prepare (kRate);
		EngineParams p = neutralParams ();
		p.phaseLink = true;
		engine.setParams (p);
		std::vector<float> ol, orr;
		run (engine, l, r, &master, &master, ol, orr, 512);
		const double d = std::remainder (phaseAt (orr, 60000, 9600, 200.0) - phaseAt (ol, 60000, 9600, 200.0), 2.0 * kPi);
		check (std::abs (std::abs (d) - kPi / 2) < 0.05, "Phase Link keeps the L / R phase difference (rad)", d);
	}

	// changing the Oversampling keeps the transfer (no release-and-duck pump)
	{
		const int len = 96000;
		auto slave = sine (len, 220.0, 0.5), master = sine (len, 220.0, 0.5);
		SpectralEngine engine;
		engine.prepare (kRate);
		EngineParams p;
		p.pull = 0.9f;
		p.harmonics = 0.5f;
		engine.setParams (p);
		std::vector<float> out (static_cast<size_t> (len));
		for (int s = 0; s < len; s += 512)
		{
			if (s == 48128)
			{
				p.oversampling = 1;
				engine.setParams (p);
			}
			const float* in[1] = {slave.data () + s};
			const float* m[1] = {master.data () + s};
			float* o[1] = {out.data () + s};
			engine.process (in, m, 1, o, 1, std::min (512, len - s));
		}
		double steadyMin, steadyMax, mn, mx;
		envelope (std::vector<float> (out.begin () + 30000, out.begin () + 48000), 0, steadyMin, steadyMax);
		envelope (std::vector<float> (out.begin () + 48128, out.begin () + 48128 + 2 * kN + 6000), 0, mn, mx);
		check (mx < 1.1 * steadyMax && mn > 0.9 * steadyMin, "Oversampling 4x -> 1x: no level jump (ratio)", mx / steadyMax);
	}

	// Delta Solo is ramped: no click when it is switched
	{
		const int len = 96000;
		auto slave = sine (len, 220.0, 0.5), master = sine (len, 330.0, 0.5);
		SpectralEngine engine;
		engine.prepare (kRate);
		EngineParams p;
		engine.setParams (p);
		std::vector<float> out (static_cast<size_t> (len));
		for (int s = 0; s < len; s += 64)
		{
			if (s == 48000)
			{
				p.deltaSolo = true;
				engine.setParams (p);
			}
			const float* in[1] = {slave.data () + s};
			const float* m[1] = {master.data () + s};
			float* o[1] = {out.data () + s};
			engine.process (in, m, 1, o, 1, std::min (64, len - s));
		}
		const double before = maxStep (out, 20000, 47990), at = maxStep (out, 47990, 49100);
		check (at < 1.5 * before, "Delta Solo switch: no click (max step / steady max step)", at / before);
	}

	// mono slave, stereo sidechain: the master's right channel is heard too
	{
		const int len = 48000;
		auto slave = sine (len, 220.0, 0.5), right = sine (len, 220.0, 0.5);
		std::vector<float> left (static_cast<size_t> (len), 0.f), out (static_cast<size_t> (len));
		SpectralEngine engine;
		engine.prepare (kRate);
		EngineParams p = neutralParams ();
		p.pull = 1.f;
		engine.setParams (p);
		for (int s = 0; s < len; s += 512)
		{
			const float* in[1] = {slave.data () + s};
			const float* m[2] = {left.data () + s, right.data () + s};
			float* o[1] = {out.data () + s};
			engine.process (in, m, 2, o, 1, std::min (512, len - s));
		}
		const double a = rms (out, len - 8192, 8192) * std::sqrt (2.0);
		check (a < 0.47, "mono slave, master on the right channel only: the slave makes room", a);
	}
}
} // namespace

int main ()
{
	testFft ();
	testSpscRing ();
	testYin ();
	testPerfectReconstruction ();
	testLatency ();
	testEnergyConservation ();
	testPhaseAlignment ();
	testHarmonicSeries ();
	testTransientShield ();
	testRobustness ();
	testArtefacts ();
	std::printf ("%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
	return failures ? 1 : 0;
}
