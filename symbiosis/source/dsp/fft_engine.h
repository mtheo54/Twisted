//------------------------------------------------------------------------
// SYMBIOSIS — fft_engine.h
// Real FFT, radix-2, power-of-two sizes. No dependency.
//
// A real signal of size N is packed into a complex signal of size N/2
// (even samples in the real part, odd samples in the imaginary part),
// transformed by an iterative radix-2 FFT, then split into the N/2+1
// bins of the real spectrum.
//
// prepare() allocates the tables (non real-time thread).
// forward() and inverse() allocate nothing.
//------------------------------------------------------------------------
#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

#if defined(_MSC_VER)
#define SYMBIOSIS_RESTRICT __restrict
#else
#define SYMBIOSIS_RESTRICT __restrict__
#endif

namespace Symbiosis::DSP {

class FftEngine
{
public:
	/** size: power of two >= 8. Call outside the audio thread. */
	void prepare (int newSize)
	{
		assert (newSize >= 8 && (newSize & (newSize - 1)) == 0);
		size = newSize;
		half = size / 2;

		const double twoPi = 6.283185307179586476925286766559;

		// bit reversal for the complex FFT of size half
		bitRev.assign (static_cast<size_t> (half), 0);
		int bits = 0;
		while ((1 << bits) < half)
			++bits;
		for (int i = 0; i < half; ++i)
		{
			int r = 0;
			for (int b = 0; b < bits; ++b)
				r |= ((i >> b) & 1) << (bits - 1 - b);
			bitRev[static_cast<size_t> (i)] = r;
		}

		// twiddles of the complex FFT, contiguous per stage: the stage of half-length h uses
		// exp(-j 2 pi n / 2h), n < h, stored at offset h - 1 (so the inner loop reads them in
		// order and vectorises)
		stageCos.assign (static_cast<size_t> (half), 0.f);
		stageSin.assign (static_cast<size_t> (half), 0.f);
		for (int h = 1; h < half; h <<= 1)
			for (int n = 0; n < h; ++n)
			{
				stageCos[static_cast<size_t> (h - 1 + n)] = static_cast<float> (std::cos (twoPi * n / (2 * h)));
				stageSin[static_cast<size_t> (h - 1 + n)] = static_cast<float> (-std::sin (twoPi * n / (2 * h)));
			}

		// twiddles of the real split: exp(-j 2 pi k / size), k <= half
		splitCos.assign (static_cast<size_t> (half + 1), 0.f);
		splitSin.assign (static_cast<size_t> (half + 1), 0.f);
		for (int k = 0; k <= half; ++k)
		{
			splitCos[static_cast<size_t> (k)] = static_cast<float> (std::cos (twoPi * k / size));
			splitSin[static_cast<size_t> (k)] = static_cast<float> (-std::sin (twoPi * k / size));
		}

		workRe.assign (static_cast<size_t> (half), 0.f);
		workIm.assign (static_cast<size_t> (half), 0.f);
	}

	int getSize () const noexcept { return size; }
	int getNumBins () const noexcept { return half + 1; }

	/** in: size real samples. re/im: half+1 bins. Unnormalised (X[k] = sum x[n] e^{-j2pi kn/N}). */
	void forward (const float* in, float* re, float* im) noexcept
	{
		float* zr = workRe.data ();
		float* zi = workIm.data ();
		for (int n = 0; n < half; ++n)
		{
			const int r = bitRev[static_cast<size_t> (n)];
			zr[r] = in[2 * n];
			zi[r] = in[2 * n + 1];
		}
		complexButterflies (zr, zi);

		// X[k] = E[k] + W^k O[k]
		// E = (Z[k] + conj Z[half-k]) / 2,  O = -j (Z[k] - conj Z[half-k]) / 2
		re[0] = zr[0] + zi[0];
		im[0] = 0.f;
		re[half] = zr[0] - zi[0];
		im[half] = 0.f;
		for (int k = 1; k < half; ++k)
		{
			const float ar = zr[k], ai = zi[k];
			const float br = zr[half - k], bi = -zi[half - k];
			const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
			const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
			const float orr = di, oi = -dr; // O = -j * d
			const float wr = splitCos[static_cast<size_t> (k)], wi = splitSin[static_cast<size_t> (k)];
			re[k] = er + (wr * orr - wi * oi);
			im[k] = ei + (wr * oi + wi * orr);
		}
	}

