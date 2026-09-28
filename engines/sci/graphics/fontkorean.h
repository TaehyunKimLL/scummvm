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

#ifndef SCI_GRAPHICS_FONTKOREAN_H
#define SCI_GRAPHICS_FONTKOREAN_H

#include "sci/graphics/helpers.h"

namespace Graphics {
class FontKorean;
}

namespace Sci {

/**
 * Special Font class, handles Korean inside sci games, uses ScummVM Korean support
 */
class GfxFontKorean : public GfxFont {
public:
	GfxFontKorean(GfxScreen *screen, GuiResourceId resourceId);
	~GfxFontKorean();

	GuiResourceId getResourceId();
	byte getHeight();
	bool isDoubleByte(uint32 chr);
	byte getCharWidth(uint32 chr);
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput);
#ifdef ENABLE_SCI32
	// SCI2/2.1 equivalent
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 width, int16 height);
#endif

	/**
	 * The byte pair korean.fnt is indexed by (lead byte low, trail byte
	 * high) for a character as GfxText16 hands it over. GfxText16 decodes
	 * code-page text to code points as it walks (readChar()), so a game
	 * whose scripts select font 1001 themselves - which gets this font
	 * directly, not inside a GfxFontSet that re-encodes for it - passes a
	 * Unicode code point here. Values that are already packed pairs (from a
	 * GfxFontSet, or undecodable bytes) do not encode to a CP949 pair and
	 * are returned unchanged, as is ASCII.
	 */
	static uint16 toFontCode(uint32 chr);

private:
	/** True when korean.fnt carries proportional Latin (format v4). */
	bool proportionalLatin() const;

	GfxScreen *_screen;
	GuiResourceId _resourceId;

	Graphics::FontKorean *_commonFont;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_FONTKOREAN_H
