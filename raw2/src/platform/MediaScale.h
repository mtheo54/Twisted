#pragma once
// Outils internes des décodeurs de fonds (voir Media.h).
namespace raw2::media {
void fitSize(int w, int h, int maxW, int maxH, int& tw, int& th);
void scaleBGRA(const unsigned char* src, int sw, int sh, int sstride, unsigned char* dst, int dw, int dh);
} // namespace raw2::media
