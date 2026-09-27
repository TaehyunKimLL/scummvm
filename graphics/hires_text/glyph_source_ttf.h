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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_TTF_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_TTF_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"

namespace Graphics {

class Font;

/**
 * A live TrueType face, read through FreeType and exposed as a
 * UnicodeGlyphSource: glyphs are rasterised the first time they are asked
 * for, then cached forever (including a clean "the face lacks this code
 * point" miss), so create() never touches anything beyond the face itself
 * and the fixed probe set used to fit the baseline into the cell.
 *
 * Cell width is never taken from the TTF advance: it comes from the Unicode
 * East Asian Width property (isWide()), which is the only thing that agrees with
 * SCVMUNI's cells()==1|2 convention across scripts. The advance is kept
 * alongside, for advance() (hires_text_latin=proportional, metrics=font).
 * A code point's presence in the face, on the other hand, cannot be asked
 * for directly - TTFFont
 * exposes no "has glyph" query - so it is inferred from whether rendering it
 * leaves any ink (see ensure() for the exact rule and its exceptions).
 *
 * Builds without FreeType compile this class down to a stub: create()
 * always fails with an explanatory error, and isWide() (needed regardless,
 * e.g. to size layout without a source) still works.
 */
class TtfGlyphSource : public UnicodeGlyphSource {
public:
	/**
	 * Opens face 0 of stream (the general overload below takes another
	 * face of a collection) at cell size pixelSize, fits the vertical
	 * offset from a fixed probe set, and rasterises nothing else. On success
	 * the source owns stream exactly when dispose is DisposeAfterUse::YES;
	 * on failure stream is disposed of the same way and null is returned
	 * with error filled in.
	 *
	 * pixelSize must lie in [kMinPixelSize, kMaxPixelSize]; anything else is
	 * rejected with an error rather than truncated into the byte-sized cell.
	 *
	 * With requireHangul, a face whose Hangul probes (the first seven of the
	 * fixed probe set) all come back without ink is rejected with "face has
	 * no Hangul glyphs", so a Latin-only face cannot silently replace a
	 * Korean game's .uni fonts. Only Hangul is checked: the probe set holds
	 * no kana or hanzi, and adding some would spend the load-time raster
	 * budget, so other CJK code pages get no such check yet.
	 *
	 * With lineFit, the face is sized so its line (ascent + descent) fills
	 * pixelSize - FreeType's kTTFSizeModeCell, the rule HiResFontBaker's
	 * callers bake with - and glyphs are drawn from the line top with no
	 * probe fit; only the Hangul probes run, and only for requireHangul.
	 * A face sized this way draws smaller than the default fit, which sizes
	 * the characters themselves to pixelSize.
	 *
	 * Legacy: kept, unchanged, for the Korean code-page paths. A UTF-8
	 * translation uses the overload below and checkCoverage() instead.
	 */
	static TtfGlyphSource *create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
	                               int pixelSize, Common::String &error, bool requireHangul = false,
	                               bool lineFit = false);

