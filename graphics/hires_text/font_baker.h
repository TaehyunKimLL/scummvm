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

#ifndef GRAPHICS_HIRES_TEXT_FONT_BAKER_H
#define GRAPHICS_HIRES_TEXT_FONT_BAKER_H

#include "common/scummsys.h"

#ifdef USE_FREETYPE2

#include "common/array.h"
#include "common/str.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

class Font;

/**
 * Bake a TrueType face into the SVFN bitmap font format, in memory.
 *
 * This is what lets a translation point at a .ttf and get exactly what a
 * pre-baked font would give: the result goes through HiResBitmapFont::load
 * like any file, so metrics, fallback, logging and the no-FreeType build all
 * see one kind of font. It is the run-time twin of the offline baking tool,
 * and is only compiled where FreeType is.
 *
 * The output is a version 2 font: it carries its own code point table, so
 * the glyph set is whatever @p codepoints lists, in that order.
 */
class HiResFontBaker {
public:
	/**
	 * @param face        a face loaded by loadTTFFont() at the wanted pixel
	 *                    size; not owned
	 * @param codepoints  which glyphs to bake; ones the face lacks are skipped
	 * @param cellW       cell width; 0 for the face's widest advance
	 * @param cellH       cell height; 0 for the face's line height
	 * @param proportional  record each glyph's advance, so the renderer may
	 *                    space text by the face rather than the cell
	 * @param out         receives the file image
	 * @return false when nothing could be baked
	 */
	static bool bake(const Font &face, const Common::Array<uint32> &codepoints,
					 int cellW, int cellH, bool proportional,
					 Common::Array<byte> &out);

	/// The 2350 KS X 1001 Hangul syllables, in code page order.
	static void hangulSyllables(Common::Array<uint32> &out);
	/// The 6879 JIS X 0208 characters reachable through CP932, in order.
	static void jisX0208(Common::Array<uint32> &out);
	/// Every code point a CP936 or CP950 two byte sequence decodes to.
	static void chineseCodePage(uint codePage, Common::Array<uint32> &out);
	/// 0x20..0x7E and 0xA0..0xFF as Latin-1.
	static void latin1(Common::Array<uint32> &out);
};

} // End of namespace Graphics

#endif // USE_FREETYPE2

#endif
