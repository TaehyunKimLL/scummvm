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

#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/unicode_props.h"

#include "common/debug.h"
#include "common/system.h"
#include "common/util.h"
#include "graphics/font.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"

#include <math.h>

#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif

namespace Graphics {

bool TtfGlyphSource::isWide(uint32 cp) {
	return Unicode::isWide(cp);
}

bool TtfGlyphSource::buildGammaCurve(int gammaX100, byte lut[256]) {
	gammaX100 = CLIP(gammaX100, 50, 400);
	if (gammaX100 == 100) {
		for (int i = 0; i < 256; ++i)
			lut[i] = (byte)i;
		return false;
	}
	const double exponent = 100.0 / gammaX100;
	lut[0] = 0;
	for (int i = 1; i < 255; ++i)
		lut[i] = (byte)CLIP<int>((int)(255.0 * pow(i / 255.0, exponent) + 0.5), 0, 255);
	lut[255] = 255;
	return true;
}

void TtfGlyphSource::setCoverageGamma(int gammaX100) {
	gammaX100 = CLIP(gammaX100, 50, 400);
	if (gammaX100 == _gamma)
		return;
	_gamma = gammaX100;
	_useGamma = buildGammaCurve(gammaX100, _gammaLut);
	// A glyph cached under the old curve would otherwise keep it.
	_cache.clear();
}

int TtfGlyphSource::chooseFitSize(int startSize, int minSize, uint32 rendersPerCall,
								   uint32 &rasterCount, uint32 maxRasterCount,
								   FitProbe &probe,
								   int &top, int &bottom) {
	int chosenSize = startSize;
	for (int trySize = startSize - 1; trySize >= minSize; trySize--) {
		// Structural bound: decided before calling measure, not after, so
		// the total can never exceed maxRasterCount regardless of how many
		// candidate sizes remain to try.
		if (rasterCount + rendersPerCall > maxRasterCount) {
			debug(1, "TtfGlyphSource: vertical-fit search capped at size %d after %u/%u "
					 "rasterisations (raster budget reached before a candidate fit)",
				  chosenSize, rasterCount, maxRasterCount);
			break;
		}

		int t = 0, b = 0;
		const bool fits = probe.measure(trySize, t, b);
		rasterCount += rendersPerCall;
		top = t;
		bottom = b;
		chosenSize = trySize;

		if (fits)
			break;
	}
	return chosenSize;
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
	0x0AC00, 0x0AC01, 0x0B620, 0x0BDC1, 0x0D7A3, 0x03131, 0x0314E,	// Hangul: see kHangulProbeCount
	0x00041, 0x00067, 0x0006A, 0x00079, 0x000C5,
	0x0007C, 0x0005B, 0x0005D, 0x0007B, 0x0007D, 0x00028, 0x00029,
	0x0300C, 0x0300D, 0x0300E, 0x0300F, 0x02026, 0x02014, 0x000B0,
};

// The leading Hangul syllables and jamo of kProbeCodepoints, checked by
// create(requireHangul).
const int kHangulProbeCount = 7;

// Matches m7mkfont.py's INK_THRESHOLD: below this, a pixel is treated as
// unlit, so faint antialiasing fringes do not affect the vertical fit.
const int kInkThreshold = 40;

// context.md's global constraint: "open the face, plus at most 32 probe
// rasterisations for the vertical fit". The probe set itself is 26, so this
// leaves headroom for the vertical-fit retry below, bounded structurally by
// chooseFitSize() rather than by hoping the retry never needs more.
const uint32 kMaxLoadRasterCount = 32;

// Rows a fit to a translation's sample keeps free above and below the
// glyphs it re-checks at a smaller size (see createImpl()).
const int kSampleFitSlack = 1;

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

// The column a glyph's origin is drawn at in its row: a mark (isFitMark(),
// coverage.h) whose ink starts left of its origin (a Thai mark's negative
// bearing puts it over the preceding base) is moved right by that much;
// every other glyph keeps column 0. The vertical fit draws its probes the
// same way, so a mark's ink is measured where it is drawn.
int markOriginX(Graphics::Font *font, uint32 cp, int cellW) {
	if (isFitMark(cp)) {
		const Common::Rect box = font->getBoundingBox(cp);
		if (box.left < 0)
			return MIN<int>(-box.left, cellW);
	}
	return 0;
}

// Adapts a lambda to chooseFitSize()'s FitProbe, so create() keeps its
// retry logic next to the state it captures.
template<typename F>
struct LambdaFitProbe : public TtfGlyphSource::FitProbe {
	explicit LambdaFitProbe(F &f) : _f(f) {}
	bool measure(int size, int &top, int &bottom) override { return _f(size, top, bottom); }
	F &_f;
};

} // namespace

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error, bool requireHangul,
										bool lineFit) {
	return createImpl(stream, dispose, pixelSize, error, requireHangul, lineFit, nullptr, 0);
}

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error,
										const uint32 *extraFitProbes, uint extraFitProbeCount) {
	return createImpl(stream, dispose, pixelSize, error, false, false, extraFitProbes, extraFitProbeCount);
}

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error, bool requireHangul, bool lineFit,
										const uint32 *extraFitProbes, uint extraFitProbeCount) {
	return createImpl(stream, dispose, pixelSize, error, requireHangul, lineFit, extraFitProbes, extraFitProbeCount);
}

