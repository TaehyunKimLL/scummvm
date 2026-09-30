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

#include "sci/graphics/fontunicode.h"
#include "sci/graphics/fontkorean.h"
#include "sci/graphics/fontsjis.h"
#include "graphics/hires_text/glyph_source_file.h"
#include "graphics/hires_text/glyph_source_missing.h"
#include "graphics/hires_text/glyph_source_scvmuni.h"
#include "graphics/hires_text/latin_advance.h"
#include "graphics/hires_text/unicode_props.h"
#include "sci/graphics/ports.h"
#include "sci/graphics/screen.h"
#include "graphics/hires_text/text_compose.h"
#include "sci/graphics/textlatin.h"
#include "sci/sci.h"
#include "sci/utf8.h"

#include "common/file.h"
#include "common/textconsole.h"
#include "common/util.h"

namespace Sci {

GfxFontUnicode::GfxFontUnicode(GfxScreen *screen, GuiResourceId resourceId)
	: _screen(screen), _resourceId(resourceId), _loaded(false), _ownHolder(nullptr, DisposeAfterUse::NO),
	  _source(nullptr, DisposeAfterUse::YES), _own(nullptr), _perGlyph(false) {
}

GfxFontUnicode::~GfxFontUnicode() {
}

bool GfxFontUnicode::load(const Common::String &filename) {
	Common::File *f = new Common::File();
	if (!f->open(Common::Path(filename))) {
		delete f;
		return false;
	}

	// An SVFN bitmap font under a .uni name serves as the bundle too.
	byte head[4];
	const uint32 got = f->read(head, sizeof(head));
	Common::String error;
	Graphics::UnicodeGlyphSource *src;
	if (Graphics::isSvfnFile(head, got)) {
		f->seek(0);
		src = Graphics::createSvfnSource(*f, error);
		delete f;
		if (!src)
			error = filename + ": " + error;
	} else {
		// A SCVMUNI bundle's glyphs are read when one is first drawn: often
		// the bundle only stands behind the faces a map names, and korean.uni
		// is 1.4 MB. The source keeps the file until then.
		src = Graphics::ScvmuniGlyphSource::createDeferred(f, DisposeAfterUse::YES, filename, error);
	}
	if (!src) {
		warning("GfxFontUnicode: %s", error.c_str());
		return false;
	}

	setSource(src, filename);
	return true;
}

void GfxFontUnicode::setSource(Graphics::UnicodeGlyphSource *src, const Common::String &name,
							   DisposeAfterUse::Flag dispose) {
	_ownHolder.reset(nullptr, DisposeAfterUse::NO);
	_source.reset(src, dispose);
	_own = src;
	_loaded = true;
	debug(1, "GfxFontUnicode: %s loaded, %u glyphs, %dx%d, %dbpp",
		  name.c_str(), src->glyphCount(), src->cellWidth(), src->cellHeight(), src->bitsPerPixel());
}

void GfxFontUnicode::setMissing(uint32 boxCp) {
	if (!boxCp || !_own || _source.get() != _own)
		return;
	// The source set moves to _ownHolder, keeping its ownership; the box
	// only points at it and goes first (declared after it).
	_ownHolder = Common::move(_source);
	_source.reset(new Graphics::MissingGlyphSource(_own, boxCp, DisposeAfterUse::NO), DisposeAfterUse::YES);
}

bool GfxFontUnicode::isDoubleByte(uint32 chr) {
	return _source && _source->cells(chr) == 2;
}

byte GfxFontUnicode::getCharWidth(uint32 chr) {
	if (!_source)
		return 0;
	const int cells = _source->cells(chr);
	if (cells <= 0)
		return 0;
	if (_placement.active())
		return (byte)(cells == 2 ? _placement.cellPx : _placement.cellPx / 2);
	return cells == 2 ? _source->advanceWide() : _source->advanceNarrow();
}

byte GfxFontUnicode::gameCharWidth(uint32 cp, int scale) {
	return (byte)glyphGameWidth(_source.get(), cp, scale, _perGlyph, _placement.active() ? _placement.cellPx : 0);
}

byte GfxFontUnicode::getCharHeight(uint32 chr) {
	if (!_source || _source->cells(chr) <= 0)
		return 0;
	return _placement.active() ? (byte)_placement.cellPx : _source->cellHeight();
}

void GfxFontUnicode::draw(uint32 chr, int16 top, int16 left, byte color,
						  bool greyedOutput) {
	if (!_source)
		return;
	const int cells = _source->cells(chr);
	if (cells <= 0)
		return;

	const int cellHeight = _source->cellHeight();
	const int bpp = _source->bitsPerPixel();
	const int w = _source->cellWidth() * cells;

	// Double-byte glyphs are drawn on the text layer at twice the lowres
	// coordinates, exactly as GfxFontKorean does via putHangulChar. Writing
	// lowres pixels here instead renders nothing visible: the upscaled
	// background is composited over them. That was measured - the glyph draw
	// calls arrived with correct code points and coordinates while the screen
	// stayed blank.
	//
	// Expand to one byte per pixel of coverage, the convention the text layer
	// expects.
	_glyphScratch.resize((uint)w * cellHeight);
	byte *cov = _glyphScratch.begin();
	for (int y = 0; y < cellHeight; y++)
		Graphics::TextCompose::expandGlyphRow(cov + y * w, coverageRow(chr, y), w, bpp, greyedOutput, top + y, left);

	// Placement (design section 4.2): the pen is at column originX of the
	// row, so the glyph is drawn originX px left of the pen - ink left of
	// the origin is not lost. A combining mark takes the pen the previous
	// base left, in hi-res px, and moves nothing. A glyph with neither (every
	// glyph of a SCVMUNI bundle, every Hangul, kana and Latin glyph of the
	// faces measured) is drawn exactly where it always was.
	// Only for a UTF-8 translation (_perGlyph): a legacy font's glyphs are
	// drawn where they always were.
	int hiresX = left << 1;
	if (_perGlyph) {
		Graphics::GlyphMetrics m;
		const bool placed = _source->metrics(chr, m);
		hiresX = _anchor.place(m, placed, left, top, placed && !m.combining ? gameCharWidth(chr, 2) : 0);
	}
	if (_placement.active()) {
		// C41: the raster cell moved within the layout cell. What leaves the
		// cell sideways is drawn over the neighbouring pixels, not clipped -
		// only at the edge of the port (or window) the text is drawn in.
		// Above and below, the ink is clipped to the text line: the game
		// erases a line by its rows only, so a row of ink above or below
		// it (an 18-row face drawn a row up in a 16-row line) would stay on
		// screen as stray dots once the text is gone.
		int lineTop, lineBottom;
		const int gameLine = (g_sci && g_sci->_gfxPorts && g_sci->_gfxPorts->_curPort) ?
			g_sci->_gfxPorts->_curPort->fontHeight << 1 : 0;
		_placement.lineRows(top << 1, gameLine, lineTop, lineBottom);
		const Common::Rect line(-0x4000, lineTop, 0x4000, lineBottom);
		_screen->putHiresCoverageGlyphAt(cov, w, cellHeight, hiresX + _placement.offsetX(cells == 2),
										 (top << 1) + _placement.offsetY(), color, true, &line);
		return;
	}
	if (hiresX == (left << 1))
		_screen->putHiresCoverageGlyph(cov, w, cellHeight, left, top, color);
	else
		_screen->putHiresCoverageGlyphAt(cov, w, cellHeight, hiresX, top << 1, color);
}

void GfxFontUnicode::drawToBuffer(uint32 chr, int16 top, int16 left, byte color,
								  bool greyedOutput, byte *buffer,
								  int16 width, int16 height) {
	if (!_source)
		return;
	const int cells = _source->cells(chr);
	if (cells <= 0)
		return;

	const int cellHeight = _source->cellHeight();
	const int bpp = _source->bitsPerPixel();
	const int w = _source->cellWidth() * cells;

	const int offX = _placement.active() ? _placement.offsetX(cells == 2) : 0;
	const int offY = _placement.active() ? _placement.offsetY() : 0;
	for (int y = 0; y < cellHeight; y++) {
		const int destY = top + offY + y;
		if (destY < 0 || destY >= height)
			continue;
		const byte *row = coverageRow(chr, y);
		for (int x = 0; x < w; x++) {
			const int destX = left + offX + x;
			if (destX < 0 || destX >= width)
				continue;
			if (Graphics::TextCompose::expandCoverage(row, x, bpp) == 0)
				continue;
			if (greyedOutput && (destY % 2) == (destX % 2))
				continue;
			buffer[destY * width + destX] = color;
		}
	}
}

TextFaceKind GfxFontUnicodeAdapter::classify(uint32 chr) const {
	// This adapter only ever exists for the hard-coded legacy ids (900/1001),
	// so its two non-Unicode outcomes are: the legacy CJK font when the game
	// shipped korean.fnt/SJIS.FNT, or the resource font otherwise (see
	// GfxCache::createUnicodeFont()).
	const bool fallbackIsLegacy = _fallback &&
		(dynamic_cast<GfxFontKorean *>(_fallback) || dynamic_cast<GfxFontSjis *>(_fallback));

	// chr is TextCompose::glyphCode()'s own result; see GfxFontSet::faceFor()
	// for what each range means.
	if (chr >= Graphics::kHiResGameCodeBase)
		return fallbackIsLegacy ? kTextFaceLegacy : kTextFaceResource;
	if (chr >= Graphics::kHiResTargetBase)
		return _font->hasGlyph(chr) ? kTextFaceUnicode : (fallbackIsLegacy ? kTextFaceLegacy : kTextFaceResource);

	if (chr < 0x80 && _fallback && !TextCompose::goesToUnicodeFace(_plan, chr))
		return fallbackIsLegacy ? kTextFaceLegacy : kTextFaceResource;

	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		// design 6.2: a range rule sent this here rather than the id's own
		// plain chain - see GfxFontSet::classify() for the same check.
		if (chr < Graphics::kHiResTargetBase && _plan.faceRules.lookup(chr) >= 0)
			return kTextFaceRule;
		return kTextFaceUnicode;
	}

