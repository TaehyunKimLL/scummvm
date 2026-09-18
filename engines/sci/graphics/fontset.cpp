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

#include "sci/graphics/fontset.h"
#include "sci/graphics/fontunicode.h"

#include "common/textconsole.h"
#include "common/ustr.h"

namespace Sci {

GfxFontSet::GfxFontSet(GuiResourceId resourceId, Common::CodePage codePage)
	: _resourceId(resourceId), _codePage(codePage) {
}

GfxFontSet::~GfxFontSet() {
	for (uint i = 0; i < _faces.size(); i++)
		delete _faces[i].font;
	_faces.clear();
}

void GfxFontSet::addFace(GfxFont *face, bool isCodePoint) {
	if (!face)
		return;
	Face f;
	f.font = face;
	f.isCodePoint = isCodePoint;
	_faces.push_back(f);
}

uint32 GfxFontSet::toCodePoint(uint32 packed) const {
	if (packed < 0x80)
		return packed;

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

const GfxFontSet::Face *GfxFontSet::faceFor(uint32 chr, uint32 &outChr) const {
	if (_faces.empty())
		return nullptr;

	// Single-byte characters always go to the first face, unconditionally.
	// Asking the faces by coverage would let a later face answer for ASCII,
	// which changes the metrics of every English string in the game.
	if (chr < 0x80) {
		outChr = chr;
		return &_faces[0];
	}

	uint32 codePoint = 0;
	bool decoded = false;

	for (uint i = 0; i < _faces.size(); i++) {
		const Face &f = _faces[i];
		if (f.isCodePoint) {
			if (!decoded) {
				codePoint = toCodePoint(chr);
				decoded = true;
			}
			if (!codePoint)
				continue;
			const GfxFontUnicode *uni = static_cast<const GfxFontUnicode *>(f.font);
			if (!uni->hasGlyph(codePoint))
				continue;
			outChr = codePoint;
			return &f;
		}

		// A byte-addressed face answers for what it has a width for; zero
		// means no glyph. This is how the legacy CJK fonts report absence.
		if (f.font->getCharWidth(chr) == 0)
			continue;
		outChr = chr;
		return &f;
	}

	// Nothing covers it. Fall back to the first face so the caller still gets
	// consistent metrics rather than zero, matching what the engine did before
	// a set existed.
	outChr = chr;
	return &_faces[0];
}

byte GfxFontSet::getHeight() {
	// Line spacing is the game's own, from the face the script chose. A later
	// face reporting its cell height here would collapse every font in the
	// game to one height.
	return _faces.empty() ? 0 : _faces[0].font->getHeight();
}

bool GfxFontSet::isDoubleByte(uint32 chr) {
	// Answered from the CODE PAGE, not from any face. The renderer calls this
	// with a lone lead byte to decide whether to fetch a second one, before a
	// code point exists; asking a face whose coverage is by code point would
	// break the byte walk and draw each half as its own character.
	if (chr > 0xFF)
		return true;
	const byte b = chr & 0xFF;
	switch (_codePage) {
	case Common::kWindows932:	// Shift-JIS
		return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
	case Common::kWindows949:	// EUC-KR / UHC
	case Common::kWindows936:	// GBK
	case Common::kWindows950:	// Big5
		return b >= 0x81 && b <= 0xFE;
	default:
		return false;
	}
}

byte GfxFontSet::getCharWidth(uint32 chr) {
	uint32 c = 0;
	const Face *f = faceFor(chr, c);
	return f ? f->font->getCharWidth(c) : 0;
}

byte GfxFontSet::getCharHeight(uint32 chr) {
	uint32 c = 0;
	const Face *f = faceFor(chr, c);
	return f ? f->font->getCharHeight(c) : 0;
}

void GfxFontSet::draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) {
	uint32 c = 0;
	const Face *f = faceFor(chr, c);
	if (f)
		f->font->draw(c, top, left, color, greyedOutput);
}

void GfxFontSet::drawToBuffer(uint32 chr, int16 top, int16 left, byte color,
							  bool greyedOutput, byte *buffer, int16 width, int16 height) {
	uint32 c = 0;
	const Face *f = faceFor(chr, c);
	if (f)
		f->font->drawToBuffer(c, top, left, color, greyedOutput, buffer, width, height);
}

} // End of namespace Sci