TtfGlyphSource *TtfGlyphSource::createImpl(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
											int pixelSize, Common::String &error, bool requireHangul, bool lineFit,
											const uint32 *extraFitProbes, uint extraFitProbeCount) {
	if (!stream) {
		error = "no font stream";
		return nullptr;
	}
	// The cell is byte-sized and the vertical-fit retry goes down to 6px, so
	// a size outside that range is refused rather than truncated.
	if (pixelSize < kMinPixelSize || pixelSize > kMaxPixelSize) {
		error = Common::String::format("pixel size %d is outside %d..%d", pixelSize, kMinPixelSize, kMaxPixelSize);
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}

	// The Font objects opened below never own stream: TtfGlyphSource keeps
	// that ownership itself (per dispose), so the vertical-fit retry can
	// close one Font and open another face size from the same stream
	// without a double free.
	auto openAt = [&](int size) -> Graphics::Font * {
		stream->seek(0);
		return Graphics::loadTTFFont(stream, DisposeAfterUse::NO, size,
									  lineFit ? Graphics::kTTFSizeModeCell : Graphics::kTTFSizeModeCharacter,
									  0, 0, Graphics::kTTFRenderModeLight);
	};

	Graphics::Font *font = openAt(pixelSize);
	if (!font) {
		error = "could not open the font face";
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}

	// Lossless: pixelSize was range-checked against kMaxPixelSize above.
	const byte cellW = (byte)pixelSize;
	const byte cellH = (byte)pixelSize;
	uint32 rasterCount = 0;
	uint32 totalRenderMs = 0;

	// m7mkfont.py's ink_box(): draw every probe at (0,0) on a 3-cell-tall
	// canvas and take the rows with any coverage >= kInkThreshold. Also
	// tracks which single code point produced the topmost and bottommost
	// ink row, so a retry at a smaller size (below) can re-check just that
	// one or two glyphs instead of the whole probe set - re-measuring all
	// of them again per candidate size would blow the load-time raster
	// budget (context.md: at most 32 rasterisations total).
	// hangulInk, when given, is set when any of the first kHangulProbeCount
	// code points drew ink.
	// Each probe is drawn with its line top at canvas row drawY, and top /
	// bottom are relative to that row: the default fit draws at 0, a
	// line-fitted face's check at cellH, so ink above its line top counts.
	// A combining mark is drawn at its originX, as ensure() draws it, so
	// its ink left of the origin is not lost off the canvas.
	// boxes, when given, receives each probe's own top and bottom (in
	// pairs; top > bottom for a probe without ink).
	auto inkBox = [&](Graphics::Font *f, const uint32 *cps, int count, int drawY, int &top, int &bottom,
					   uint32 &topCp, uint32 &bottomCp, bool *hangulInk, Common::Array<int> *boxes = nullptr) {
		const int probeW = cellW * 3, probeH = cellH * 3;
		top = probeH;
		bottom = -1 - drawY;
		for (int i = 0; i < count; i++) {
			Graphics::ManagedSurface probeSurf(probeW, probeH, Graphics::PixelFormat::createFormatARGB32());
			const uint32 renderStart = g_system->getMillis();
			renderCoverage(f, cps[i], markOriginX(f, cps[i], cellW), drawY, probeSurf);
			totalRenderMs += g_system->getMillis() - renderStart;
			rasterCount++;
			int ownTop = probeH, ownBottom = -probeH;
			for (int y = 0; y < probeH; y++) {
				bool ink = false;
				for (int x = 0; x < probeW; x++) {
					if (coverageAt(probeSurf, x, y) >= kInkThreshold) {
						ink = true;
						break;
					}
				}
				if (ink) {
					ownTop = MIN(ownTop, y - drawY);
					ownBottom = y - drawY + 1;
					if (hangulInk && i < kHangulProbeCount)
						*hangulInk = true;
					if (y - drawY < top) {
						top = y - drawY;
						topCp = cps[i];
					}
					if (y - drawY + 1 > bottom) {
						bottom = y - drawY + 1;
						bottomCp = cps[i];
					}
				}
			}
			if (boxes) {
				boxes->push_back(ownTop);
				boxes->push_back(ownBottom);
			}
		}
		if (bottom < -drawY) { // no probe drew any ink: fall back to a centred box
			top = 0;
			bottom = 0;
		}
	};

	// The fixed set first (its first kHangulProbeCount entries stay the
	// Hangul ones), then the caller's extra fit probes; the raster budget
	// grows by exactly as many, so with none the fit is the legacy one.
	if (!extraFitProbes)
		extraFitProbeCount = 0;
	extraFitProbeCount = MIN<uint>(extraFitProbeCount, kMaxExtraFitProbes);
	Common::Array<uint32> probes(kProbeCodepoints, ARRAYSIZE(kProbeCodepoints));
	for (uint i = 0; i < extraFitProbeCount; i++)
		probes.push_back(extraFitProbes[i]);
	const uint32 maxLoadRasterCount = kMaxLoadRasterCount + extraFitProbeCount;

	int top, bottom;
	uint32 topCp = 0, bottomCp = 0;
	bool hangulInk = false;
	// The mark-aware fit (below) is for a sample that holds Thai or Lao
	// stacking marks (isStackingFitMark(), SARA AM included): their ink
	// lies above the ascent, below the descent and left of the origin. Any
	// other sample - Korean, Japanese, Latin, with or without their own
	// combining marks (U+3099, U+0301) - is fitted exactly as before, as
	// is no sample at all.
	bool sampleHasMarks = false;
	for (uint i = 0; i < extraFitProbeCount && !sampleHasMarks; i++)
		sampleHasMarks = isStackingFitMark(extraFitProbes[i]);
	// The mark-aware fit measures with the line top a cell down the canvas,
	// so a mark drawn above the face's ascent (Sukhumvit Set's MAI
	// CHATTAWA) is seen; so does a line-fitted face's check of its sample.
	// The legacy fit keeps drawing at the canvas top.
	const int drawY = (lineFit ? extraFitProbeCount > 0 : sampleHasMarks) ? cellH : 0;
	// Every probe's own box, to pick the mark-aware retry's glyphs.
	Common::Array<int> boxes;
	const uint32 *boxCps = probes.data();
	// A line-fitted face is placed by its own metrics, so it needs the
	// probes only to prove it draws Hangul; extra fit probes do not apply.
	if (lineFit)
		inkBox(font, kProbeCodepoints, requireHangul ? kHangulProbeCount : 0, 0, top, bottom, topCp, bottomCp, &hangulInk);
	else
		inkBox(font, probes.data(), (int)probes.size(), drawY, top, bottom, topCp, bottomCp, &hangulInk,
			   sampleHasMarks ? &boxes : nullptr);

	if (requireHangul && !hangulInk) {
		error = "face has no Hangul glyphs";
		delete font;
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}

	// A line-fitted face is placed by its own metrics only while the
	// translation's characters fit the cell that way. Its ascent may be
	// taller than the line it was sized by (Sukhumvit Set: hhea ascent
	// 1103 units against an OS/2 win line of 834 + 250), putting the
	// baseline below the cell, and marks may reach past its descent (Thai
	// SARA U/UU): then the sample's ink box decides the placement, and the
	// size when the box is taller than the cell. A sample that fits leaves
	// everything as it was.
	bool lineRefit = false;
	if (lineFit && extraFitProbeCount > 0) {
		boxCps = extraFitProbes;
		inkBox(font, extraFitProbes, (int)extraFitProbeCount, drawY, top, bottom, topCp, bottomCp, nullptr, &boxes);
		lineRefit = top < 0 || bottom > cellH;
	}

	// Only a line-fitted face that must move, or a sample with marks,
	// takes the mark-aware retry; everything else keeps the legacy one.
	const bool markFit = lineRefit || (!lineFit && sampleHasMarks);
	if (!markFit)
		boxes.clear();

	int faceSize = pixelSize;
	if ((!lineFit || lineRefit) && bottom - top > cellH) {
		// Re-check with only the one or two code points that set the
		// current top/bottom: at a smaller size the same glyphs are still
		// the tallest in the overwhelming common case (font metrics scale
		// close to linearly with pixel size), and this keeps the retry to
		// one or two rasterisations per candidate size instead of the full
		// probe set.
		//
		// The mark-aware fit (Thai: SARA I, MAI EK and MAI CHATTAWA within a row
		// of each other at the top; SARA UU, PHINTHU, DO CHADA and Latin g
		// at the bottom), which glyph is tallest can change with the size:
		// the next-highest and next-lowest glyphs are re-checked too (up to
		// kMaxWorstProbes in all), and a size must leave a row free above
		// and below them (kSampleFitSlack), for a glyph not re-checked that
		// rounds a row further. The legacy fit keeps its one or two glyphs
		// and no slack.
		const int kMaxWorstProbes = 4;
		const int slack = boxes.empty() ? 0 : kSampleFitSlack;
		uint32 worstCps[kMaxWorstProbes] = { topCp, bottomCp };
		int worstCount = (topCp == bottomCp) ? 1 : 2;
		for (int side = 0; !boxes.empty() && worstCount < kMaxWorstProbes; side ^= 1) {
			// The not yet chosen glyph whose top (side 0) or bottom (side 1)
			// is closest to the box's.
			int best = -1;
			for (uint i = 0; i + 1 < boxes.size(); i += 2) {
				if (boxes[i] > boxes[i + 1])
					continue; // no ink
				const uint32 cp = boxCps[i / 2];
				bool seen = false;
				for (int k = 0; k < worstCount; k++)
					seen = seen || worstCps[k] == cp;
				if (seen)
					continue;
				if (best < 0 || (side == 0 ? boxes[i] < boxes[best] : boxes[i + 1] > boxes[best + 1]))
					best = (int)i;
			}
			if (best < 0)
				break;
			worstCps[worstCount++] = boxCps[best / 2];
		}

		// bestFont always tracks the most recently opened candidate (i.e.
		// the smallest size tried so far), not just the one that ends up
		// fitting: if the raster budget runs out before anything fits,
		// chooseFitSize's contract is to keep the smallest size tried, so
		// the Font actually kept must track that too, not silently fall
		// back to the original (oversized) face.
		Graphics::Font *bestFont = font;
		auto measure = [&](int trySize, int &t, int &b) -> bool {
			Graphics::Font *smaller = openAt(trySize);
			if (!smaller) {
				// Could not even open this size: report the previous
				// measurement unchanged and treat it as "does not fit",
				// so the search keeps trying smaller sizes.
				t = top;
				b = bottom;
				return false;
			}
			// chooseFitSize() counts these renders itself (rendersPerCall);
			// the mark-aware fit takes inkBox's own count back rather than
			// counting them twice. The legacy fit keeps the double count
			// (and so its number of tries within the budget) unchanged.
			const uint32 countBefore = rasterCount;
			uint32 unusedTopCp = 0, unusedBottomCp = 0;
			inkBox(smaller, worstCps, worstCount, drawY, t, b, unusedTopCp, unusedBottomCp, nullptr);
			if (markFit)
				rasterCount = countBefore;
			delete bestFont;
			bestFont = smaller;
			faceSize = trySize;
			return (b - t) <= cellH - 2 * slack;
		};

		// Stepping down one size at a time from pixelSize, as far as the
		// raster budget reaches, finds the largest size that fits. When the
		// box is so much taller than the cell that the budget cannot step
		// that far (Thai marks above and below: about 1.4 cells), the
		// mark-aware search starts just above the size the box scales to
		// linearly instead, so the few steps it has land on the fitting size.
		int startSize = pixelSize;
		const int estimate = MAX(6, pixelSize * (cellH - 2 * slack) / (bottom - top));
		if (markFit && rasterCount + (uint32)(pixelSize - estimate) * worstCount > maxLoadRasterCount)
			startSize = MIN(pixelSize, estimate + 2);

		LambdaFitProbe<decltype(measure)> probe(measure);
		chooseFitSize(startSize, 6, (uint32)worstCount, rasterCount, maxLoadRasterCount,
					  probe, top, bottom);
		font = bestFont;
	}

	TtfGlyphSource *src = new TtfGlyphSource();
	src->_font = font;
	src->_stream = stream;
	src->_dispose = dispose;
	src->_cellWidth = cellW;
	src->_cellHeight = cellH;
	src->_faceSize = faceSize;
	src->_yOffset = (lineFit && !lineRefit) ? 0 : -top + MAX(0, (cellH - (bottom - top)) / 2);
	src->_rasterCount = rasterCount;
	src->_totalRenderMs = totalRenderMs;
	return src;
}

