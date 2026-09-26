/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "sci/graphics/glyphsource_ttf.h"

#include "common/util.h"
#include "graphics/font.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"

#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif

namespace Sci {

namespace {

struct EawRun {
	uint32 first, last;
};

// East Asian Width "Wide" or "Fullwidth" runs, U+0000..U+3FFFF, generated
// from Python's unicodedata (Unicode 16.0) by harness/i18n and copied here
// verbatim from runs/eaw_wide_runs.txt. Sorted ascending and non-overlapping,
// so isWide() below is a binary search.
const EawRun kEawWideRuns[] = {
	{ 0x01100, 0x0115F },
	{ 0x0231A, 0x0231B },
	{ 0x02329, 0x0232A },
	{ 0x023E9, 0x023EC },
	{ 0x023F0, 0x023F0 },
	{ 0x023F3, 0x023F3 },
	{ 0x025FD, 0x025FE },
	{ 0x02614, 0x02615 },
	{ 0x02630, 0x02637 },
	{ 0x02648, 0x02653 },
	{ 0x0267F, 0x0267F },
	{ 0x0268A, 0x0268F },
	{ 0x02693, 0x02693 },
	{ 0x026A1, 0x026A1 },
	{ 0x026AA, 0x026AB },
	{ 0x026BD, 0x026BE },
	{ 0x026C4, 0x026C5 },
	{ 0x026CE, 0x026CE },
	{ 0x026D4, 0x026D4 },
	{ 0x026EA, 0x026EA },
	{ 0x026F2, 0x026F3 },
	{ 0x026F5, 0x026F5 },
	{ 0x026FA, 0x026FA },
	{ 0x026FD, 0x026FD },
	{ 0x02705, 0x02705 },
	{ 0x0270A, 0x0270B },
	{ 0x02728, 0x02728 },
	{ 0x0274C, 0x0274C },
	{ 0x0274E, 0x0274E },
	{ 0x02753, 0x02755 },
	{ 0x02757, 0x02757 },
	{ 0x02795, 0x02797 },
	{ 0x027B0, 0x027B0 },
	{ 0x027BF, 0x027BF },
	{ 0x02B1B, 0x02B1C },
	{ 0x02B50, 0x02B50 },
	{ 0x02B55, 0x02B55 },
	{ 0x02E80, 0x02E99 },
	{ 0x02E9B, 0x02EF3 },
	{ 0x02F00, 0x02FD5 },
	{ 0x02FF0, 0x0303E },
	{ 0x03041, 0x03096 },
	{ 0x03099, 0x030FF },
	{ 0x03105, 0x0312F },
	{ 0x03131, 0x0318E },
	{ 0x03190, 0x031E5 },
	{ 0x031EF, 0x0321E },
	{ 0x03220, 0x03247 },
	{ 0x03250, 0x0A48C },
	{ 0x0A490, 0x0A4C6 },
	{ 0x0A960, 0x0A97C },
	{ 0x0AC00, 0x0D7A3 },
	{ 0x0F900, 0x0FAFF },
	{ 0x0FE10, 0x0FE19 },
	{ 0x0FE30, 0x0FE52 },
	{ 0x0FE54, 0x0FE66 },
	{ 0x0FE68, 0x0FE6B },
	{ 0x0FF01, 0x0FF60 },
	{ 0x0FFE0, 0x0FFE6 },
	{ 0x16FE0, 0x16FE4 },
	{ 0x16FF0, 0x16FF1 },
	{ 0x17000, 0x187F7 },
	{ 0x18800, 0x18CD5 },
	{ 0x18CFF, 0x18D08 },
	{ 0x1AFF0, 0x1AFF3 },
	{ 0x1AFF5, 0x1AFFB },
	{ 0x1AFFD, 0x1AFFE },
	{ 0x1B000, 0x1B122 },
	{ 0x1B132, 0x1B132 },
	{ 0x1B150, 0x1B152 },
	{ 0x1B155, 0x1B155 },
	{ 0x1B164, 0x1B167 },
	{ 0x1B170, 0x1B2FB },
	{ 0x1D300, 0x1D356 },
	{ 0x1D360, 0x1D376 },
	{ 0x1F004, 0x1F004 },
	{ 0x1F0CF, 0x1F0CF },
	{ 0x1F18E, 0x1F18E },
	{ 0x1F191, 0x1F19A },
	{ 0x1F200, 0x1F202 },
	{ 0x1F210, 0x1F23B },
	{ 0x1F240, 0x1F248 },
	{ 0x1F250, 0x1F251 },
	{ 0x1F260, 0x1F265 },
	{ 0x1F300, 0x1F320 },
	{ 0x1F32D, 0x1F335 },
	{ 0x1F337, 0x1F37C },
	{ 0x1F37E, 0x1F393 },
	{ 0x1F3A0, 0x1F3CA },
	{ 0x1F3CF, 0x1F3D3 },
	{ 0x1F3E0, 0x1F3F0 },
	{ 0x1F3F4, 0x1F3F4 },
	{ 0x1F3F8, 0x1F43E },
	{ 0x1F440, 0x1F440 },
	{ 0x1F442, 0x1F4FC },
	{ 0x1F4FF, 0x1F53D },
	{ 0x1F54B, 0x1F54E },
	{ 0x1F550, 0x1F567 },
	{ 0x1F57A, 0x1F57A },
	{ 0x1F595, 0x1F596 },
	{ 0x1F5A4, 0x1F5A4 },
	{ 0x1F5FB, 0x1F64F },
	{ 0x1F680, 0x1F6C5 },
	{ 0x1F6CC, 0x1F6CC },
	{ 0x1F6D0, 0x1F6D2 },
	{ 0x1F6D5, 0x1F6D7 },
	{ 0x1F6DC, 0x1F6DF },
	{ 0x1F6EB, 0x1F6EC },
	{ 0x1F6F4, 0x1F6FC },
	{ 0x1F7E0, 0x1F7EB },
	{ 0x1F7F0, 0x1F7F0 },
	{ 0x1F90C, 0x1F93A },
	{ 0x1F93C, 0x1F945 },
	{ 0x1F947, 0x1F9FF },
	{ 0x1FA70, 0x1FA7C },
	{ 0x1FA80, 0x1FA89 },
	{ 0x1FA8F, 0x1FAC6 },
	{ 0x1FACE, 0x1FADC },
	{ 0x1FADF, 0x1FAE9 },
	{ 0x1FAF0, 0x1FAF8 },
	{ 0x20000, 0x2FFFD },
	{ 0x30000, 0x3FFFD }
};

} // namespace

bool TtfGlyphSource::isWide(uint32 cp) {
	int lo = 0, hi = ARRAYSIZE(kEawWideRuns) - 1;
	while (lo <= hi) {
		const int mid = lo + (hi - lo) / 2;
		if (cp < kEawWideRuns[mid].first)
			hi = mid - 1;
		else if (cp > kEawWideRuns[mid].last)
			lo = mid + 1;
		else
			return true;
	}
	return false;
}

#ifdef USE_FREETYPE2

namespace {

// The fixed probe set from the plan's global constraints: the tallest and
// lowest-descending hangul syllables and jamo, Latin letters with ascenders
// and descenders, brackets, CJK quotation marks, the ellipsis, the em dash
// and the degree sign - "가각똠뷁힣ㄱㅎAgjyÅ|[]{}()「」『』…—°" (26 code
// points, at most the 32 the plan allows). Used only to fit the baseline
// into the cell at create() time; nothing else is rasterised until asked
// for through cells()/row().
const uint32 kProbeCodepoints[] = {
	0x0AC00, 0x0AC01, 0x0B620, 0x0BDC1, 0x0D7A3, 0x03131, 0x0314E,
	0x00041, 0x00067, 0x0006A, 0x00079, 0x000C5,
	0x0007C, 0x0005B, 0x0005D, 0x0007B, 0x0007D, 0x00028, 0x00029,
	0x0300C, 0x0300D, 0x0300E, 0x0300F, 0x02026, 0x02014, 0x000B0,
};

// Matches m7mkfont.py's INK_THRESHOLD: below this, a pixel is treated as
// unlit, so faint antialiasing fringes do not affect the vertical fit.
const int kInkThreshold = 40;

// Draws cp at (x, y) onto surf (already zeroed, i.e. fully transparent) in
// opaque white, and leaves the per-pixel coverage in the alpha channel.
//
// graphics/fonts/ttf.cpp's renderGlyph<uint32>() (the ARGB32 destination
// path used by TTFFont::drawChar) either copies the source colour verbatim
// where FreeType's coverage is 255, or alpha-composites it over the
// destination using the coverage as alpha; against a fully transparent
// (all-zero) destination that composite's result alpha equals the source
// coverage. So with an opaque white source colour, every destination pixel's
// alpha channel *is* the coverage FreeType rendered - and nothing else in
// the pixel depends on it, since colour is constant. test_coverage_is_eight_bit
// pins this: a full pixel reads 255, a partially-covered edge pixel reads a
// value strictly between 0 and 255.
void renderCoverage(Graphics::Font *font, uint32 cp, int x, int y, Graphics::ManagedSurface &surf) {
	const uint32 white = surf.format.ARGBToColor(255, 255, 255, 255);
	font->drawChar(&surf, cp, x, y, white);
}

byte coverageAt(const Graphics::ManagedSurface &surf, int x, int y) {
	uint8 a, r, g, b;
	surf.format.colorToARGB(surf.getPixel(x, y), a, r, g, b);
	return a;
}

} // namespace

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error) {
	if (!stream) {
		error = "no font stream";
		return nullptr;
	}

	// The Font objects opened below never own stream: TtfGlyphSource keeps
	// that ownership itself (per dispose), so the vertical-fit retry can
	// close one Font and open another face size from the same stream
	// without a double free.
	auto openAt = [&](int size) -> Graphics::Font * {
		stream->seek(0);
		return Graphics::loadTTFFont(stream, DisposeAfterUse::NO, size, Graphics::kTTFSizeModeCharacter,
									  0, 0, Graphics::kTTFRenderModeLight);
	};

	Graphics::Font *font = openAt(pixelSize);
	if (!font) {
		error = "could not open the font face";
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}

	const byte cellW = (byte)pixelSize;
	const byte cellH = (byte)pixelSize;
	uint32 rasterCount = 0;

	// m7mkfont.py's ink_box(): draw every probe at (0,0) on a 3-cell-tall
	// canvas and take the rows with any coverage >= kInkThreshold. Also
	// tracks which single code point produced the topmost and bottommost
	// ink row, so a retry at a smaller size (below) can re-check just that
	// one or two glyphs instead of the whole probe set - re-measuring all
	// of them again per candidate size would blow the load-time raster
	// budget (context.md: at most 32 rasterisations total).
	auto inkBox = [&](Graphics::Font *f, const uint32 *cps, int count, int &top, int &bottom,
					   uint32 &topCp, uint32 &bottomCp) {
		const int probeW = cellW * 3, probeH = cellH * 3;
		top = probeH;
		bottom = -1;
		for (int i = 0; i < count; i++) {
			Graphics::ManagedSurface probeSurf(probeW, probeH, Graphics::PixelFormat::createFormatARGB32());
			renderCoverage(f, cps[i], 0, 0, probeSurf);
			rasterCount++;
			for (int y = 0; y < probeH; y++) {
				bool ink = false;
				for (int x = 0; x < probeW; x++) {
					if (coverageAt(probeSurf, x, y) >= kInkThreshold) {
						ink = true;
						break;
					}
				}
				if (ink) {
					if (y < top) {
						top = y;
						topCp = cps[i];
					}
					if (y + 1 > bottom) {
						bottom = y + 1;
						bottomCp = cps[i];
					}
				}
			}
		}
		if (bottom < 0) { // no probe drew any ink: fall back to a centred box
			top = 0;
			bottom = 0;
		}
	};

	int top, bottom;
	uint32 topCp = 0, bottomCp = 0;
	inkBox(font, kProbeCodepoints, ARRAYSIZE(kProbeCodepoints), top, bottom, topCp, bottomCp);

	if (bottom - top > cellH) {
		// Re-check with only the one or two code points that set the
		// current top/bottom: at a smaller size the same glyphs are still
		// the tallest in the overwhelming common case (font metrics scale
		// close to linearly with pixel size), and this keeps the retry to
		// one or two rasterisations per candidate size instead of the full
		// probe set.
		const uint32 worstCps[2] = { topCp, bottomCp };
		const int worstCount = (topCp == bottomCp) ? 1 : 2;

		Graphics::Font *bestFont = font;
		int bestTop = top, bestBottom = bottom;
		for (int trySize = pixelSize - 1; trySize >= 6; trySize--) {
			Graphics::Font *smaller = openAt(trySize);
			if (!smaller)
				continue;
			int t2, b2;
			uint32 unusedTopCp, unusedBottomCp;
			inkBox(smaller, worstCps, worstCount, t2, b2, unusedTopCp, unusedBottomCp);
			if (b2 - t2 <= cellH) {
				delete bestFont;
				bestFont = smaller;
				bestTop = t2;
				bestBottom = b2;
				break;
			}
			delete smaller;
		}
		font = bestFont;
		top = bestTop;
		bottom = bestBottom;
	}

	TtfGlyphSource *src = new TtfGlyphSource();
	src->_font = font;
	src->_stream = stream;
	src->_dispose = dispose;
	src->_cellWidth = cellW;
	src->_cellHeight = cellH;
	src->_yOffset = -top + MAX(0, (cellH - (bottom - top)) / 2);
	src->_rasterCount = rasterCount;
	return src;
}

