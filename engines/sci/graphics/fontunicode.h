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
#include "common/ptr.h"
#include "common/str.h"
#include "common/str-enc.h"
#include "graphics/hires_text/font_map.h"
#include "sci/graphics/glyphsource.h"
#include "sci/graphics/scifont.h"
#include "sci/graphics/textlatin.h"

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

	/**
	 * Use an already-built source and mark the face loaded; the font owns
	 * it unless @p dispose is DisposeAfterUse::NO (a source GfxCache shares
	 * between fonts). name is used only for the debug line printed on
	 * success.
	 */
	void setSource(UnicodeGlyphSource *src, const Common::String &name,
				   DisposeAfterUse::Flag dispose = DisposeAfterUse::YES);

	bool isLoaded() const { return _loaded; }

	GuiResourceId getResourceId() override { return _resourceId; }
	byte getHeight() override { return _source ? _source->cellHeight() : 0; }

	/** True when this code point occupies two cells (East Asian W/F). */
	bool isDoubleByte(uint32 chr) override;

	byte getCharWidth(uint32 chr) override;
	byte getCharHeight(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput,
	                  byte *buffer, int16 width, int16 height) override;

	/** Does this font have a glyph for @p codepoint? */
	bool hasGlyph(uint32 codepoint) const { return _source && _source->cells(codepoint) > 0; }

	uint32 glyphCount() const { return _source ? _source->glyphCount() : 0; }

	/** The face's own advance for @p cp in hi-res pixels, 0 when unknown
	 *  (see UnicodeGlyphSource::advance()). */
	int advanceHires(uint32 cp) { return _source ? _source->advance(cp) : 0; }

	/** The packed row y of cp's glyph, for TextCompose::expandGlyphRow(). */
	const byte *coverageRow(uint32 cp, int y) { return _source ? _source->row(cp, y) : nullptr; }
	int bitsPerPixel() const { return _source ? _source->bitsPerPixel() : 1; }

private:
	GfxScreen *_screen;
	GuiResourceId _resourceId;
	bool _loaded;

	Common::DisposablePtr<UnicodeGlyphSource> _source;

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
	/**
	 * @param latinMode  this font id's Latin mode (see GfxFontSet), resolved
	 *                   by GfxCache. Only kLatinHalf/kLatinProportional
	 *                   change anything here: they route the printable
	 *                   ASCII range to _font instead of _fallback (see the
	 *                   chr < 0x80 checks below).
	 * @param fullwidthSpace  kLatinFullwidth: whether GfxText16 remaps ' '
	 *                   too; only carried, for GfxText16 to read.
	 * @param metrics    kLatinProportional: whose advance ASCII gets - the
	 *                   fallback font's (game) or the face's (font); see
	 *                   latinAdvanceGamePx().
	 */
	GfxFontUnicodeAdapter(GfxFontUnicode *font, Common::CodePage codePage,
	                      GfxFont *fallback, GuiResourceId resourceId,
	                      LatinMode latinMode = kLatinOff, bool fullwidthSpace = false,
	                      Graphics::HiResMetricsSource metrics = Graphics::kHiResMetricsGame);

	LatinMode latinMode() const { return _latinMode; }
	bool latinFullwidthSpace() const { return _fullwidthSpace; }
	~GfxFontUnicodeAdapter() override;

	GuiResourceId getResourceId() override { return _resourceId; }
	byte getHeight() override;
	bool isDoubleByte(uint32 chr) override;
	byte getCharWidth(uint32 chr) override;
	byte getCharHeight(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput,
	                  byte *buffer, int16 width, int16 height) override;

	/**
	 * hires_text_log: which face draw() would pick for @p chr - mirrors its
	 * choice read-only. See GfxFontSet::classify() and textlatin.h's
	 * TextFaceKind.
	 */
	TextFaceKind classify(uint32 chr) const;

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
	LatinMode _latinMode;
	bool _fullwidthSpace;
	Graphics::HiResMetricsSource _metrics;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_FONTUNICODE_H