TtfGlyphSource::~TtfGlyphSource() {
	// context.md's Task 3: the total FreeType render cost over this source's
	// lifetime (probes at create() time, plus one rasterisation per distinct
	// code point since), so a run.log can be grepped for the measurement
	// without instrumenting the caller. "%u glyphs rasterised" is
	// _rasterCount: every FreeType render this source performed, including
	// create()'s probes and clean misses (a code point the face lacks still
	// costs one render) - not glyphCount(), which counts only the cached
	// hits.
	debug(1, "TtfGlyphSource: %u glyphs rasterised, %u ms total render time, %.3f ms/glyph mean",
		  _rasterCount, _totalRenderMs, _rasterCount ? (double)_totalRenderMs / _rasterCount : 0.0);
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
	// A combining mark's ink lies left of its origin (its negative bearing
	// puts it over the preceding base) and would be clipped at column 0, so
	// a mark is drawn with its origin further right. Every other glyph keeps
	// its origin at column 0 and its old rows - including Latin 'j', whose
	// descender reaches left of the origin - until the engines read originX
	// (C11 T5/T6/T8); a drawer that does not would otherwise move it.
	// SARA AM (Thai U+0E33, Lao AM U+0EB3) is general category Lo, not a
	// mark: it advances like a vowel (the SARA AA part), but it is
	// NIKHAHIT + SARA AA in one glyph, and the NIKHAHIT ring is drawn over
	// the base before it - left of the glyph's own origin (Sukhumvit Set
	// 16 px: bounding box left -4, measured). Drawn with its origin at
	// column 0 the ring was cut off. It gets the marks' treatment: origin
	// moved right by the ink left of it; its advance is unchanged.
	const int originX = markOriginX(_font, cp, cellW);
	Graphics::ManagedSurface surf(cellW * 2, cellH, Graphics::PixelFormat::createFormatARGB32());
	const uint32 renderStart = g_system->getMillis();
	renderCoverage(_font, cp, originX, _yOffset, surf);
	_totalRenderMs += g_system->getMillis() - renderStart;
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

	entry.cells = Unicode::isWide(cp) ? 2 : 1;
	entry.originX = (int16)originX;
	// The glyph was drawn with its origin at column originX (bearing kept),
	// so this advance is measured from there.
	entry.advance = (int16)CLIP<int>(_font->getCharWidth(cp), 0, 0x7FFF);
	entry.cov.resize((size_t)cellH * cellW * 2, 0);
	for (int y = 0; y < cellH; y++)
		for (int x = 0; x < cellW * 2; x++)
			entry.cov[y * cellW * 2 + x] = coverageAt(surf, x, y);
	if (_useGamma) {
		for (uint i = 0; i < entry.cov.size(); ++i)
			entry.cov[i] = _gammaLut[entry.cov[i]];
	}

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

int TtfGlyphSource::advance(uint32 cp) {
	return ensure(cp).advance;
}

bool TtfGlyphSource::metrics(uint32 cp, GlyphMetrics &m) {
	if (!UnicodeGlyphSource::metrics(cp, m))
		return false;
	m.originX = ensure(cp).originX;
	return true;
}

uint32 TtfGlyphSource::glyphCount() const {
	uint32 n = 0;
	for (Common::HashMap<uint32, Entry>::const_iterator it = _cache.begin(); it != _cache.end(); ++it) {
		if (it->_value.cells > 0)
			n++;
	}
	return n;
}

int TtfGlyphSource::baseline() const {
	// Glyphs are drawn from the line top at row _yOffset, and the face's
	// baseline lies its ascent below that.
	if (!_font)
		return 0;
	return CLIP<int>(_yOffset + _font->getFontAscent(), 0, _cellHeight);
}

int TtfGlyphSource::faceSize() const {
	return _faceSize;
}

int TtfGlyphSource::lineTop() const {
	return _yOffset;
}

#else // !USE_FREETYPE2

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error, bool requireHangul,
										bool lineFit) {
	return createImpl(stream, dispose, pixelSize, error, requireHangul, lineFit, nullptr, 0);
}

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error,
										const uint32 *extraFitProbes, uint extraFitProbeCount) {
	return createImpl(stream, dispose, pixelSize, error, false, false, extraFitProbes, extraFitProbeCount);
}

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int pixelSize, Common::String &error, bool requireHangul, bool lineFit,
										const uint32 *extraFitProbes, uint extraFitProbeCount) {
	return createImpl(stream, dispose, pixelSize, error, requireHangul, lineFit, extraFitProbes, extraFitProbeCount);
}

