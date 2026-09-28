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

#include "sci/sci.h"
#include "sci/engine/state.h"
#include "sci/graphics/screen.h"
#include "sci/graphics/scifont.h"
#include "sci/graphics/fontkorean.h"

#include "sci/utf8.h"
#include "graphics/korfont.h"

namespace Sci {

GfxFontKorean::GfxFontKorean(GfxScreen *screen, GuiResourceId resourceId, bool packedInput)
	: _resourceId(resourceId), _screen(screen), _packedInput(packedInput) {
	assert(resourceId != -1);

	_commonFont = Graphics::FontKorean::createFont("korean.fnt");
	//warning("GfxFontKorean: created Font");

	if (!_commonFont)
		error("Could not load ScummVM's 'korean.fnt'");
}

GfxFontKorean::~GfxFontKorean() {
	delete _commonFont;
}

GuiResourceId GfxFontKorean::getResourceId() {
	return _resourceId;
}

// Returns true for first byte of double byte characters
bool GfxFontKorean::isDoubleByte(uint32 chr) {
	uint16 ch = chr & 0xFF;
	if ((ch >= 0xA1) && (ch <= 0xFE))
		return true;
	return false;
}

// We can do >>1, because returned char width/height is 8 or 16 exclusively. Font returns hires size, we need lowres
byte GfxFontKorean::getHeight() {
	if (getSciVersion() >= SCI_VERSION_2)
		return _commonFont->getFontHeight();
	else
		return _commonFont->getFontHeight() >> 1;
}

uint16 GfxFontKorean::toFontCode(uint32 chr) {
	if (chr < 0x80)
		return (uint16)chr;
	const uint32 packed = encodeCodePagePair(chr, Common::kWindows949);
	return packed ? (uint16)packed : (uint16)chr;
}

bool GfxFontKorean::proportionalLatin() const {
	const Graphics::FontKoreanSVM *svm = dynamic_cast<const Graphics::FontKoreanSVM *>(_commonFont);
	return svm && svm->hasProportionalLatin();
}

byte GfxFontKorean::getCharWidth(uint32 chr) {
	const uint16 code = fontCode(chr);
	if (getSciVersion() >= SCI_VERSION_2)
		return _commonFont->getCharWidth(code);
	// A v4 font's proportional Latin advances are odd as often as even;
	// rounding up keeps a 7 px glyph from overlapping its neighbour on the
	// hi-res plane (lowres x is doubled there).
	if (code < 0x80 && proportionalLatin())
		return (_commonFont->getCharWidth(code) + 1) >> 1;
	return _commonFont->getCharWidth(code) >> 1;
}

void GfxFontKorean::draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) {
	// TODO: Check, if character fits on screen - if it doesn't we need to skip it
	// The 4-pixel alignment is the PC-98 SJIS rule this was copied from; it
	// is harmless with fixed half/full cells. With a v4 font's proportional
	// Latin it would pull every glyph after a narrow letter back onto the
	// one before it, so the pen position is kept as is there.
	const int16 x = proportionalLatin() ? left : (left & 0xFFC);
	_screen->putHangulChar(_commonFont, x, top, fontCode(chr), color);
}

#ifdef ENABLE_SCI32
void GfxFontKorean::drawToBuffer(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput, byte *buffer, int16 bufWidth, int16 bufHeight) {
	byte *displayPtr = buffer + top * bufWidth + left;
	// we don't use outline, so color 0 is actually not used
	_commonFont->drawChar(displayPtr, fontCode(chr), bufWidth, 1, color, 0, bufWidth - left, bufHeight - top);
}

#endif

} // End of namespace Sci
