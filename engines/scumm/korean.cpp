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

#include "common/str.h"
#include "scumm/charset.h"
#include "scumm/korean_sentence.h"
#include "scumm/resource.h"
#include "scumm/saved_text.h"
#include "scumm/scumm_v2.h"
#include "scumm/korean_text.h"
#include "scumm/trs_bundle.h"
#include "scumm/verbs.h"

namespace Scumm {

namespace {

int sentenceWidth(const Common::String &text, void *ctx) {
	return static_cast<CharsetRenderer *>(ctx)->getStringWidth(0, (const byte *)text.c_str());
}

} // End of anonymous namespace

bool ScummEngine_v2::drawKoreanSentence() {
	// _sentenceBuf holds the verb, translated when its resource was loaded (loadPtrToResource()).
	if (!koreanSentenceApplies(_game.version, _game.platform, _language, hasTranslationBundle()) ||
	    !koreanVerbTranslated(_sentenceBuf))
		return false;

	KoreanSentenceParts parts;
	parts.verb = _sentenceBuf;
	parts.prep = VAR(VAR_SENTENCE_PREPOSITION);
	if (parts.prep < 0 || parts.prep > 4)
		parts.prep = 0;

	// Object names come from the room's OBCD, which upstream never translates: look each up in the bundle.
	byte buf[256];
	const int obj1 = VAR(VAR_SENTENCE_OBJECT1);
	const int obj2 = VAR(VAR_SENTENCE_OBJECT2);
	const byte *name;
	if (obj1 > 0 && (name = getObjOrActorName(obj1)) != nullptr) {
		translateText(name, buf, sizeof(buf));
		parts.objA = (const char *)buf;
		parts.hasA = true;
	}
	if (obj2 > 0 && (name = getObjOrActorName(obj2)) != nullptr) {
		translateText(name, buf, sizeof(buf));
		parts.objB = (const char *)buf;
		parts.hasB = true;
		parts.bIsActor = objIsActor(obj2);
	}
	// Unlock (8) and Fix (6) ... with <B>: B is the tool (MM verb ids; jamo kMMVerbFix/kMMVerbUnlock; Zak has no
	// such verb).
	const int verb = VAR(VAR_SENTENCE_VERB);
	parts.instrumentB = parts.prep == 2 && _game.id == GID_MANIAC && (verb == 6 || verb == 8);

	_sentenceBuf = fitKoreanSentence(parts, _virtscr[kVerbVirtScreen].w, sentenceWidth, _charset);

	// The C64 draws the line one pixel right and down, as drawSentence() does.
	const int pixelOffset = (_game.platform == Common::kPlatformC64) ? 1 : 0;
	_string[2].charset = 1;
	_string[2].ypos = _virtscr[kVerbVirtScreen].topline + pixelOffset;
	_string[2].xpos = pixelOffset;
	_string[2].right = _virtscr[kVerbVirtScreen].w - 1 + pixelOffset;
	_string[2].color = (_game.platform == Common::kPlatformC64) ? 16 : 13;

	Common::Rect sentenceline(pixelOffset, _virtscr[kVerbVirtScreen].topline + pixelOffset,
	                          _virtscr[kVerbVirtScreen].w - 1 + pixelOffset, _virtscr[kVerbVirtScreen].topline + 8 + pixelOffset);
	restoreBackground(sentenceline);
	drawString(2, (const byte *)_sentenceBuf.c_str());
	return true;
}

void ScummEngine::transcodeLanguageBundle(uint32 bodySize) {
	Common::Array<uint32> originals, translations;
	for (int i = 0; i < _numTranslatedLines; i++) {
		originals.push_back(_translatedLines[i].originalTextOffset);
		translations.push_back(_translatedLines[i].translatedTextOffset);
	}
	Common::Array<byte> body;
	uint unmapped = 0;
	if (!convertTrsBodyToUtf8(_languageBuffer, bodySize, originals, translations, body, unmapped)) {
		warning("The CP949 translation bundle is malformed (an entry inside a string): not used");
		_existLanguageFile = false;
		return;
	}
	for (int i = 0; i < _numTranslatedLines; i++) {
		_translatedLines[i].originalTextOffset = originals[i];
		_translatedLines[i].translatedTextOffset = translations[i];
	}
	delete[] _languageBuffer;
	_languageBuffer = new byte[body.size()];
	memcpy(_languageBuffer, body.begin(), body.size());
	if (unmapped)
		warning("CP949 translation bundle: %u characters could not be read and show as '?'", unmapped);
	debug(2, "loadLanguageBundle: CP949 body converted to UTF-8, %u -> %u bytes", bodySize, body.size());
}

static const int kKoreanRefusal = 0xFFFD;

int ScummEngine::nextCJKChar(int c, const byte *buf, int &pos) {
	if (!_koreanUtf8)
		return c + buf[pos++] * 256;
	int len;
	const byte *p = buf + pos - 1;
	const uint32 cp = decodeUtf8(p, p + 4, len);	// a NUL is no continuation byte, so it stops the read
	pos += len - 1;
	// U+0080..U+00FF is a glyph of the game's charset (cp949ToUtf8()). korean.fnt has the 2350 KS X 1001 syllables;
	// anything else is refused visibly (once logged).
	if (cp >= 0x80 && cp < 0x100)
		return cp;
	if (cp == 0xFFFFFFFF || ksx1001Index(cp) < 0) {
		static bool warned = false;
		if (!warned) {
			warning("Korean text: U+%04X has no glyph in korean.fnt, drawn as '?' (further ones not logged)", cp);
			warned = true;
		}
		return kKoreanRefusal;
	}
	return cp;
}

int ScummEngine::koreanCharsetGlyph(const byte *p) const {
	if (!_koreanUtf8 || (p[0] != 0xC2 && p[0] != 0xC3))
		return -1;
	int len;
	const uint32 cp = decodeUtf8(p, p + 2, len);
	return (cp >= 0x80 && cp < 0x100) ? (int)cp : -1;
}

int ScummEngine::cjkTrailBytes(const byte *p) const {
	if (!_koreanUtf8)
		return 1;
	int len;
	decodeUtf8(p, p + 4, len);
	return len - 1;
}

bool ScummEngine::koreanBreakableAt(const byte *p) const {
	if (!_koreanUtf8)
		return checkKSCode(p[0], p[1]);
	int len;
	const uint32 cp = decodeUtf8(p, p + 4, len);
	return cp >= 0xAC00 && cp <= 0xD7A3;
}

bool ScummEngine::koreanCharBefore(const byte *begin, const byte *p) const {
	if (!_koreanUtf8)
		return p - 2 >= begin && checkKSCode(p[-2], p[-1]);
	const byte *s = p - 1;
	while (s > begin && p - s < 4 && (*s & 0xC0) == 0x80)
		s--;
	return s >= begin && koreanBreakableAt(s);
}

void ScummEngine::copyTranslatedText(byte *dst, const byte *src, int dstSize, bool knownUtf8) {
	if (_koreanUtf8)
		toEngineText(src, resStrLen(src) + 1, dst, dstSize, nullptr, knownUtf8);
	else
		memcpy(dst, src, MIN<int>(resStrLen(src) + 1, dstSize));
}

byte *ScummEngine::koreanRefusalGlyph() {
	// '?' scaled into the current 2-byte cell, so the line keeps its metrics.
	static const byte kQuestion[8] = { 0x3C, 0x66, 0x06, 0x0C, 0x18, 0x18, 0x00, 0x18 };
	static byte glyph[512];
	const int w = CLIP(_2byteWidth, 1, 32), h = CLIP(_2byteHeight, 1, 32);
	const int stride = (w + 7) / 8;
	memset(glyph, 0, sizeof(glyph));
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			if (kQuestion[y * 8 / h] & (0x80 >> (x * 8 / w)))
				glyph[y * stride + x / 8] |= 0x80 >> (x % 8);
	return glyph;
}

void ScummEngine::convertLoadedText(int saved) {
	// A save of this engine holds UTF-8 (trailer 1 or 2). One without the trailer (upstream, the CP949 builds) holds
	// CP949, which is known: no guessing, every text resource is converted as CP949 (legacyCp949Resource()). Only a
	// resource that is one NUL-terminated string filling its whole size is touched (a v3-v5 string resource may be a
	// byte array); UTF-8 text is as long as its CP949 or longer, except that an unreadable pair becomes
	// one '?' and a cut escape is dropped (29 of 33437 fuzzed conversions): the resource is re-created at its new size.
	if (saved == kSavedTextUtf8)
		return;
	static const ResType kTextTypes[] = { rtVerb, rtObjectName, rtActorName, rtString };
	int converted = 0;
	Common::Array<byte> out;
	for (uint t = 0; t < ARRAYSIZE(kTextTypes); t++) {
		const ResType type = kTextTypes[t];
		if (type == rtString && _game.version >= 6)
			continue;	// a v6 string resource is an array, not text
		for (ResId idx = 0; idx < _res->_types[type].size(); idx++) {
			const byte *ptr = _res->_types[type][idx]._address;
			const uint32 size = _res->_types[type][idx]._size;
			if (!ptr || !size)
				continue;
			if (type == rtVerb && (idx >= _numVerbs || _verbs[idx].type != kTextVerbType))
				continue;
			if (!legacyCp949Resource(ptr, size, out))
				continue;
			byte *dst = _res->createResource(type, idx, out.size());
			memcpy(dst, out.begin(), out.size());
			converted++;
		}
	}
	int pos = _charsetBufPos;
	if (legacyCp949Buffer(_charsetBuffer, sizeof(_charsetBuffer), pos)) {
		_charsetBufPos = pos;
		converted++;
	}
	debug(1, "SCUMM: converted %d text resources of a CP949 save to UTF-8", converted);
}

} // End of namespace Scumm
