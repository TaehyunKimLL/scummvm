#ifndef TEST_ENGINES_SCUMM_SUPPORT_HIRES_FIXTURE_H
#define TEST_ENGINES_SCUMM_SUPPORT_HIRES_FIXTURE_H

// Shared setup for the SCUMM hi-res text suites (version-2 maps).
//
// This header sits outside test/engines/scumm/*.h, which cxxtestgen globs
// into test/runner.cpp: a cxxtest suite class in
// here would be picked up a second time and run twice. Free helper
// functions are fine - test/runner.cpp is one translation unit, and the
// include guard above keeps a repeated #include from redefining them.

#include "common/array.h"
#include "common/memstream.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../../system/null_osystem.h"

namespace ScummHiResFixture {

// A card charset (with its own SVF in the shipped MI2 map) and a charset
// with none of its own, used throughout as "the charset under test" and "a
// charset that must borrow its neighbour's".
static const int kCs = 4;
static const int kOtherCs = 0;
static const int kCell = 16;

inline void put16(Common::Array<byte> &b, uint pos, uint16 v) {
	b[pos] = v & 0xff;
	b[pos + 1] = (v >> 8) & 0xff;
}

inline void put32(Common::Array<byte> &b, uint pos, uint32 v) {
	put16(b, pos, v & 0xffff);
	put16(b, pos + 2, v >> 16);
}

/**
 * A version 2 SVFN file, 1bpp proportional, holding one glyph per code point
 * in @p cps, each with some ink so cells()/glyphInk() see it. Every glyph is
 * a kCell x kCell cell with an advance of 9 hi-res px.
 */
inline Common::Array<byte> makeFont(const Common::Array<uint32> &cps, int cellHeight = kCell) {
	const int n = cps.size();
	const int rowPitch = (kCell + 7) / 8;
	const uint32 glyphStride = rowPitch * cellHeight;
	const uint32 metricsOff = 36;
	const uint32 dataOff = metricsOff + n * 4;
	const uint32 dataSize = glyphStride * n;
	const uint32 cmapOff = dataOff + dataSize;

	Common::Array<byte> b;
	b.resize(cmapOff + n * 8);
	for (uint i = 0; i < b.size(); ++i)
		b[i] = 0;
	b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
	put16(b, 4, 2);
	put16(b, 6, 1);
	b[8] = 1;
	put16(b, 12, n);
	b[14] = kCell;
	b[15] = cellHeight;
	b[16] = 12;
	put32(b, 20, metricsOff);
	put32(b, 24, dataOff);
	put32(b, 28, dataSize);
	put32(b, 32, cmapOff);
	const int inkTop = MIN(8, MAX(0, cellHeight - 4));
	const int inkBottom = MIN(cellHeight, inkTop + 4);
	for (int i = 0; i < n; ++i) {
		b[metricsOff + i * 4 + 0] = 9;  // advance
		b[metricsOff + i * 4 + 1] = 1;  // left bearing
		b[metricsOff + i * 4 + 2] = 6;  // ink width
		for (int y = inkTop; y < inkBottom; ++y)
			b[dataOff + i * glyphStride + y * rowPitch] = 0x7e;
		put32(b, cmapOff + i * 8, cps[i]);
		put32(b, cmapOff + i * 8 + 4, i);
	}
	return b;
}

/**
 * Parse @p body (appended after a fixed [map]/[render]/[font] header - see
 * below) as a version-2 map and adopt it into @p hr, with @p kCs and
 * @p kOtherCs given the same 8x8 game grid hires_latin_same.h used: at
 * scale 2 a kCell (16px) SVF is exactly 2x, and nearestFont() matches
 * kOtherCs's font to kCs's by that grid, not by the noteGameCharset() cell.
 */
inline bool openMap(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const Common::String &text,
					const Graphics::HiResIniOverrides &ini = Graphics::HiResIniOverrides()) {
	Graphics::HiResMap m;
	Common::Array<Common::String> q;
	Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
	if (!Graphics::HiResFontMap::loadMap(s, Common::Path("/tmp/t", '/'), q, Graphics::kHiResKeysScumm, m))
		return false;
	hr.useOverlay(&overlay);
	hr.adoptMap(m, ini);
	hr.noteGameCharset(kCs, 8, 8);
	hr.noteGameCharset(kOtherCs, 8, 8);
	hr.setCharsetGrid(kCs, 8, 8);
	hr.setCharsetGrid(kOtherCs, 8, 8);
	return true;
}

/// As openMap(), with the common [map]/[render]/[font] header hires_glyph_targets.h uses.
inline bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body,
				 const Graphics::HiResIniOverrides &ini = Graphics::HiResIniOverrides()) {
	const Common::String text =
		Common::String("[map]\nversion=2\n[render]\nblend=off\n[font]\nmissing=u+25a1\n") + body;
	return openMap(hr, overlay, text, ini);
}

/// Add a face built by makeFont() under @p path.
inline bool addFace(Scumm::ScummHiResText &hr, const char *path, const Common::Array<uint32> &cps) {
	const Common::Array<byte> bytes = makeFont(cps);
	Common::MemoryReadStream ms(bytes.begin(), bytes.size());
	return hr.addFace(path, ms);
}

/// RAII-ish setUp()/tearDown() pair every suite below calls into.
inline void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
	Common::install_null_g_system();
#endif
}

inline void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
	Common::uninstall_null_g_system();
#endif
}

} // namespace ScummHiResFixture

#endif