TtfGlyphSource::~TtfGlyphSource() {
	delete _font;
	if (_dispose == DisposeAfterUse::YES)
		delete _stream;
}

TtfGlyphSource::Entry &TtfGlyphSource::ensure(uint32 cp) {
	Common::HashMap<uint32, Entry>::iterator it = _cache.find(cp);
	if (it != _cache.end())
		return it->_value;

	// Inserting the default-constructed entry (cells = 0, cov empty) first
	// means every return path below - including the "missing" one - leaves
	// the miss cached, so it is never rasterised again.
	Entry &entry = _cache[cp];

	const int cellW = _cellWidth, cellH = _cellHeight;
	Graphics::ManagedSurface surf(cellW * 2, cellH, Graphics::PixelFormat::createFormatARGB32());
	renderCoverage(_font, cp, 0, _yOffset, surf);
	_rasterCount++;

	bool hasInk = false;
	for (int y = 0; y < cellH && !hasInk; y++) {
		for (int x = 0; x < cellW * 2; x++) {
			if (coverageAt(surf, x, y) != 0) {
				hasInk = true;
				break;
			}
		}
	}

	// TTFFont exposes no "has glyph" query, so a code point the face lacks
	// is inferred from drawing no ink at all - except the code points that
	// are legitimately blank (space, no-break space, the CJK ideographic
	// space), which must still count as present so layout can advance past
	// them.
	if (!hasInk && cp != 0x0020 && cp != 0x00A0 && cp != 0x3000)
		return entry;

	entry.cells = isWide(cp) ? 2 : 1;
	entry.cov.resize((size_t)cellH * cellW * 2, 0);
	for (int y = 0; y < cellH; y++)
		for (int x = 0; x < cellW * 2; x++)
			entry.cov[y * cellW * 2 + x] = coverageAt(surf, x, y);

	return entry;
}

