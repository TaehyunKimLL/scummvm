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
#include "graphics/hires_text/unicode_props.h"

#include "common/debug.h"
#include "common/system.h"
#include "common/util.h"
#include "graphics/font.h"
#include "graphics/managed_surface.h"
#include "graphics/pixelformat.h"

#ifdef USE_FREETYPE2
#include "graphics/fonts/ttf.h"
#endif

namespace Graphics {

bool TtfGlyphSource::isWide(uint32 cp) {
	return Unicode::isWide(cp);
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
	auto inkBox = [&](Graphics::Font *f, const uint32 *cps, int count, int &top, int &bottom,
					   uint32 &topCp, uint32 &bottomCp, bool *hangulInk) {
		const int probeW = cellW * 3, probeH = cellH * 3;
		top = probeH;
		bottom = -1;
		for (int i = 0; i < count; i++) {
			Graphics::ManagedSurface probeSurf(probeW, probeH, Graphics::PixelFormat::createFormatARGB32());
			const uint32 renderStart = g_system->getMillis();
			renderCoverage(f, cps[i], 0, 0, probeSurf);
			totalRenderMs += g_system->getMillis() - renderStart;
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
					if (hangulInk && i < kHangulProbeCount)
						*hangulInk = true;
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
	bool hangulInk = false;
	// A line-fitted face is placed by its own metrics, so it needs the
	// probes only to prove it draws Hangul.
	const int probeCount = !lineFit ? ARRAYSIZE(kProbeCodepoints) : (requireHangul ? kHangulProbeCount : 0);
	inkBox(font, kProbeCodepoints, probeCount, top, bottom, topCp, bottomCp, &hangulInk);

	if (requireHangul && !hangulInk) {
		error = "face has no Hangul glyphs";
		delete font;
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return nullptr;
	}

	if (!lineFit && bottom - top > cellH) {
		// Re-check with only the one or two code points that set the
		// current top/bottom: at a smaller size the same glyphs are still
		// the tallest in the overwhelming common case (font metrics scale
		// close to linearly with pixel size), and this keeps the retry to
		// one or two rasterisations per candidate size instead of the full
		// probe set.
		const uint32 worstCps[2] = { topCp, bottomCp };
		const int worstCount = (topCp == bottomCp) ? 1 : 2;

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
			uint32 unusedTopCp = 0, unusedBottomCp = 0;
			inkBox(smaller, worstCps, worstCount, t, b, unusedTopCp, unusedBottomCp, nullptr);
			delete bestFont;
			bestFont = smaller;
			return (b - t) <= cellH;
		};

		LambdaFitProbe<decltype(measure)> probe(measure);
		chooseFitSize(pixelSize, 6, (uint32)worstCount, rasterCount, kMaxLoadRasterCount,
					  probe, top, bottom);
		font = bestFont;
	}

	TtfGlyphSource *src = new TtfGlyphSource();
	src->_font = font;
	src->_stream = stream;
	src->_dispose = dispose;
	src->_cellWidth = cellW;
	src->_cellHeight = cellH;
	src->_yOffset = lineFit ? 0 : -top + MAX(0, (cellH - (bottom - top)) / 2);
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
	int originX = 0;
	if (Unicode::isCombining(cp)) {
		const Common::Rect box = _font->getBoundingBox(cp);
		if (box.left < 0)
			originX = MIN<int>(-box.left, cellW);
	}
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

#else // !USE_FREETYPE2

TtfGlyphSource *TtfGlyphSource::create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
										int /*pixelSize*/, Common::String &error, bool /*requireHangul*/,
										bool /*lineFit*/) {
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

#endif // USE_FREETYPE2

} // End of namespace Graphics