	return fallbackIsLegacy ? kTextFaceLegacy : kTextFaceResource;
}

GfxFontUnicodeAdapter::GfxFontUnicodeAdapter(GfxFontUnicode *font,
											 Common::CodePage codePage,
											 GfxFont *fallback,
											 GuiResourceId resourceId,
											 const Graphics::HiResIdPlan &plan,
											 int cell)
	: _font(font), _fallback(fallback), _codePage(codePage),
	  _resourceId(resourceId), _plan(plan), _cell(cell) {
}

GfxFontUnicodeAdapter::~GfxFontUnicodeAdapter() {
	// Neither the wrapped font nor the fallback is owned: both live in the
	// font cache, which deletes them.
}

uint32 GfxFontUnicodeAdapter::toCodePoint(uint32 packed) const {
	if (packed < 0x80)
		return packed;	// ASCII is the same in every code page we handle

	if (packed > 0xFF) {
		// GfxText16 packs the LEAD byte in the low half and the trail byte in
		// the high half - reversed relative to the encoding - so undo that
		// here rather than anywhere else.
		return decodeCodePagePair(packed & 0xFF, (packed >> 8) & 0xFF, _codePage);
	}

	Common::String bytes;
	bytes += (char)packed;
	const Common::U32String decoded = bytes.decode(_codePage);
	if (decoded.empty())
		return 0;
	return decoded[0];
}