TtfGlyphSource *TtfGlyphSource::createImpl(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
											int /*pixelSize*/, Common::String &error, bool /*requireHangul*/, bool /*lineFit*/,
											const uint32 * /*extraFitProbes*/, uint /*extraFitProbeCount*/) {
	error = "this build has no FreeType";
	if (dispose == DisposeAfterUse::YES)
		delete stream;
	return nullptr;
}

TtfGlyphSource::~TtfGlyphSource() {
}

TtfGlyphSource::Entry &TtfGlyphSource::ensure(uint32 /*cp*/) {
	// create() never succeeds without FreeType, so no instance exists to
	// call this; kept only so the class links in a no-FreeType build.
	static Entry missing;
	return missing;
}

int TtfGlyphSource::cells(uint32 /*cp*/) {
	return 0;
}

const byte *TtfGlyphSource::row(uint32 /*cp*/, int /*y*/) {
	return nullptr;
}

int TtfGlyphSource::advance(uint32 /*cp*/) {
	return 0;
}

bool TtfGlyphSource::metrics(uint32 /*cp*/, GlyphMetrics &/*m*/) {
	return false;
}

uint32 TtfGlyphSource::glyphCount() const {
	return 0;
}

int TtfGlyphSource::baseline() const {
	return 0;
}

int TtfGlyphSource::faceSize() const {
	return 0;
}

int TtfGlyphSource::lineTop() const {
	return 0;
}

#endif // USE_FREETYPE2

} // End of namespace Graphics