	/**
	 * As create() above, without the Hangul check, and with @p extraFitProbes
	 * (the translation's sampled code points, CodePointSet::sample(),
	 * I18N_TEXT_DESIGN.md section 4.4) appended to the fixed probe set for the
	 * vertical fit only, so Thai above/below marks and Japanese brackets fit
	 * the cell. At most kMaxExtraFitProbes are used; the load-time raster
	 * budget grows by as many. With none, the fit is exactly the legacy one.
	 * Coverage is checked apart, with checkCoverage() (coverage.h).
	 */
	static TtfGlyphSource *create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
	                               int pixelSize, Common::String &error,
	                               const uint32 *extraFitProbes, uint extraFitProbeCount);

	/**
	 * The general form of both overloads above: requireHangul and lineFit
	 * as in the first, and @p extraFitProbes (the translation's sample) for
	 * either fit.
	 *
	 * Without lineFit this is the second overload: the probes join the
	 * fixed set the characters are fitted to.
	 *
	 * With lineFit the face keeps its line-fitted size and line-top
	 * placement as long as every probe's ink lies inside the cell - so a
	 * sample that already fits (Korean or Japanese in their usual faces)
	 * draws byte for byte what it drew without one. Only when some ink
	 * would fall outside (a face whose ascent is taller than the line it
	 * is sized by, or marks beyond its line: Thai SARA U/UU under
	 * Sukhumvit Set's descent) are the probes' ink box centred in the
	 * cell, and, when the box is taller than the cell, the face shrunk
	 * until it fits, as the default fit does (bounded by the same raster
	 * budget). The fixed probe set plays no part in that check.
	 *
	 * @p faceIndex picks the face of a TrueType collection (.ttc) to open
	 * (a map's "<file>.ttc#<N>", openFontFace()); every probe, fit and
	 * glyph uses that face. 0, the default, is the first face - what the
	 * overloads above open. A face the file does not have (or a negative
	 * index) fails like an unreadable file, with error naming the index.
	 */
	static TtfGlyphSource *create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
	                               int pixelSize, Common::String &error, bool requireHangul, bool lineFit,
	                               const uint32 *extraFitProbes, uint extraFitProbeCount,
	                               int32 faceIndex = 0);

	/**
	 * A pixel font (hires_text.map pixel=<designPx>): a face drawn on a
	 * grid of designPx pixels per em, crisp only at that ppem or a whole
	 * multiple of it. Opened at pixelGridSize(cellSize, designPx) ppem -
	 * never shrunk to fit a probe set, as create() does - in a cell of
	 * cellSize, with glyphs placed by whole pixels from the face's line
	 * top. When the face's line (ascent + descent) is taller than the
	 * cell, the ink of the Hangul and basic Latin probes (the first
	 * kPixelPlacementProbes of the fixed set) is moved into the cell by
	 * whole rows, its top at row 0 when it is itself taller; the ppem stays.
	 * Ink outside the cell is clipped. No translation sample and no Hangul
	 * check: a pixel face is the map's explicit choice.
	 *
	 * Fails like create() for a bad stream, face index or cell size, and
	 * for a designPx pixelGridSize() gives no size for.
	 */
	static TtfGlyphSource *createPixel(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
	                                    int cellSize, int designPx, Common::String &error,
	                                    int32 faceIndex = 0);

	/**
	 * The ppem a pixel font of designPx is opened at in a cell of cellSize:
	 * the largest whole multiple of designPx not taller than the cell, and
	 * designPx itself when the cell is smaller than it. 0 when designPx is
	 * not positive, or when that ppem is above kMaxPixelSize. Pure.
	 */
	static int pixelGridSize(int cellSize, int designPx);

	/** How many of the fixed probes (the Hangul ones, then A g j y) place a
	 *  pixel face whose line is taller than its cell. */
	static const int kPixelPlacementProbes = 11;

	/** The most extra fit probes create() takes. */
	static const uint kMaxExtraFitProbes = 64;

	/** The pixel sizes create() accepts. */
	static const int kMinPixelSize = 6;
	static const int kMaxPixelSize = 255;
	~TtfGlyphSource() override;

	byte cellWidth() const override { return _cellWidth; }
	byte cellHeight() const override { return _cellHeight; }
	byte advanceNarrow() const override { return _cellWidth / 2; }
	byte advanceWide() const override { return _cellWidth; }
	int bitsPerPixel() const override { return 8; }
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	/** FreeType's advance for cp at the size in use (rounded up to whole
	 *  pixels, as TTFFont reports it), or 0 when the face lacks cp. */
	int advance(uint32 cp) override;
	/** The default metrics plus originX: a combining mark whose ink starts
	 *  left of its origin (a Thai mark's negative bearing) is drawn with its
	 *  origin at column originX = min(-left, cellWidth()) of its row instead
	 *  of being clipped. Every non-combining glyph keeps originX 0 and its
	 *  old rows, even with a negative bearing (Latin 'j'). */
	bool metrics(uint32 cp, GlyphMetrics &m) override;
	uint32 glyphCount() const override;

	/**
	 * The row of the face's baseline inside the cell: the row glyphs are
	 * drawn from plus the face's ascent, clipped to the cell. For a
	 * line-fitted face this is the ascent HiResFontBaker recorded for the
	 * same face at the same size, so a baked Latin font can be put on the
	 * same baseline. 0 in a build without FreeType.
	 */
	int baseline() const;

	/** The size the face was opened at once fitted: pixelSize, unless the
	 *  vertical fit had to shrink it (a kTTFSizeModeCell size for a
	 *  line-fitted face, a character size otherwise). 0 without FreeType. */
	int faceSize() const;

	/** The cell row the face's line top is drawn at (negative when the fit
	 *  moved it up); glyphs are placed by it and the face's ascent. 0
	 *  without FreeType. */
	int lineTop() const;

	/** FreeType renders done so far (probes at create() time, plus one per
	 *  distinct code point since); exposed for tests. */
	uint32 rasterCount() const { return _rasterCount; }

	/** Milliseconds spent inside FreeType rendering calls so far (probes at
	 *  create() time, plus one per distinct code point since); exposed for
	 *  tests and logged, with rasterCount(), when the source is destroyed. */
	uint32 totalRenderMs() const { return _totalRenderMs; }

	/**
	 * The map's [hires] gamma=, in hundredths: every glyph rasterised from
	 * now on has its coverage c replaced by 255*(c/255)^(100/gamma), and
	 * glyphs already cached are dropped so they are drawn again with it.
	 * 100 (the default) is off: the rows are FreeType's bytes, untouched.
	 * Clamped to 50..400.
	 *
	 * Zero stays zero and 255 stays 255, so the glyph's box, advance and
	 * fit do not change, and on a blended screen no pixel is added. What
	 * reads the coverage does see a change: the outline (C19 dilate() takes
	 * coverage as distance) widens with the body, about +0.23 px at 2.2 and
	 * up to +0.47 px at 4; on a keyed screen (alpha=false) pixels that now
	 * cross the ink cut (0x40) or the decoration cut (0x80) become whole
	 * solid pixels, so keyed text grows, and below 1 thin keyed strokes can
	 * drop out.
	 */
	void setCoverageGamma(int gammaX100);
	int coverageGamma() const { return _gamma; }

	/**
	 * Fills lut with the gamma= curve (gammaX100 clamped to 50..400).
	 * Above gamma 1, coverage below kGammaToe is left as it is, so faint
	 * fringe (haze between a stacked mark and its base, inside a tight
	 * counter) is not lifted into view. Returns false when the curve is
	 * the identity (gamma 1). Pure; also in the no-FreeType build.
	 */
	static const int kGammaToe = 4;
	static bool buildGammaCurve(int gammaX100, byte lut[256]);

	/** Whether cp is East Asian Wide or Fullwidth: forwards to
	 *  Unicode::isWide() (unicode_props.h), kept for existing callers.
	 *  Available even when this build has no FreeType. */
	static bool isWide(uint32 cp);

	/**
	 * What chooseFitSize() calls to try one candidate size. An abstract
	 * interface rather than a std::function, so this engine header needs no
	 * standard-library include (common/forbidden.h would collide with it).
	 */
	struct FitProbe {
		virtual ~FitProbe() {}
		/** Measures the ink box at size: fills top/bottom, and returns
		 *  whether it fits the cell. */
		virtual bool measure(int size, int &top, int &bottom) = 0;
	};

	/**
	 * Picks a vertical-fit size, independent of any real font or FreeType
	 * state, so the raster-budget bound can be pinned in a unit test with a
	 * fake probe: starting at startSize, tries candidate sizes
	 * down to minSize (inclusive) by calling probe.measure(trySize, top, bottom),
	 * which reports whether that candidate's ink box fits and, regardless
	 * of fit, what its top/bottom are (for bookkeeping). Stops calling
	 * measure once doing so would push rasterCount past maxRasterCount -
	 * a structural bound, checked before the call rather than trusted to
	 * measure, assuming every call costs rendersPerCall rasterisations.
	 * When no candidate both fits and stays within budget, returns the
	 * smallest size that was actually tried (or startSize itself if the
	 * budget allowed no retry at all), with top/bottom updated to match
	 * that size's measurement.
	 */
	static int chooseFitSize(int startSize, int minSize, uint32 rendersPerCall,
	                          uint32 &rasterCount, uint32 maxRasterCount,
	                          FitProbe &probe,
	                          int &top, int &bottom);

private:
	TtfGlyphSource() {}

	static TtfGlyphSource *createImpl(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
	                                  int pixelSize, Common::String &error, bool requireHangul, bool lineFit,
	                                  const uint32 *extraFitProbes, uint extraFitProbeCount, int32 faceIndex);

	struct Entry {
		byte cells = 0;
		int16 advance = 0;	///< FreeType's advance, in pixels; 0 for a miss
		int16 originX = 0;	///< column the glyph's origin was drawn at
		Common::Array<byte> cov;
	};

	Entry &ensure(uint32 cp);

#ifdef USE_FREETYPE2
	// Only the FreeType build ever opens a face; the stub has none of this.
	Graphics::Font *_font = nullptr;
	Common::SeekableReadStream *_stream = nullptr;
	DisposeAfterUse::Flag _dispose = DisposeAfterUse::NO;
	int _yOffset = 0;
	int _faceSize = 0;
#endif

	byte _cellWidth = 0;
	byte _cellHeight = 0;
	uint32 _rasterCount = 0;
	uint32 _totalRenderMs = 0;

	Common::HashMap<uint32, Entry> _cache;

	int _gamma = 100;
	bool _useGamma = false;
	byte _gammaLut[256];
};

} // End of namespace Graphics

#endif
