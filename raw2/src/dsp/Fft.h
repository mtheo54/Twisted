#pragma once
// FFT complexe radix-2 en place (taille puissance de 2). Utilisée hors du thread audio.
#include <complex>
#include <cstdint>
#include <vector>

namespace raw2 {

void fft(std::vector<std::complex<double>>& a, bool inverse);

} // namespace raw2