	/** re/im: half+1 bins. out: size real samples. Includes the 1/size factor. */
	void inverse (const float* re, const float* im, float* out) noexcept
	{
		float* zr = workRe.data ();
		float* zi = workIm.data ();
		// E = (X[k] + conj X[half-k]) / 2, O = (X[k] - conj X[half-k]) / 2 * conj(W^k)
		// Z[k] = E + j O, then z = IFFT(Z)
		for (int k = 0; k < half; ++k)
		{
			const float ar = re[k], ai = im[k];
			const float br = re[half - k], bi = -im[half - k];
			const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
			const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
			const float wr = splitCos[static_cast<size_t> (k)], wi = -splitSin[static_cast<size_t> (k)];
			const float orr = dr * wr - di * wi, oi = dr * wi + di * wr;
			// Z = E + j O, stored conjugated so the forward butterflies compute the inverse
			const int r = bitRev[static_cast<size_t> (k)];
			zr[r] = er - oi;
			zi[r] = -(ei + orr);
		}
		complexButterflies (zr, zi);
		const float scale = 1.f / static_cast<float> (half);
		for (int n = 0; n < half; ++n)
		{
			out[2 * n] = zr[n] * scale;
			out[2 * n + 1] = -zi[n] * scale; // conjugate back
		}
	}

private:
	void complexButterflies (float* zr, float* zi) noexcept
	{
		// stages 1 and 2 merged (radix 4, twiddles 1 and -j)
		for (int start = 0; start < half; start += 4)
		{
			float* r = zr + start;
			float* i = zi + start;
			const float r0 = r[0] + r[1], i0 = i[0] + i[1], r1 = r[0] - r[1], i1 = i[0] - i[1];
			const float r2 = r[2] + r[3], i2 = i[2] + i[3], r3 = r[2] - r[3], i3 = i[2] - i[3];
			r[0] = r0 + r2, i[0] = i0 + i2;
			r[2] = r0 - r2, i[2] = i0 - i2;
			r[1] = r1 + i3, i[1] = i1 - r3; // (r3 + j i3) . (-j) = i3 - j r3
			r[3] = r1 - i3, i[3] = i1 + r3;
		}
		// remaining stages: contiguous twiddles, independent iterations (vectorisable)
		for (int h = 4; h < half; h <<= 1)
		{
			const float* SYMBIOSIS_RESTRICT wr = stageCos.data () + (h - 1);
			const float* SYMBIOSIS_RESTRICT wi = stageSin.data () + (h - 1);
			for (int start = 0; start < half; start += 2 * h)
			{
				float* SYMBIOSIS_RESTRICT ar = zr + start;
				float* SYMBIOSIS_RESTRICT ai = zi + start;
				float* SYMBIOSIS_RESTRICT br = zr + start + h;
				float* SYMBIOSIS_RESTRICT bi = zi + start + h;
				for (int n = 0; n < h; ++n)
				{
					const float tr = br[n] * wr[n] - bi[n] * wi[n];
					const float ti = br[n] * wi[n] + bi[n] * wr[n];
					br[n] = ar[n] - tr;
					bi[n] = ai[n] - ti;
					ar[n] += tr;
					ai[n] += ti;
				}
			}
		}
	}

	int size {0};
	int half {0};
	std::vector<int> bitRev;
	std::vector<float> stageCos, stageSin, splitCos, splitSin;
	std::vector<float> workRe, workIm;
};

} // namespace Symbiosis::DSP