byte GfxFontUnicodeAdapter::getHeight() {
	// Line spacing must stay the wrapped game font's, otherwise every font the
	// game uses collapses to one height. Measured: reporting the bundle's cell
	// height made fonts of 12 and 9 both report 8.
	if (_fallback)
		return _fallback->getHeight();
	const byte h = _font->getHeight();
	return (getSciVersion() >= SCI_VERSION_2) ? h : (h >> 1);
}

bool GfxFontUnicodeAdapter::isDoubleByte(uint32 chr) {
	// Answered from the CODE PAGE, not from the fallback font and not from
	// glyph coverage. The renderer calls this with a single lead byte to
	// decide whether to fetch a second one, long before a code point exists.
	//
	// Delegating to the fallback was wrong and visibly so: with a Japanese
	// bundle the fallback is the game's own resource font, which reports
	// false for everything, so "ゲーム開始" was walked one byte at a time and
	// drawn as "?Q?[???J?n" - exactly the garbage that appeared on screen.
	if (chr > 0xFF)
		return true;	// already a packed pair
	const byte b = chr & 0xFF;
	switch (_codePage) {
	case Common::kWindows932:	// Shift-JIS
		return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
	case Common::kWindows949:	// EUC-KR / UHC
		return b >= 0x81 && b <= 0xFE;
	case Common::kWindows936:	// GBK
	case Common::kWindows950:	// Big5
		return b >= 0x81 && b <= 0xFE;
	default:
		return false;
	}
}

