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
#include "sci/graphics/screen.h"
#include "sci/sci.h"

#include "common/file.h"
#include "common/textconsole.h"

namespace Sci {

static const char kMagic[8] = { 'S', 'C', 'V', 'M', 'U', 'N', 'I', 0 };
static const uint16 kVersion = 1;
static const uint32 kHeaderSize = 36;

GfxFontUnicode::GfxFontUnicode(GfxScreen *screen, GuiResourceId resourceId)
	: _screen(screen), _resourceId(resourceId), _loaded(false),
	  _codepoints(nullptr), _widths(nullptr), _bitmaps(nullptr),
	  _glyphCount(0), _cellWidth(0), _cellHeight(0),
	  _advanceNarrow(0), _advanceWide(0), _bitsPerPixel(1),
	  _rowBytes(0), _bytesPerGlyph(0) {
}

GfxFontUnicode::~GfxFontUnicode() {
}

bool GfxFontUnicode::load(const Common::String &filename) {
	Common::File f;
	if (!f.open(Common::Path(filename)))
		return false;

	const uint32 size = f.size();
	if (size < kHeaderSize) {
		warning("GfxFontUnicode: %s is too small", filename.c_str());
		return false;
	}

	_data.resize(size);
	if (f.read(&_data[0], size) != size) {
		warning("GfxFontUnicode: could not read %s", filename.c_str());
		_data.clear();
		return false;
	}
	const byte *d = &_data[0];

	if (memcmp(d, kMagic, 8) != 0) {
		warning("GfxFontUnicode: %s has a bad signature", filename.c_str());
		_data.clear();
		return false;
	}

	const uint16 version = READ_LE_UINT16(d + 8);
	const uint16 flags = READ_LE_UINT16(d + 10);
	if (version != kVersion) {
		warning("GfxFontUnicode: unsupported version %u", version);
		_data.clear();
		return false;
	}

	_cellWidth = d[12];
	_cellHeight = d[13];
	_advanceNarrow = d[14];
	_advanceWide = d[15];
	_glyphCount = READ_LE_UINT32(d + 16);

	const uint32 cpOff = READ_LE_UINT32(d + 20);
	const uint32 wOff = READ_LE_UINT32(d + 24);
	const uint32 bmOff = READ_LE_UINT32(d + 28);

	_bitsPerPixel = (flags & 1) ? 2 : 1;
	// A wide glyph spans two cells and every glyph uses the same stride, so
	// one row length serves both widths and the reader stays branch-free.
	_rowBytes = ((uint32)_cellWidth * 2 * _bitsPerPixel + 7) / 8;
	_bytesPerGlyph = _rowBytes * _cellHeight;

	if (_cellWidth == 0 || _cellHeight == 0 || _glyphCount == 0) {
		warning("GfxFontUnicode: %s declares an empty font", filename.c_str());
		_data.clear();
		return false;
	}

	// Every table must lie inside the file. Checked before any table is read
	// so a truncated or hostile bundle cannot walk off the buffer.
	if (cpOff > size || (uint64)_glyphCount * 4 > size - cpOff ||
		wOff > size || (uint64)_glyphCount > size - wOff ||
		bmOff > size || (uint64)_glyphCount * _bytesPerGlyph > size - bmOff) {
		warning("GfxFontUnicode: %s has a table that runs past the end",
				filename.c_str());
		_data.clear();
		return false;
	}

	_codepoints = d + cpOff;
	_widths = d + wOff;
	_bitmaps = d + bmOff;

	// The code point table must be sorted, because lookup is a binary search.
	// Verify once at load rather than trusting the producer.
	for (uint32 i = 1; i < _glyphCount; i++) {
		if (READ_LE_UINT32(_codepoints + i * 4) <=
			READ_LE_UINT32(_codepoints + (i - 1) * 4)) {
			warning("GfxFontUnicode: %s code point table is not sorted at %u",
					filename.c_str(), i);
			_data.clear();
			_codepoints = _widths = _bitmaps = nullptr;
			return false;
		}
	}

	_loaded = true;
	debug(1, "GfxFontUnicode: %s loaded, %u glyphs, %dx%d, %dbpp",
		  filename.c_str(), _glyphCount, _cellWidth, _cellHeight, _bitsPerPixel);
	return true;
}

int GfxFontUnicode::findGlyph(uint32 codepoint) const {
	if (!_loaded)
		return -1;

	uint32 lo = 0, hi = _glyphCount;
	while (lo < hi) {
		const uint32 mid = lo + (hi - lo) / 2;
		const uint32 cp = READ_LE_UINT32(_codepoints + mid * 4);
		if (cp == codepoint)
			return (int)mid;
		if (cp < codepoint)
			lo = mid + 1;
		else
			hi = mid;
	}
	return -1;
}

bool GfxFontUnicode::pixelSet(int glyph, int x, int y) const {
	const byte *row = _bitmaps + (uint32)glyph * _bytesPerGlyph + (uint32)y * _rowBytes;
	if (_bitsPerPixel == 1)
		return (row[x >> 3] & (0x80 >> (x & 7))) != 0;
	return ((row[x >> 2] >> (6 - ((x & 3) * 2))) & 3) != 0;
}

bool GfxFontUnicode::isDoubleByte(uint32 chr) {
	const int g = findGlyph(chr);
	return g >= 0 && _widths[g] == 2;
}

byte GfxFontUnicode::getCharWidth(uint32 chr) {
	const int g = findGlyph(chr);
	if (g < 0)
		return 0;
	return _widths[g] == 2 ? _advanceWide : _advanceNarrow;
}

byte GfxFontUnicode::getCharHeight(uint32 chr) {
	return findGlyph(chr) >= 0 ? _cellHeight : 0;
}

void GfxFontUnicode::draw(uint32 chr, int16 top, int16 left, byte color,
						  bool greyedOutput) {
	const int g = findGlyph(chr);
	if (g < 0)
		return;

	const int cells = _widths[g];
	const int w = _cellWidth * cells;

	// Double-byte glyphs are drawn on the hires text plane at twice the lowres
	// coordinates, exactly as GfxFontKorean does via putHangulChar. Writing
	// lowres pixels here instead renders nothing visible: the upscaled
	// background is composited over them. That was measured - the glyph draw
	// calls arrived with correct code points and coordinates while the screen
	// stayed blank.
	//
	// Expand to one byte per pixel with 0xff meaning "unset", the convention
	// the driver expects.
	_glyphScratch.resize((uint)w * _cellHeight);
	byte *dst = _glyphScratch.begin();
	memset(dst, 0xff, (uint)w * _cellHeight);

	for (int y = 0; y < _cellHeight; y++) {
		for (int x = 0; x < w; x++) {
			if (!pixelSet(g, x, y))
				continue;
			// Greying is the engine's existing checkerboard convention: skip
			// every other pixel so the glyph reads as disabled.
			if (greyedOutput && ((top + y) % 2) == ((left + x) % 2))
				continue;
			dst[y * w + x] = color;
		}
	}

	_screen->putHiresGlyph(dst, w, _cellHeight, left, top, color);
}

void GfxFontUnicode::drawToBuffer(uint32 chr, int16 top, int16 left, byte color,
								  bool greyedOutput, byte *buffer,
								  int16 width, int16 height) {
	const int g = findGlyph(chr);
	if (g < 0)
		return;

	const int cells = _widths[g];
	const int w = _cellWidth * cells;

	for (int y = 0; y < _cellHeight; y++) {
		const int destY = top + y;
		if (destY < 0 || destY >= height)
			continue;
		for (int x = 0; x < w; x++) {
			const int destX = left + x;
			if (destX < 0 || destX >= width)
				continue;
			if (!pixelSet(g, x, y))
				continue;
			if (greyedOutput && (destY % 2) == (destX % 2))
				continue;
			buffer[destY * width + destX] = color;
		}
	}
}

GfxFontUnicodeAdapter::GfxFontUnicodeAdapter(GfxFontUnicode *font,
											 Common::CodePage codePage,
											 GfxFont *fallback,
											 GuiResourceId resourceId)
	: _font(font), _fallback(fallback), _codePage(codePage),
	  _resourceId(resourceId) {
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
	// Answered from the ENCODING, not from the glyph: the renderer calls this
	// with a single lead byte to decide whether to fetch a second one, long
	// before a code point exists. Getting this from the font would break the
	// byte walk.
	if (_fallback)
		return _fallback->isDoubleByte(chr);
	return false;
}

byte GfxFontUnicodeAdapter::getCharWidth(uint32 chr) {
	// Single-byte characters stay with the game's own font. The Unicode
	// bundle has Latin glyphs too, but using them would change the metrics of
	// every English string in every font the game uses - measured, it made
	// fonts of height 12 and 9 all report 8 and pushed menu text outside its
	// button. The bundle is for characters the resource font cannot draw.
	if (chr < 0x80 && _fallback)
		return _fallback->getCharWidth(chr);

	const uint32 cp = toCodePoint(chr);
	if (cp && _font->hasGlyph(cp)) {
		const byte w = _font->getCharWidth(cp);
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
	if (chr < 0x80 && _fallback)
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
	// Keep single-byte text pixel-identical to the unmodified engine.
	if (chr < 0x80 && _fallback) {
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
	if (chr < 0x80 && _fallback) {
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
