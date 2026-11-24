#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/text_compose.h"
#include "common/str.h"


// The MMX composite against its own reference: blendPixel() (a division per channel, no table, no shared code with the MMX
// arithmetic) with coverage 0 = untouched and 255 = RGBToColor(). Needs a build with -DHIRES_TEXT_MMX (or DOS) on a CPU
// with MMX; otherwise every test passes without checking anything (and says so).
class HiResTextMMXTestSuite : public CxxTest::TestSuite {
	typedef Graphics::PixelFormat PF;
	static const PF *formats(uint &n) {
		static const PF f[] = {
			PF(2, 5, 6, 5, 0, 11, 5, 0, 0),  // RGB565
			PF(2, 5, 5, 5, 0, 10, 5, 0, 0),  // RGB555
			PF(2, 5, 5, 5, 1, 10, 5, 0, 15), // ARGB1555
			PF(2, 5, 5, 5, 1, 11, 6, 1, 0),  // RGBA5551
			PF(2, 5, 6, 5, 0, 0, 5, 11, 0)   // BGR565
		};
		n = ARRAYSIZE(f);
		return f;
	}

	static uint32 refPixel(const PF &f, uint32 d, const byte *pal, byte idx, byte cov) {
		const byte *c = pal + idx * 3;
		if (!cov)
			return d;
		if (cov == 255)
			return f.RGBToColor(c[0], c[1], c[2]);
		return Graphics::TextCompose::blendPixel(f, d, c[0], c[1], c[2], cov);
	}

	// A palette whose every channel takes all 256 values over the 256 entries (the channels at different strides).
	static void makePalette(byte *pal) {
		for (int k = 0; k < 256; ++k) {
			pal[k * 3] = (byte)k;
			pal[k * 3 + 1] = (byte)(k * 7 + 3);
			pal[k * 3 + 2] = (byte)(k * 13 + 101);
		}
	}

	bool enable(int mode) {
		Graphics::TextCompose::setMMXMode(mode);
		const bool on = Graphics::TextCompose::setUseMMX(true);
#ifdef HIRES_TEXT_MMX
		TS_ASSERT(on); // the host build asked for MMX: an x86 CPU without it must not pass silently
#endif
		if (!on)
			TS_WARN("[hires_text_mmx: MMX not compiled in or absent, nothing checked]");
		return on;
	}

	struct Off {
		~Off() {
			Graphics::TextCompose::setUseMMX(false);
			Graphics::TextCompose::setMMXMode(1);
		}
	};

	// One row through composeSpan() with MMX on, against the reference; counts mismatching pixels.
	static uint32 checkRow(const PF &f, const uint16 *dst0, const byte *idx, const byte *cov, int n, const byte *pal) {
		uint16 dst[1024];
		for (int i = 0; i < n; ++i)
			dst[i] = dst0[i];
		Graphics::TextCompose::composeSpan((byte *)dst, f, idx, cov, n, pal, Graphics::TextCompose::kBlend);
		uint32 bad = 0;
		for (int i = 0; i < n; ++i)
			if (dst[i] != (uint16)refPixel(f, dst0[i], pal, idx[i], cov[i]))
				++bad;
		return bad;
	}