int TtfGlyphSource::cells(uint32 cp) {
	return ensure(cp).cells;
}

const byte *TtfGlyphSource::row(uint32 cp, int y) {
	Entry &entry = ensure(cp);
	if (entry.cov.empty())
		return nullptr; // contract: only called when cells(cp) > 0
	return &entry.cov[(size_t)y * _cellWidth * 2];
}

uint32 TtfGlyphSource::glyphCount() const {
	uint32 n = 0;
	for (Common::HashMap<uint32, Entry>::const_iterator it = _cache.begin(); it != _cache.end(); ++it) {
		if (it->_value.cells > 0)
			n++;
	}
	return n;
}

#else // !USE_FREETYPE2

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error) {
	error = "this build has no FreeType";
	if (dispose == DisposeAfterUse::YES)
		delete stream;
	return nullptr;
}

TtfGlyphSource::~TtfGlyphSource() {
}

TtfGlyphSource::Entry &TtfGlyphSource::ensure(uint32 cp) {
	// create() never succeeds without FreeType, so no instance exists to
	// call this; kept only so the class links in a no-FreeType build.
	static Entry missing;
	return missing;
}

int TtfGlyphSource::cells(uint32 cp) {
	return 0;
}

const byte *TtfGlyphSource::row(uint32 cp, int y) {
	return nullptr;
}

uint32 TtfGlyphSource::glyphCount() const {
	return 0;
}

#endif // USE_FREETYPE2

} // End of namespace Sci
