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

#include "sci/sci.h"

#include "common/textconsole.h"
#include "common/ustr.h"

namespace Sci {

GfxFontSet::GfxFontSet(GuiResourceId resourceId, Common::CodePage codePage, LatinMode latinMode)
	: _resourceId(resourceId), _codePage(codePage), _latinMode(latinMode) {
}

GfxFontSet::~GfxFontSet() {
	for (uint i = 0; i < _faces.size(); i++) {
		if (_faces[i].owned)
			delete _faces[i].font;
	}
	_faces.clear();
}

void GfxFontSet::addFace(GfxFont *face, FaceKind kind, bool owned, bool hiresPlane) {
	if (!face)
		return;
	Face f;
	f.font = face;
	f.kind = kind;
	f.owned = owned;
	f.hiresPlane = hiresPlane;
	_faces.push_back(f);
}

uint32 GfxFontSet::toCodePoint(uint32 chr) const {
	// GfxText16 now decodes as it walks, so what arrives here is already a
	// code point. The only values that are not are the undecodable byte pairs
	// readChar() passes through unchanged, and those have no glyph anywhere.
	return chr;
}

uint32 GfxFontSet::toEncodedPair(uint32 codePoint) const {
	const char32_t cp = (char32_t)codePoint;
	const Common::U32String one(&cp, 1);
	const Common::String encoded = one.encode(_codePage);
	if (encoded.size() != 2)
		return 0;
	// Lead byte low, trail byte high - the layout the legacy faces index by.
	return (byte)encoded[0] | ((uint32)(byte)encoded[1] << 8);
}

bool GfxFontSet::legacyCovers(uint32 codePoint) const {
	// The hangul syllable block, which is the whole of korean.fnt, and the
	// ranges a Shift-JIS face holds. Anything else must fall through to the
	// Unicode face.
	if (!codePoint)
		return false;
	switch (_codePage) {
	case Common::kWindows949:
		return codePoint >= 0xAC00 && codePoint <= 0xD7A3;
	case Common::kWindows932:
		// Kana, CJK ideographs and the fullwidth forms an SJIS ROM carries.
		return (codePoint >= 0x3040 && codePoint <= 0x30FF) ||
		       (codePoint >= 0x4E00 && codePoint <= 0x9FFF) ||
		       (codePoint >= 0xFF00 && codePoint <= 0xFFEF);
	default:
		return false;
	}
}

byte GfxFontSet::toLowres(const Face &f, byte v) const {
	// A face drawing on the hires text plane puts its glyph at twice the
	// lowres coordinates, so it must report half the advance - the same `>> 1`
	// GfxFontKorean applies below SCI2. Without it the glyphs are spaced at
	// double width and the tail of a menu entry lands outside its button.
	if (!f.hiresPlane || getSciVersion() >= SCI_VERSION_2)
		return v;
	return v >> 1;
}

const GfxFontSet::Face *GfxFontSet::faceFor(uint32 chr, uint32 &outChr) const {
	if (_faces.empty())
		return nullptr;

	// Single-byte characters go to the first face, unconditionally - except
	// in kLatinHalf mode, where the printable ASCII range is deliberately
	// routed past it instead (hires_text_latin=half; see
	// TextCompose::asciiGoesToUnicodeFace()). Outside that one mode, asking
	// the faces by coverage would let a later face answer for ASCII, which
	// changes the metrics of every English string in the game.
	if (chr < 0x80 && !TextCompose::asciiGoesToUnicodeFace(chr, _latinMode)) {
		outChr = chr;
		return &_faces[0];
	}

	uint32 codePoint = 0;
	bool decoded = false;

	for (uint i = 0; i < _faces.size(); i++) {
		const Face &f = _faces[i];

		// The resource face holds only the game's own single-byte glyphs. It
		// reports a width for a double-byte value anyway, so asking it by
		// width let it swallow every Korean syllable before the Unicode face
		// was reached - measured, 37 syllables drawn by the wrong face and
		// the menu buttons came out blank.
		if (f.kind == kFaceResource)
			continue;

		if (!decoded) {
			codePoint = toCodePoint(chr);
			decoded = true;
		}
		if (!codePoint)
			continue;

		if (f.kind == kFaceCodePoint) {
			const GfxFontUnicode *uni = static_cast<const GfxFontUnicode *>(f.font);
			if (!uni->hasGlyph(codePoint))
				continue;
			outChr = codePoint;
			return &f;
		}

		// A legacy double-byte face is asked by CODE PAGE RANGE, since
		// neither of its own predicates reports coverage: getCharWidth()
		// returns a width for anything and isDoubleByte() only inspects the
		// lead byte. korean.fnt indexes glyphs as `uc - 0xAC00` and holds
		// 11184 of them, exactly the hangul syllable block, so that block is
		// its real coverage and nothing else.
		if (!legacyCovers(codePoint))
			continue;
		// A legacy face is indexed by the encoded byte pair, not by a code
		// point, so re-encode for it. GfxText16 hands out code points now;
		// this is the one place that still needs the game's encoding, and it
		// disappears when the legacy faces do.
		const uint32 packed = toEncodedPair(codePoint);
		if (!packed)
			continue;
		outChr = packed;
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
	return f ? toLowres(*f, f->font->getCharWidth(c)) : 0;
}

byte GfxFontSet::getCharHeight(uint32 chr) {
	uint32 c = 0;
	const Face *f = faceFor(chr, c);
	return f ? toLowres(*f, f->font->getCharHeight(c)) : 0;
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
