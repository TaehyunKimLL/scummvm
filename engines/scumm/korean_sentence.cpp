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

#include "scumm/korean_sentence.h"
#include "scumm/korean_text.h"

namespace Scumm {

namespace {

// [josa][consonant, vowel, rieul, unknown], UTF-8
static const char *const kJosa[][4] = {
	{ "", "", "", "" },
	{ "\xec\x9d\x84", "\xeb\xa5\xbc", "\xec\x9d\x84", "\xec\x9d\x84(\xeb\xa5\xbc)" },	// 을 를 을 을(를)
	{ "\xec\x9d\x80", "\xeb\x8a\x94", "\xec\x9d\x80", "\xec\x9d\x80(\xeb\x8a\x94)" },	// 은 는 은 은(는)
	{ "\xec\x9d\xb4", "\xea\xb0\x80", "\xec\x9d\xb4", "\xec\x9d\xb4(\xea\xb0\x80)" },	// 이 가 이 이(가)
	{ "\xea\xb3\xbc", "\xec\x99\x80", "\xea\xb3\xbc", "\xea\xb3\xbc(\xec\x99\x80)" },	// 과 와 과 과(와)
	{ "\xec\x9c\xbc\xeb\xa1\x9c", "\xeb\xa1\x9c", "\xeb\xa1\x9c", "(\xec\x9c\xbc)\xeb\xa1\x9c" },	// 으로 로 로 (으)로
	{ "\xec\x97\x90", "\xec\x97\x90", "\xec\x97\x90", "\xec\x97\x90" },	// 에
	{ "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c" },	// 에게
};

struct PrepRule {
	KoreanJosa person;
	KoreanJosa thing;
	KoreanJosa tool;
};

static const PrepRule kPrepRules[5] = {
	{ kJosaNone, kJosaNone, kJosaNone  }, // 0 none: "<A> <B> <verb>"
	{ kJosaAt,   kJosaAt,   kJosaAt    }, // 1 in:   "<B>에 <A> <verb>"
	{ kJosaAt,   kJosaAt,   kJosaMeans }, // 2 with: "<B>에 ...", tool "<B>(으)로 ..."
	{ kJosaAt,   kJosaAt,   kJosaAt    }, // 3 on
	{ kJosaTo,   kJosaAt,   kJosaTo    }, // 4 to:   person "<B>에게", thing "<B>에"
};

static const char kPlaceholder[] = "...";

Common::String clean(const Common::String &s) {
	Common::String out;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] != '@')
			out += s[i];
	}
	out.trim();
	return out;
}

uint lastCharStart(const Common::String &s) {
	uint pos = 0, last = 0;
	while (pos < s.size()) {
		last = pos;
		pos += koreanCharLength(s.c_str() + pos);
	}
	return last;
}

Common::String dropLastChar(const Common::String &s) {
	Common::String out(s.c_str(), lastCharStart(s));
	while (!out.empty() && out.lastChar() == ' ')
		out.deleteLastChar();
	return out;
}

int charCount(const Common::String &s) {
	int n = 0;
	for (uint pos = 0; pos < s.size(); n++)
		pos += koreanCharLength(s.c_str() + pos);
	return n;
}

void join(Common::String &line, const Common::String &part) {
	if (part.empty())
		return;
	if (!line.empty())
		line += ' ';
	line += part;
}