byte GfxFontUnicodeAdapter::getCharWidth(uint32 chr) {
	// chr is TextCompose::glyphCode()'s own result; see GfxFontSet::faceFor()
	// for what each range means. A declined target has no game code to
	// recover, so it is simply unmeasured, as GfxFontSet's own faceFor() does.
	if (chr >= Graphics::kHiResGameCodeBase)
		return _fallback ? _fallback->getCharWidth(chr - Graphics::kHiResGameCodeBase) : 0;
	if (chr >= Graphics::kHiResTargetBase) {
		if (!_font->hasGlyph(chr))
			return 0;
		const byte w = _font->getCharWidth(chr);
		return (getSciVersion() >= SCI_VERSION_2) ? w : (w >> 1);
	}

	// Single-byte characters stay with the game's own font. The Unicode
	// bundle has Latin glyphs too, but using them would change the metrics of
	// every English string in every font the game uses - measured, it made
	// fonts of height 12 and 9 all report 8 and pushed menu text outside its
	// button. The bundle is for characters a range rule deliberately routes
	// here instead (design 6.2).
	if (chr < 0x80 && _fallback && !TextCompose::goesToUnicodeFace(_plan, chr))
		return _fallback->getCharWidth(chr);

	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		const Graphics::HiResAdvance rule = _plan.advanceFor(cp);
		const int scale = (getSciVersion() >= SCI_VERSION_2) ? 1 : 2;
		if (rule == Graphics::kHiResAdvanceGame || rule == Graphics::kHiResAdvanceFont) {
			// design 6.3/6.7: the game font's own width for the game code -
			// _gameCode (setGameCode(), GfxText16::glyphChar()'s own @p chr
			// before any remap or target substitution), never @p chr itself,
			// which for a fullwidth remap or a target is not a code the game
			// font has anything for. Where the game font has no glyph for it
			// either, design 6.3 falls to the cell.
			int gameWidth = _fallback ? _fallback->getCharWidth(_gameCode) : 0;
			if (gameWidth <= 0) {
				const int raw = Graphics::Unicode::isWide(cp) ? _cell : _cell / 2;
				gameWidth = MAX(1, raw / scale);
			}
			return (byte)Graphics::advanceGamePx(rule, gameWidth, _font->advanceHires(cp), scale);
		}
		if (rule == Graphics::kHiResAdvanceCell) {
			const int raw = Graphics::Unicode::isWide(cp) ? _cell : _cell / 2;
			return (byte)MAX(1, raw / scale);
		}
		// The glyph is drawn on the hires plane at twice the lowres
		// coordinates, so its advance must be reported halved - exactly what
		// GfxFontKorean::getCharWidth does with `>> 1` below SCI2. Reporting
		// the full width makes text run past its box; that was measured, with
		// the last two syllables of a menu entry spilling outside the button.
		// design section 8's engine default: the cell for a wide glyph,
		// nothing for a combining mark, the face's advance otherwise
		// (Sci::glyphGameWidth(), only for a UTF-8 translation's per-glyph
		// layout; a legacy font keeps its cell widths to the pixel).
		const byte w = _font->getCharWidth(cp);
		if (chr >= 0x80 && _font->perGlyph())
			return _font->gameCharWidth(cp, scale);
		return (getSciVersion() >= SCI_VERSION_2) ? w : (w >> 1);
	}
	if (_fallback)
		return _fallback->getCharWidth(chr);
	return 0;
}

