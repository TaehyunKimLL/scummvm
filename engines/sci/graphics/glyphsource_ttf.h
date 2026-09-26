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

#ifndef SCI_GRAPHICS_GLYPHSOURCE_TTF_H
#define SCI_GRAPHICS_GLYPHSOURCE_TTF_H

#include <functional>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/types.h"
#include "sci/graphics/glyphsource.h"

namespace Graphics {
class Font;
}

namespace Sci {

/**
 * A live TrueType face, read through FreeType and exposed as a
 * UnicodeGlyphSource: glyphs are rasterised the first time they are asked
 * for, then cached forever (including a clean "the face lacks this code
 * point" miss), so create() never touches anything beyond the face itself
 * and the fixed probe set used to fit the baseline into the cell.
 *
 * Width is never taken from the TTF advance: it comes from the Unicode East
 * Asian Width property (isWide()), which is the only thing that agrees with
 * SCVMUNI's cells()==1|2 convention across scripts. A code point's presence
 * in the face, on the other hand, cannot be asked for directly - TTFFont
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
	 * Opens face 0 of stream at cell size pixelSize, fits the vertical
	 * offset from a fixed probe set, and rasterises nothing else. On success
	 * the source owns stream exactly when dispose is DisposeAfterUse::YES;
	 * on failure stream is disposed of the same way and null is returned
	 * with error filled in.
	 */
	static TtfGlyphSource *create(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
	                               int pixelSize, Common::String &error);
	~TtfGlyphSource() override;

	byte cellWidth() const override { return _cellWidth; }
	byte cellHeight() const override { return _cellHeight; }
	byte advanceNarrow() const override { return _cellWidth / 2; }
	byte advanceWide() const override { return _cellWidth; }
	int bitsPerPixel() const override { return 8; }
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	uint32 glyphCount() const override;

	/** FreeType renders done so far (probes at create() time, plus one per
	 *  distinct code point since); exposed for tests. */
	uint32 rasterCount() const { return _rasterCount; }

	/** Whether cp is East Asian Wide or Fullwidth, per the table generated
	 *  from Python's unicodedata (Unicode 16.0). Available even when this
	 *  build has no FreeType, since layout needs it independent of a face. */
	static bool isWide(uint32 cp);

	/**
	 * Picks a vertical-fit size, independent of any real font or FreeType
	 * state, so the raster-budget bound can be pinned in a unit test with a
	 * fake measure callback: starting at startSize, tries candidate sizes
	 * down to minSize (inclusive) by calling measure(trySize, top, bottom),
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
	                          const std::function<bool(int, int &, int &)> &measure,
	                          int &top, int &bottom);

private:
	TtfGlyphSource() {}

	struct Entry {
		byte cells = 0;
		Common::Array<byte> cov;
	};

	Entry &ensure(uint32 cp);

	Graphics::Font *_font = nullptr;
	Common::SeekableReadStream *_stream = nullptr;
	DisposeAfterUse::Flag _dispose = DisposeAfterUse::NO;

	byte _cellWidth = 0;
	byte _cellHeight = 0;
	int _yOffset = 0;
	uint32 _rasterCount = 0;

	Common::HashMap<uint32, Entry> _cache;
};

} // End of namespace Sci

#endif
