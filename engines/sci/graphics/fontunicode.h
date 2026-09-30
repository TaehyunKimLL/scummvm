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
#include "graphics/hires_text/glyph_source.h"
#include "sci/graphics/scifont.h"
#include "sci/graphics/textlatin.h"
#include "sci/graphics/textlayout16.h"

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
	void setSource(Graphics::UnicodeGlyphSource *src, const Common::String &name,
				   DisposeAfterUse::Flag dispose = DisposeAfterUse::YES);

	bool isLoaded() const { return _loaded; }

	/**
	 * hires_text.map [hires] missing=: draw @p boxCp for a code point the
	 * source lacks (Graphics::MissingGlyphSource), at most once per font.
	 * hasGlyph() and source() still answer for the source's own glyphs, so
	 * the box never stands before another face of a GfxFontSet; the set asks
	 * drawsMissing() once every face has declined a character.
	 */
	void setMissing(uint32 boxCp);

	/** Whether this font draws @p codepoint as the missing= box. */
	bool drawsMissing(uint32 codepoint) const {
		return _source && _source.get() != _own && !hasGlyph(codepoint) && _source->cells(codepoint) > 0;
	}

	/**
	 * Lay the face out in another cell than it is rasterised in, and move
	 * its glyphs (C41, GlyphPlacement). An inactive placement (the default)
	 * measures and draws by the source's own cell, as before.
	 */
	void setPlacement(const GlyphPlacement &p) { _placement = p; }
	const GlyphPlacement &placement() const { return _placement; }

	GuiResourceId getResourceId() override { return _resourceId; }
	byte getHeight() override { return _placement.active() ? (byte)_placement.cellPx : (_source ? _source->cellHeight() : 0); }

	/** True when this code point occupies two cells (East Asian W/F). */
	bool isDoubleByte(uint32 chr) override;

	byte getCharWidth(uint32 chr) override;
	byte getCharHeight(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput,
	                  byte *buffer, int16 width, int16 height) override;

	/**
	 * The advance rule of I18N_TEXT_DESIGN.md section 4.2, in game px:
	 * wide -> @p gameWide (the cell rule, unchanged); combining -> 0; any
	 * other glyph -> Graphics::advanceGamePx(kHiResAdvanceFont, @p gameNarrow,
	 * m.advance, @p scale). See Sci::gameAdvance() (textlayout16.h).
	 */
	static int16 gameAdvance(const Graphics::GlyphMetrics &m, int gameNarrow, int gameWide, int scale) {
		return Sci::gameAdvance(m, gameNarrow, gameWide, scale);
	}

	/**
	 * The advance of @p cp in game px when drawn at @p scale hi-res px per
	 * game px: glyphGameWidth() with perGlyph(). 0 when there is no glyph.
	 */
	byte gameCharWidth(uint32 cp, int scale);

	/**
	 * Per-glyph advance and placement (a UTF-8 translation is loaded).
	 * Off - the default - every glyph is measured and drawn by its cell,
	 * as before, so a legacy font keeps its widths to the pixel.
	 */
	void setPerGlyph(bool on) { _perGlyph = on; }
	bool perGlyph() const { return _perGlyph; }

	/** A string starts: a mark at its start does not attach to the last string's base. */
	void beginString() override { _anchor.reset(); }

	/** Does this font have a glyph for @p codepoint? */
	bool hasGlyph(uint32 codepoint) const { return _own && _own->cells(codepoint) > 0; }

	uint32 glyphCount() const { return _source ? _source->glyphCount() : 0; }

	/** The glyph source, still owned by this font (GfxCache chains the .uni bundle's behind TrueType faces). */
	Graphics::UnicodeGlyphSource *source() { return _own; }

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

	/// With missing= (setMissing()), the source set, kept here while
	/// _source is the box wrapped around it; empty otherwise.
	Common::DisposablePtr<Graphics::UnicodeGlyphSource> _ownHolder;
	/// What is drawn and measured: the source set, or the box around it.
	Common::DisposablePtr<Graphics::UnicodeGlyphSource> _source;
	/// The source set: its own glyphs, without the box.
	Graphics::UnicodeGlyphSource *_own;

	/** Scratch buffer for expanding a glyph to one byte per pixel. */
	Common::Array<byte> _glyphScratch;

	/// Per-glyph advance and placement: set for a UTF-8 translation only.
	bool _perGlyph;
	/// Layout cell and glyph offset (C41).
	GlyphPlacement _placement;
	/// Where a combining mark goes; reset by beginString().
	CombiningAnchor _anchor;
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
	 * @param plan  this font id's compiled plan (see GfxFontSet), resolved
	 *              by GfxCache: it decides which packed byte pairs
	 *              TextCompose::goesToUnicodeFace() routes to @p font instead
	 *              of @p fallback (see the chr < 0x80 checks below) and by
	 *              how much they advance (plan.advanceFor()).
	 * @param cell  the id's resolved layout cell (FontSettings::cell, hi-res
	 *              px): design 6.3's `advance=cell` grid, and the fallback
	 *              `advance=game`/`font` fall to when the game font has no
	 *              glyph for the game code at all.
	 */
	GfxFontUnicodeAdapter(GfxFontUnicode *font, Common::CodePage codePage,
	                      GfxFont *fallback, GuiResourceId resourceId,
	                      const Graphics::HiResIdPlan &plan = Graphics::HiResIdPlan(), int cell = 16);

	/** The plan this font id was built with; GfxText16::refreshTextPlan() reads it. */
	const Graphics::HiResIdPlan &plan() const { return _plan; }

	/** As GfxFontSet::setGameCode(): the game code design 6.3/6.7's `advance=game`/`font` measure on. */
	void setGameCode(uint32 code) { _gameCode = code; }

	~GfxFontUnicodeAdapter() override;

	GuiResourceId getResourceId() override { return _resourceId; }
	byte getHeight() override;
	bool isDoubleByte(uint32 chr) override;
	byte getCharWidth(uint32 chr) override;
	byte getCharHeight(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;
	void drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput,
	                  byte *buffer, int16 width, int16 height) override;
	void beginString() override;

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
	Graphics::HiResIdPlan _plan;
	int _cell;
	uint32 _gameCode = 0;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_FONTUNICODE_H