byte GfxFontUnicodeAdapter::getCharHeight(uint32 chr) {
	if (chr >= Graphics::kHiResGameCodeBase)
		return _fallback ? _fallback->getCharHeight(chr - Graphics::kHiResGameCodeBase) : 0;
	if (chr >= Graphics::kHiResTargetBase) {
		if (!_font->hasGlyph(chr))
			return 0;
		const byte h = _font->getCharHeight(chr);
		return (getSciVersion() >= SCI_VERSION_2) ? h : (h >> 1);
	}
	if (chr < 0x80 && _fallback && !TextCompose::goesToUnicodeFace(_plan, chr))
		return _fallback->getCharHeight(chr);

	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		const byte h = _font->getCharHeight(cp);
		return (getSciVersion() >= SCI_VERSION_2) ? h : (h >> 1);
	}
	if (_fallback)
		return _fallback->getCharHeight(chr);
	return 0;
}

void GfxFontUnicodeAdapter::draw(uint32 chr, int16 top, int16 left, byte color,
								 bool greyedOutput) {
	if (chr >= Graphics::kHiResGameCodeBase) {
		if (_fallback)
			_fallback->draw(chr - Graphics::kHiResGameCodeBase, top, left, color, greyedOutput);
		return;
	}
	if (chr >= Graphics::kHiResTargetBase) {
		if (_font->hasGlyph(chr))
			_font->draw(chr, top, left, color, greyedOutput);
		return;
	}
	// Keep single-byte text pixel-identical to the unmodified engine - except
	// where a range rule routes it here; see getCharWidth().
	if (chr < 0x80 && _fallback && !TextCompose::goesToUnicodeFace(_plan, chr)) {
		_fallback->draw(chr, top, left, color, greyedOutput);
		return;
	}
	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		_font->draw(cp, top, left, color, greyedOutput);
		return;
	}
	// No glyph: fall back rather than drawing nothing, so a font with partial
	// coverage degrades to the old rendering instead of to blank space.
	if (_fallback)
		_fallback->draw(chr, top, left, color, greyedOutput);
}

void GfxFontUnicodeAdapter::beginString() {
	_font->beginString();
	if (_fallback)
		_fallback->beginString();
}

void GfxFontUnicodeAdapter::drawToBuffer(uint32 chr, int16 top, int16 left,
										 byte color, bool greyedOutput,
										 byte *buffer, int16 width, int16 height) {
	if (chr >= Graphics::kHiResGameCodeBase) {
		if (_fallback)
			_fallback->drawToBuffer(chr - Graphics::kHiResGameCodeBase, top, left, color, greyedOutput, buffer, width, height);
		return;
	}
	if (chr >= Graphics::kHiResTargetBase) {
		if (_font->hasGlyph(chr))
			_font->drawToBuffer(chr, top, left, color, greyedOutput, buffer, width, height);
		return;
	}
	if (chr < 0x80 && _fallback && !TextCompose::goesToUnicodeFace(_plan, chr)) {
		_fallback->drawToBuffer(chr, top, left, color, greyedOutput, buffer, width, height);
		return;
	}
	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		_font->drawToBuffer(cp, top, left, color, greyedOutput, buffer, width, height);
		return;
	}
	if (_fallback)
		_fallback->drawToBuffer(chr, top, left, color, greyedOutput, buffer, width, height);
}

} // End of namespace Sci