	// (dst, coverage) exhaustive: every 16-bit pixel at every coverage 0..255, the colour rotating over the palette.
	void run_everyPixelEveryCoverage(int mode) {
		Off off;
		if (!enable(mode))
			return;
		uint nf;
		const PF *f = formats(nf);
		byte pal[768];
		makePalette(pal);
		uint32 bad = 0;
		uint64 checked = 0;
		for (uint i = 0; i < nf; ++i)
			for (uint cov = 0; cov < 256; ++cov)
				for (uint32 d = 0; d < 65536; d += 64) {
					uint16 dst[64];
					byte idx[64], cv[64];
					for (int k = 0; k < 64; ++k) {
						dst[k] = (uint16)(d + k);
						idx[k] = (byte)(d / 64 + k * 5 + cov);
						cv[k] = (byte)cov;
					}
					bad += checkRow(f[i], dst, idx, cv, 64, pal);
					checked += 64;
				}
		TS_WARN(Common::String::format("[mmx exhaustive A: %llu pixels] ", (unsigned long long)checked).c_str());
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// (colour, coverage) exhaustive against 256 spread backgrounds, and (colour, dst) against every coverage.
	void run_everyColourEveryCoverage(int mode) {
		Off off;
		if (!enable(mode))
			return;
		uint nf;
		const PF *f = formats(nf);
		byte pal[768];
		makePalette(pal);
		uint32 bad = 0;
		uint64 checked = 0;
		for (uint i = 0; i < nf; ++i)
			for (uint cov = 0; cov < 256; ++cov)
				for (uint ci = 0; ci < 256; ++ci)
					for (uint j = 0; j < 256; j += 64) {
						uint16 dst[64];
						byte idx[64], cv[64];
						for (int k = 0; k < 64; ++k) {
							dst[k] = (uint16)((j + k) * 257 + ci * 31);
							idx[k] = (byte)ci;
							cv[k] = (byte)cov;
						}
						bad += checkRow(f[i], dst, idx, cv, 64, pal);
						checked += 64;
					}
		TS_WARN(Common::String::format("[mmx exhaustive B: %llu pixels] ", (unsigned long long)checked).c_str());
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Per channel exhaustive: every (background channel value, colour channel value, coverage) with the three channels at
	// different colour offsets (so each channel meets each pairing), in every format.
	void run_everyChannelTriple(int mode) {
		Off off;
		if (!enable(mode))
			return;
		uint nf;
		const PF *f = formats(nf);
		uint32 bad = 0;
		uint64 checked = 0;
		for (uint i = 0; i < nf; ++i)
			for (uint cov = 0; cov < 256; ++cov)
				for (uint fv = 0; fv < 256; ++fv) {
					byte pal[768];
					for (int k = 0; k < 256; ++k) {
						pal[k * 3] = (byte)fv;
						pal[k * 3 + 1] = (byte)(fv + 85);
						pal[k * 3 + 2] = (byte)(fv + 170);
					}
					for (uint v0 = 0; v0 < 256; v0 += 64) {
						uint16 dst[64];
						byte idx[64], cv[64];
						for (int k = 0; k < 64; ++k) {
							const uint v = v0 + k;
							dst[k] = (uint16)f[i].RGBToColor((byte)v, (byte)(v + 85), (byte)(v + 170));
							idx[k] = (byte)k;
							cv[k] = (byte)cov;
						}
						bad += checkRow(f[i], dst, idx, cv, 64, pal);
						checked += 64;
					}
				}
		TS_WARN(Common::String::format("[mmx channel triples: %llu pixels] ", (unsigned long long)checked).c_str());
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Random rows: random lengths 0..1023 (tails, short rows), blocks of 0, 255 and mixed coverage, random palettes.
	void run_randomRows(int mode) {
		Off off;
		if (!enable(mode))
			return;
		uint nf;
		const PF *f = formats(nf);
		uint32 s = 99;
		uint32 bad = 0;
		uint64 checked = 0;
		for (uint i = 0; i < nf; ++i)
			for (int row = 0; row < 40000; ++row) {
				byte pal[768];
				uint16 dst[1024];
				byte idx[1024], cv[1024];
				for (int k = 0; k < 768; ++k) {
					s = s * 1103515245 + 12345;
					pal[k] = (byte)(s >> 16);
				}
				s = s * 1103515245 + 12345;
				const int n = (s >> 16) % 1024;
				int mode = 0;
				for (int k = 0; k < n; ++k) {
					s = s * 1103515245 + 12345;
					if (k % 4 == 0)
						mode = (s >> 20) % 4;
					dst[k] = (uint16)(s >> 8);
					idx[k] = (byte)(s >> 5);
					cv[k] = mode == 0 ? 0 : mode == 1 ? 255 : (byte)(s >> 12);
					if (mode == 3 && (s & 0x100))
						cv[k] = (byte)(s & 1 ? 255 : 0);
				}
				bad += checkRow(f[i], dst, idx, cv, n, pal);
				checked += n;
			}
		TS_WARN(Common::String::format("[mmx random: %llu pixels] ", (unsigned long long)checked).c_str());
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// Past the end of the row nothing is written, and the FPU is usable after a call (EMMS: a double stays a double).
	void run_noOverrunAndEmms(int mode) {
		Off off;
		if (!enable(mode))
			return;
		const PF f(2, 5, 6, 5, 0, 11, 5, 0, 0);
		byte pal[768];
		makePalette(pal);
		for (int n = 0; n < 12; ++n) {
			uint16 dst[24];
			byte idx[24], cv[24];
			for (int k = 0; k < 24; ++k) {
				dst[k] = 0xA5A5;
				idx[k] = (byte)k;
				cv[k] = (byte)(k * 11 + 1);
			}
			Graphics::TextCompose::composeSpan((byte *)dst, f, idx, cv, n, pal, Graphics::TextCompose::kBlend);
			for (int k = n; k < 24; ++k)
				TS_ASSERT_EQUALS(dst[k], 0xA5A5);
			volatile double x = 1.5;
			volatile double y = x * 2.0 + 0.25;
			TS_ASSERT_EQUALS(y, 3.25);
		}
	}
public:
	void test_everyPixelEveryCoverage() {
		run_everyPixelEveryCoverage(0);
		run_everyPixelEveryCoverage(1);
	}

	void test_everyColourEveryCoverage() {
		run_everyColourEveryCoverage(0);
		run_everyColourEveryCoverage(1);
	}

	void test_everyChannelTriple() {
		run_everyChannelTriple(0);
		run_everyChannelTriple(1);
	}

	void test_randomRows() {
		run_randomRows(0);
		run_randomRows(1);
	}

	void test_noOverrunAndEmms() {
		run_noOverrunAndEmms(0);
		run_noOverrunAndEmms(1);
	}
};