Common::String compose(const Common::String &verb, const Common::String &a, const Common::String &b,
                       const KoreanSentenceParts &p, KoreanFinal bFinal) {
	if (!p.hasA || a.empty())
		return verb;

	Common::String line;
	// "What is" asks: "이게 뭐지? 램프" reads, "램프 이게 뭐지?" does not.
	if (!verb.empty() && verb.lastChar() == '?') {
		join(line, verb);
		join(line, a);
		return line;
	}

	const bool unnamedB = p.hasB && b.empty();
	if (p.prep >= 1 && p.prep <= 4 && !unnamedB) {
		const PrepRule &rule = kPrepRules[p.prep];
		const KoreanJosa josa = p.instrumentB ? rule.tool : ((!p.hasB || p.bIsActor) ? rule.person : rule.thing);
		join(line, (p.hasB ? b : Common::String(kPlaceholder)) + koreanJosa(p.hasB ? bFinal : kKoFinalUnknown, josa));
		join(line, a);
		join(line, verb);
		return line;
	}

	join(line, a);
	if (p.hasB)
		join(line, b);
	join(line, verb);
	return line;
}

} // End of anonymous namespace

int koreanCharLength(const char *p) {
	// UTF-8 through decodeUtf8(): a byte that starts no valid sequence (a stray continuation byte, 0xFF of an escape) is
	// one character, and a NUL is no continuation byte, so it never steps past the end.
	int len;
	decodeUtf8((const byte *)p, (const byte *)p + 4, len);
	return len;
}

KoreanFinal koreanFinalOf(const Common::String &noun) {
	const Common::String s = clean(noun);
	if (s.empty())
		return kKoFinalUnknown;
	const uint start = lastCharStart(s);
	const byte *c = (const byte *)s.c_str() + start;
	int len;
	const uint32 cp = decodeUtf8(c, (const byte *)s.c_str() + s.size(), len);
	if (cp < 0xAC00 || cp > 0xD7A3)
		return kKoFinalNone;	// Latin, digits, symbols: read as vowel-final
	const int jong = (cp - 0xAC00) % 28;
	if (jong == 0)
		return kKoFinalNone;
	return (jong == 8) ? kKoFinalRieul : kKoFinalConsonant;
}

Common::String koreanJosa(KoreanFinal final, KoreanJosa josa) {
	int form;
	switch (final) {
	case kKoFinalConsonant: form = 0; break;
	case kKoFinalNone:      form = 1; break;
	case kKoFinalRieul:     form = 2; break;
	default:                form = 3; break;
	}
	return kJosa[josa][form];
}

Common::String composeKoreanSentence(const KoreanSentenceParts &parts) {
	const Common::String b = clean(parts.objB);
	return compose(clean(parts.verb), clean(parts.objA), b, parts, koreanFinalOf(b));
}

Common::String fitKoreanSentence(const KoreanSentenceParts &parts, int maxWidth, KoreanWidthFunc width, void *ctx) {
	const Common::String verb = clean(parts.verb);
	Common::String a = clean(parts.objA);
	Common::String b = clean(parts.objB);
	const KoreanFinal bFinal = koreanFinalOf(b);

	Common::String line;
	for (;;) {
		line = compose(verb, a, b, parts, bFinal);
		if (width(line, ctx) <= maxWidth)
			return line;
		const bool canA = parts.hasA && charCount(a) > 1;
		const bool canB = parts.hasB && charCount(b) > 1;
		if (!canA && !canB)
			break;
		if (canB && (!canA || width(b, ctx) > width(a, ctx)))
			b = dropLastChar(b);
		else
			a = dropLastChar(a);
	}

	Common::String fit;
	uint pos = 0;
	while (pos < line.size()) {
		const int n = koreanCharLength(line.c_str() + pos);
		Common::String next(line.c_str(), pos + n);
		if (width(next, ctx) > maxWidth)
			break;
		fit = next;
		pos += n;
	}
	while (!fit.empty() && fit.lastChar() == ' ')
		fit.deleteLastChar();
	return fit;
}

bool koreanSentenceApplies(int version, Common::Platform platform, Common::Language language, bool hasBundle) {
	return version == 2 && platform != Common::kPlatformNES && language == Common::KO_KOR && hasBundle;
}

bool koreanVerbTranslated(const Common::String &verb) {
	for (uint i = 0; i < verb.size(); i++) {
		if ((byte)verb[i] >= 0x80)
			return true;
	}
	return false;
}

} // End of namespace Scumm
