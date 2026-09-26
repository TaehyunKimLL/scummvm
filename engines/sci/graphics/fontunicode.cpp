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
#include "sci/graphics/glyphsource_scvmuni.h"
#include "sci/graphics/latinadvance.h"
#include "sci/graphics/screen.h"
#include "sci/graphics/textcompose.h"
#include "sci/graphics/textlatin.h"
#include "sci/sci.h"

#include "common/file.h"
#include "common/textconsole.h"
#include "common/util.h"

namespace Sci {

GfxFontUnicode::GfxFontUnicode(GfxScreen *screen, GuiResourceId resourceId)
	: _screen(screen), _resourceId(resourceId), _loaded(false), _source(nullptr, DisposeAfterUse::YES) {
}

GfxFontUnicode::~GfxFontUnicode() {
}

bool GfxFontUnicode::load(const Common::String &filename) {
	Common::File f;
	if (!f.open(Common::Path(filename)))
		return false;

	const uint32 size = f.size();
	Common::Array<byte> data(size);
	if (size > 0 && f.read(&data[0], size) != size) {
		warning("GfxFontUnicode: could not read %s", filename.c_str());
		return false;
	}

	Common::String error;
	UnicodeGlyphSource *src = ScvmuniGlyphSource::create(Common::move(data), filename, error);
	if (!src) {
		warning("GfxFontUnicode: %s", error.c_str());
		return false;
	}

	setSource(src, filename);
	return true;
}

void GfxFontUnicode::setSource(UnicodeGlyphSource *src, const Common::String &name,
							   DisposeAfterUse::Flag dispose) {
	_source.reset(src, dispose);
	_loaded = true;
	debug(1, "GfxFontUnicode: %s loaded, %u glyphs, %dx%d, %dbpp",
		  name.c_str(), src->glyphCount(), src->cellWidth(), src->cellHeight(), src->bitsPerPixel());
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
	return cells == 2 ? _source->advanceWide() : _source->advanceNarrow();
}

byte GfxFontUnicode::getCharHeight(uint32 chr) {
	return (_source && _source->cells(chr) > 0) ? _source->cellHeight() : 0;
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
		TextCompose::expandGlyphRow(cov + y * w, coverageRow(chr, y), w, bpp, greyedOutput, top + y, left);
	_screen->putHiresCoverageGlyph(cov, w, cellHeight, left, top, color);
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

	for (int y = 0; y < cellHeight; y++) {
		const int destY = top + y;
		if (destY < 0 || destY >= height)
			continue;
		const byte *row = coverageRow(chr, y);
		for (int x = 0; x < w; x++) {
			const int destX = left + x;
			if (destX < 0 || destX >= width)
				continue;
			if (TextCompose::expandCoverage(row, x, bpp) == 0)
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

	if (chr < 0x80 && _fallback && !TextCompose::asciiGoesToUnicodeFace(chr, _latinMode))
		return fallbackIsLegacy ? kTextFaceLegacy : kTextFaceResource;

	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		// ASCII (hires_text_latin=half) or its fullwidth remap
		// (hires_text_latin=fullwidth) drawn by the Unicode bundle - see
		// GfxFontSet::classify() for the same two checks.
		if (chr < 0x80 && TextCompose::asciiGoesToUnicodeFace(chr, _latinMode))
			return kTextFaceLatin;
		if (_latinMode == kLatinFullwidth &&
			((chr >= 0xFF01 && chr <= 0xFF5E) || chr == 0x3000))
			return kTextFaceLatin;
		return kTextFaceUnicode;
	}

	return fallbackIsLegacy ? kTextFaceLegacy : kTextFaceResource;
}

GfxFontUnicodeAdapter::GfxFontUnicodeAdapter(GfxFontUnicode *font,
											 Common::CodePage codePage,
											 GfxFont *fallback,
											 GuiResourceId resourceId,
											 LatinMode latinMode,
											 bool fullwidthSpace,
											 Graphics::HiResMetricsSource metrics)
	: _font(font), _fallback(fallback), _codePage(codePage),
	  _resourceId(resourceId), _latinMode(latinMode), _fullwidthSpace(fullwidthSpace),
	  _metrics(metrics) {
}

GfxFontUnicodeAdapter::~GfxFontUnicodeAdapter() {
	// Neither the wrapped font nor the fallback is owned: both live in the
	// font cache, which deletes them.
}

uint32 GfxFontUnicodeAdapter::toCodePoint(uint32 packed) const {
	if (packed < 0x80)
		return packed;	// ASCII is the same in every code page we handle

	Common::String bytes;
	if (packed > 0xFF) {
		// GfxText16 packs the LEAD byte in the low half and the trail byte in
		// the high half - reversed relative to the encoding - so undo that
		// here rather than anywhere else.
		bytes += (char)(packed & 0xFF);
		bytes += (char)((packed >> 8) & 0xFF);
	} else {
		bytes += (char)packed;
	}

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
	// Single-byte characters stay with the game's own font. The Unicode
	// bundle has Latin glyphs too, but using them would change the metrics of
	// every English string in every font the game uses - measured, it made
	// fonts of height 12 and 9 all report 8 and pushed menu text outside its
	// button. The bundle is for characters the resource font cannot draw -
	// except in kLatinHalf mode, which deliberately asks the printable ASCII
	// range to be drawn narrow by the Unicode/TrueType face instead.
	if (chr < 0x80 && _fallback && !TextCompose::asciiGoesToUnicodeFace(chr, _latinMode))
		return _fallback->getCharWidth(chr);

	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		const byte w = _font->getCharWidth(cp);
		if (_latinMode == kLatinProportional &&
			TextCompose::asciiGoesToUnicodeFace(chr, _latinMode)) {
			// hires_text_latin=proportional: the fallback (the game's font)
			// sets the advance, or the face's own does (metrics=font). With
			// no fallback, the narrow cell stands in for the game's width.
			// draw() needs no counterpart: GfxText16 advances the pen by
			// this, and the glyph's origin is at the start of its cell.
			// The range is GfxFontSet::getCharWidth()'s: the ASCII routed
			// to this face.
			const int scale = (getSciVersion() >= SCI_VERSION_2) ? 1 : 2;
			const int gameWidth = _fallback ? _fallback->getCharWidth(chr) : w / scale;
			return (byte)latinAdvanceGamePx(_metrics, gameWidth, _font->advanceHires(cp), scale);
		}
		// The glyph is drawn on the hires plane at twice the lowres
		// coordinates, so its advance must be reported halved - exactly what
		// GfxFontKorean::getCharWidth does with `>> 1` below SCI2. Reporting
		// the full width makes text run past its box; that was measured, with
		// the last two syllables of a menu entry spilling outside the button.
		return (getSciVersion() >= SCI_VERSION_2) ? w : (w >> 1);
	}
	if (_fallback)
		return _fallback->getCharWidth(chr);
	return 0;
}

byte GfxFontUnicodeAdapter::getCharHeight(uint32 chr) {
	if (chr < 0x80 && _fallback && !TextCompose::asciiGoesToUnicodeFace(chr, _latinMode))
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
	// Keep single-byte text pixel-identical to the unmodified engine - except
	// in kLatinHalf mode; see getCharWidth().
	if (chr < 0x80 && _fallback && !TextCompose::asciiGoesToUnicodeFace(chr, _latinMode)) {
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

void GfxFontUnicodeAdapter::drawToBuffer(uint32 chr, int16 top, int16 left,
										 byte color, bool greyedOutput,
										 byte *buffer, int16 width, int16 height) {
	if (chr < 0x80 && _fallback && !TextCompose::asciiGoesToUnicodeFace(chr, _latinMode)) {
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
