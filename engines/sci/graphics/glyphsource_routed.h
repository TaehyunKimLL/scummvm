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

#ifndef SCI_GRAPHICS_GLYPHSOURCE_ROUTED_H
#define SCI_GRAPHICS_GLYPHSOURCE_ROUTED_H

#include "sci/graphics/glyphsource.h"
#include "sci/graphics/textlatin.h"

namespace Sci {

/**
 * Routes a fixed range of code points to a second UnicodeGlyphSource - the
 * face hires_text_latin_font names - while everything else goes to the main
 * source (hires_text_font's face). Both sources must have been built at the
 * same pixel size; geometry (cellWidth/cellHeight/advanceNarrow/advanceWide/
 * bitsPerPixel) is reported from the main source only, so a caller sizing
 * layout never needs to know a second face exists.
 *
 * Which code points route to the latin source depends on the LatinMode this
 * was constructed with (see textlatin.h):
 *
 *   kLatinFullwidth - U+FF01..U+FF5E and U+3000, the fullwidth-forms range
 *                     GfxText16::glyphChar() remaps plain ASCII into via
 *                     TextCompose::latinFullwidth().
 *   kLatinHalf      - U+0020..U+007E, plain ASCII left unremapped at the code
 *                     point level, routed here instead by
 *                     GfxFontSet/GfxFontUnicodeAdapter choosing the Unicode
 *                     face for it in the first place (see
 *                     TextCompose::asciiGoesToUnicodeFace()).
 *
 * Never constructed with kLatinOff - callers only wrap two sources once
 * hires_text_latin has resolved to half or fullwidth (see cache.cpp).
 *
 * Owns both sources.
 */
class RoutedGlyphSource : public UnicodeGlyphSource {
public:
	RoutedGlyphSource(UnicodeGlyphSource *main, UnicodeGlyphSource *latin, LatinMode mode);
	~RoutedGlyphSource() override;

	byte cellWidth() const override { return _main->cellWidth(); }
	byte cellHeight() const override { return _main->cellHeight(); }
	byte advanceNarrow() const override { return _main->advanceNarrow(); }
	byte advanceWide() const override { return _main->advanceWide(); }
	int bitsPerPixel() const override { return _main->bitsPerPixel(); }

	/** Whether cp is in the range this mode routes to the latin source. */
	bool routeToLatin(uint32 cp) const;

	/**
	 * Whether cp is actually drawn by the latin source: it is in the routed
	 * range AND the latin face has a glyph for it. A Latin-only face (one
	 * with no fullwidth forms, say) then leaves those code points to the
	 * main face instead of drawing nothing. cells() and row() both decide
	 * through this, so they always agree on the source.
	 */
	bool useLatin(uint32 cp) {
		return routeToLatin(cp) && _latin->cells(cp) > 0;
	}

	int cells(uint32 cp) override {
		return useLatin(cp) ? _latin->cells(cp) : _main->cells(cp);
	}
	const byte *row(uint32 cp, int y) override {
		return useLatin(cp) ? _latin->row(cp, y) : _main->row(cp, y);
	}
	uint32 glyphCount() const override {
		return _main->glyphCount() + _latin->glyphCount();
	}

private:
	UnicodeGlyphSource *_main;
	UnicodeGlyphSource *_latin;
	LatinMode _mode;
};

} // End of namespace Sci

#endif
