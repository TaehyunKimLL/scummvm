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

#ifndef SCI_GRAPHICS_FONTUNICODE_H
#define SCI_GRAPHICS_FONTUNICODE_H

#include "common/array.h"
#include "common/str.h"
#include "common/str-enc.h"
#include "sci/graphics/scifont.h"

namespace Sci {

class GfxScreen;

/**
 * A SCVMUNI bitmap font: glyphs addressed by Unicode code point.
 *
 * The existing Korean font (SCVMSJIS, GfxFontKorean) indexes its glyph array
 * as `codepoint - 0xAC00`, so it holds exactly the 11,172 pre-composed hangul
 * syllables and nothing else. That is why Graphics::checkKorCode() only
 * accepts lead bytes 0xB0-0xC8: widening the gate without changing the
 * indexing would compute a negative offset and read outside the array. Text
 * containing hanja, full-width punctuation, full-width alphanumerics or
 * standalone jamo therefore never switched to the Korean font at all and was
 * drawn as empty boxes by the single-byte font.
 *
 * SCVMUNI replaces that arithmetic with an explicit sorted code point table,
 * so the representable set is whatever the font was built with, in any
 * script, and an absent glyph is a clean lookup miss. Bundles are produced by
 * harness/i18n/m7mkfont.py, which verifies what it writes.
 */
class GfxFontUnicode : public GfxFont {
public:
	GfxFontUnicode(GfxScreen *screen, GuiResourceId resourceId);
	~GfxFontUnicode() override;

	/** Load a bundle by filename. False leaves the object unusable. */
	bool load(const Common::String &filename);

	bool isLoaded() const { return _loaded; }

	GuiResourceId getResourceId() override { return _resourceId; }
	byte getHeight() override { return _cellHeight; }

	/** True when this code point occupies two cells (East Asian W/F). */
	bool isDoubleByte(uint32 chr) override;

	byte getCharWidth(uint32 chr) override;
	byte getCharHeight(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput,
	                  byte *buffer, int16 width, int16 height) override;

	/** Does this font have a glyph for @p codepoint? */
	bool hasGlyph(uint32 codepoint) const { return findGlyph(codepoint) >= 0; }

	uint32 glyphCount() const { return _glyphCount; }

	/**
	 * The ISO language tag the font declares, or empty when it declares none.
	 * Older fonts predate the field and are silently unset.
	 */
	const Common::String &language() const { return _language; }

private:
	/** Binary search of the sorted code point table; -1 when absent. */
	int findGlyph(uint32 codepoint) const;

	/** Is this pixel of the glyph set? Handles both 1bpp and 2bpp. */
	bool pixelSet(int glyph, int x, int y) const;

	GfxScreen *_screen;
	GuiResourceId _resourceId;
	bool _loaded;

	Common::Array<byte> _data;

	const byte *_codepoints;	// glyphCount x uint32 LE, ascending
	const byte *_widths;		// glyphCount x uint8, 1 or 2 cells
	const byte *_bitmaps;		// glyphCount x _bytesPerGlyph

	uint32 _glyphCount;
	Common::String _language;
	byte _cellWidth;
	byte _cellHeight;
	byte _advanceNarrow;
	byte _advanceWide;
	byte _bitsPerPixel;
	uint32 _rowBytes;
	uint32 _bytesPerGlyph;

	/** Scratch buffer for expanding a glyph to one byte per pixel. */
	Common::Array<byte> _glyphScratch;
};

/**
 * Presents a GfxFontUnicode to the byte-oriented text renderer.
 *
 * GfxText16 assembles a packed byte pair - lead byte in the low half, trail
 * byte in the high half - and its line-wrapping arithmetic counts BYTES:
 * GetLongest() returns a byte count that callers use to index the original
 * string. Rewriting that contract would touch every SCI game the engine
 * supports.
 *
 * So the byte string stays the transport and the conversion happens here, at
 * the font boundary: this adapter accepts the packed pair the renderer already
 * produces, decodes it to a Unicode code point using the game's code page, and
 * delegates to GfxFontUnicode. Wrapping arithmetic, byte counts and the
 * script-visible representation are all untouched - which is what M4's
 * measurement requires, since a real SCI0 game walks dialogue bytes.
 *
 * Widths are reported from the wrapped font but clamped to the cell geometry
 * the renderer assumes, so a code-point font cannot change where text wraps.
 */
class GfxFontUnicodeAdapter : public GfxFont {
public:
	GfxFontUnicodeAdapter(GfxFontUnicode *font, Common::CodePage codePage,
	                      GfxFont *fallback, GuiResourceId resourceId);
	~GfxFontUnicodeAdapter() override;

	GuiResourceId getResourceId() override { return _resourceId; }
	byte getHeight() override;
	bool isDoubleByte(uint32 chr) override;
	byte getCharWidth(uint32 chr) override;
	byte getCharHeight(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput,
	                  byte *buffer, int16 width, int16 height) override;

private:
	/**
	 * Decode a packed byte pair into a Unicode code point.
	 *
	 * Returns 0 when the bytes do not form a character in this code page,
	 * which the callers treat as "no glyph" rather than guessing.
	 */
	uint32 toCodePoint(uint32 packed) const;

	GfxFontUnicode *_font;
	GfxFont *_fallback;
	Common::CodePage _codePage;
	GuiResourceId _resourceId;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_FONTUNICODE_H
