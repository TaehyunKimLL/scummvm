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
#include "graphics/hires_text/font_value.h"
#include "graphics/hires_text/latin_advance.h"
#include "graphics/hires_text/unicode_props.h"

#include "sci/sci.h"
#include "sci/utf8.h"

#include "common/textconsole.h"
#include "common/ustr.h"
#include "common/util.h"

namespace Sci {

GfxFontSet::GfxFontSet(GuiResourceId resourceId, Common::CodePage codePage, const FontSettings &settings)
	: _resourceId(resourceId), _codePage(codePage), _settings(settings) {
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

uint32 GfxFontSet::toEncodedPair(uint32 codePoint) const {
	// Lead byte low, trail byte high - the layout the legacy faces index by.
	return encodeCodePagePair(codePoint, _codePage);
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

	// @p chr is TextCompose::glyphCode()'s own result (design 6.5 steps 2-6):
	// a declined code (the game's own font draws the original game code), a
	// virtual targeted-glyph code, or a real code point.
	if (chr >= Graphics::kHiResGameCodeBase) {
		outChr = chr - Graphics::kHiResGameCodeBase;
		return &_faces[0];
	}

	if (chr >= Graphics::kHiResTargetBase) {
		// design 6.7: a target is answered only by the ranged Unicode face,
		// which decodes it to {face, real cp} itself - the range table,
		// coverage fallback and missing= box are all bypassed. A target the
		// face turns out to lack (warned about at load) has no glyph here
		// either, and no other face can draw a virtual code point, so it is
		// simply declined.
		for (uint i = 0; i < _faces.size(); i++) {
			if (_faces[i].kind != kFaceCodePoint)
				continue;
			if (static_cast<const GfxFontUnicode *>(_faces[i].font)->hasGlyph(chr)) {
				outChr = chr;
				return &_faces[i];
			}
		}
		return nullptr;
	}

	const uint32 codePoint = chr;
	for (uint i = 0; i < _faces.size(); i++) {
		const Face &f = _faces[i];

		// The resource face holds only the game's own single-byte glyphs. It
		// reports a width for a double-byte value anyway, so asking it by
		// width let it swallow every Korean syllable before the Unicode face
		// was reached - measured, 37 syllables drawn by the wrong face and
		// the menu buttons came out blank.
		if (f.kind == kFaceResource)
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
		//
		// Faces are asked in order. The legacy face comes before the Unicode
		// one, except in a Korean game while a hi-res face is in effect (a
		// map's face= or hires_text_face): GfxCache::createFontSet() then
		// puts that face before korean.fnt, which draws only what it lacks.
		if (!legacyCovers(codePoint))
			continue;
		const uint32 packed = toEncodedPair(codePoint);
		if (!packed)
			continue;
		outChr = packed;
		return &f;
	}

	// Nothing covers it: the resource face draws the original game code -
	// today's behaviour for a character no face wants, and (design 6.5 step
	// 6) for a code point whose chain lacks it and has no missing= box
	// either (a face lacking the box is already caught by
	// Sci::checkPlanLoadWarnings() at load time; drawing here still falls
	// back cleanly since RangeRoutedGlyphSource::cells() already answered 0
	// for it and the kFaceCodePoint branch above never matched).
	outChr = chr;
	return &_faces[0];
}

TextFaceKind GfxFontSet::classify(uint32 chr) const {
	uint32 outChr = 0;
	const Face *f = faceFor(chr, outChr);
	if (!f)
		return kTextFaceResource;

	switch (f->kind) {
	case kFaceLegacyDbcs:
		return kTextFaceLegacy;

	case kFaceCodePoint:
		// A real code point (not a target) an explicit range rule sent here,
		// rather than the id's own plain chain: design 6.2's "a range rule
		// beats the plain default".
		if (chr < Graphics::kHiResTargetBase && _settings.plan.faceRules.lookup(chr) >= 0)
			return kTextFaceRule;
		return kTextFaceUnicode;

	case kFaceResource:
	default:
		return kTextFaceResource;
	}
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
	return isLeadByte(chr & 0xFF);
}

bool GfxFontSet::isLeadByte(byte b) const {
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
	if (!f)
		return 0;
	if (f->kind != kFaceCodePoint)
		return toLowres(*f, f->font->getCharWidth(c));

	GfxFontUnicode *uni = static_cast<GfxFontUnicode *>(f->font);
	const int scale = (f->hiresPlane && getSciVersion() < SCI_VERSION_2) ? 2 : 1;
	const Graphics::HiResAdvance rule = _settings.plan.advanceFor(c);

	// design sections 6.2/6.3: the id's resolved advance rule for the code
	// point actually drawn (GfxText16 moves the pen by this very value, and
	// the glyph's origin is at the start of that box, so measuring and
	// drawing agree).
	if (rule == Graphics::kHiResAdvanceGame || rule == Graphics::kHiResAdvanceFont) {
		// `game`: the resource face's own width for the game code (as today,
		// glyphChar()'s declined codes already round-trip to it above).
		const uint32 gameCode = chr < Graphics::kHiResGameCodeBase ? chr : c;
		const int gameWidth = _faces[0].font->getCharWidth(gameCode);
		return (byte)Graphics::advanceGamePx(rule, gameWidth, uni->advanceHires(c), scale);
	}
	if (rule == Graphics::kHiResAdvanceCell) {
		const int cell = _settings.cell;
		const int raw = Graphics::Unicode::isWide(c) ? cell : cell / 2;
		return (byte)MAX(1, raw / scale);
	}

	// kHiResAdvanceEngine (nothing set an advance rule): design section 8's
	// "today's path" - the cell rule for a legacy code-page game, per-glyph
	// advances (Sci::glyphGameWidth()) for a UTF-8 translation.
	if (f->hiresPlane && c >= 0x80 && uni->perGlyph())
		return uni->gameCharWidth(c, getSciVersion() >= SCI_VERSION_2 ? 1 : 2);
	return toLowres(*f, f->font->getCharWidth(c));
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

void GfxFontSet::beginString() {
	for (uint i = 0; i < _faces.size(); i++)
		_faces[i].font->beginString();
}

void GfxFontSet::drawToBuffer(uint32 chr, int16 top, int16 left, byte color,
							  bool greyedOutput, byte *buffer, int16 width, int16 height) {
	uint32 c = 0;
	const Face *f = faceFor(chr, c);
	if (f)
		f->font->drawToBuffer(c, top, left, color, greyedOutput, buffer, width, height);
}

} // End of namespace Sci
