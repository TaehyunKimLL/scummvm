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
#include "scumm/ks_check.h"

namespace Scumm {

namespace {

// [josa][consonant, vowel, rieul, unknown]: 을/를, 은/는, 이/가, 과/와, 으로/로, 에/에, 에게/에게
static const char *const kJosaUtf8[][4] = {
	{ "", "", "", "" },	// kJosaNone - - - -
	{ "\xec\x9d\x84", "\xeb\xa5\xbc", "\xec\x9d\x84", "\xec\x9d\x84(\xeb\xa5\xbc)" },	// kJosaObject 을 를 을 을(를)
	{ "\xec\x9d\x80", "\xeb\x8a\x94", "\xec\x9d\x80", "\xec\x9d\x80(\xeb\x8a\x94)" },	// kJosaTopic 은 는 은 은(는)
	{ "\xec\x9d\xb4", "\xea\xb0\x80", "\xec\x9d\xb4", "\xec\x9d\xb4(\xea\xb0\x80)" },	// kJosaSubject 이 가 이 이(가)
	{ "\xea\xb3\xbc", "\xec\x99\x80", "\xea\xb3\xbc", "\xea\xb3\xbc(\xec\x99\x80)" },	// kJosaAnd 과 와 과 과(와)
	{ "\xec\x9c\xbc\xeb\xa1\x9c", "\xeb\xa1\x9c", "\xeb\xa1\x9c", "(\xec\x9c\xbc)\xeb\xa1\x9c" },	// kJosaMeans 으로 로 로 (으)로
	{ "\xec\x97\x90", "\xec\x97\x90", "\xec\x97\x90", "\xec\x97\x90" },	// kJosaAt 에 에 에 에
	{ "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c", "\xec\x97\x90\xea\xb2\x8c" },	// kJosaTo 에게 에게 에게 에게
};
// [josa][consonant, vowel, rieul, unknown]: 을/를, 은/는, 이/가, 과/와, 으로/로, 에/에, 에게/에게
static const char *const kJosaCp949[][4] = {
	{ "", "", "", "" },	// kJosaNone - - - -
	{ "\xc0\xbb", "\xb8\xa6", "\xc0\xbb", "\xc0\xbb(\xb8\xa6)" },	// kJosaObject 을 를 을 을(를)
	{ "\xc0\xba", "\xb4\xc2", "\xc0\xba", "\xc0\xba(\xb4\xc2)" },	// kJosaTopic 은 는 은 은(는)
	{ "\xc0\xcc", "\xb0\xa1", "\xc0\xcc", "\xc0\xcc(\xb0\xa1)" },	// kJosaSubject 이 가 이 이(가)
	{ "\xb0\xfa", "\xbf\xcd", "\xb0\xfa", "\xb0\xfa(\xbf\xcd)" },	// kJosaAnd 과 와 과 과(와)
	{ "\xc0\xb8\xb7\xce", "\xb7\xce", "\xb7\xce", "(\xc0\xb8)\xb7\xce" },	// kJosaMeans 으로 로 로 (으)로
	{ "\xbf\xa1", "\xbf\xa1", "\xbf\xa1", "\xbf\xa1" },	// kJosaAt 에 에 에 에
	{ "\xbf\xa1\xb0\xd4", "\xbf\xa1\xb0\xd4", "\xbf\xa1\xb0\xd4", "\xbf\xa1\xb0\xd4" },	// kJosaTo 에게 에게 에게 에게
};

/**
 * The particle B takes for each v2 preposition (the table in the design
 * note): a person, a thing, or the tool of Unlock/Fix ... with.
 */
struct PrepRule {
	KoreanJosa person;
	KoreanJosa thing;
	KoreanJosa tool;
};

static const PrepRule kPrepRules[5] = {
	{ kJosaNone,  kJosaNone, kJosaNone  },	// 0: none - "<A> <B> <verb>"
	{ kJosaAt,    kJosaAt,   kJosaAt    },	// 1: in   - "<B>에 <A> <verb>"
	{ kJosaAt,    kJosaAt,   kJosaMeans },	// 2: with - "<B>에 ...", the tool "<B>(으)로 ..."
	{ kJosaAt,    kJosaAt,   kJosaAt    },	// 3: on   - "<B>에 <A> <verb>"
	{ kJosaTo,    kJosaAt,   kJosaTo    },	// 4: to   - a person "<B>에게", a thing "<B>에"
};

/// The not-yet-chosen B.
static const char kPlaceholder[] = "...";

/// @p s without '@' padding and without spaces at either end.
Common::String clean(const Common::String &s) {
	Common::String out;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] != '@')
			out += s[i];
	}
	out.trim();
	return out;
}

/// Byte offset of the last character of @p s (which must not be empty).
uint lastCharStart(const Common::String &s, bool utf8) {
	uint pos = 0, last = 0;
	while (pos < s.size()) {
		last = pos;
		pos += koreanCharLength(s.c_str() + pos, utf8);
	}
	return last;
}

/// @p s without its last character, and without the spaces that leaves at its end.
Common::String dropLastChar(const Common::String &s, bool utf8) {
	Common::String out(s.c_str(), lastCharStart(s, utf8));
	while (!out.empty() && out.lastChar() == ' ')
		out.deleteLastChar();
	return out;
}

int charCount(const Common::String &s, bool utf8) {
	int n = 0;
	for (uint pos = 0; pos < s.size(); n++)
		pos += koreanCharLength(s.c_str() + pos, utf8);
	return n;
}

void join(Common::String &line, const Common::String &part) {
	if (part.empty())
		return;
	if (!line.empty())
		line += ' ';
	line += part;
}

/// The composer, on cleaned parts, with B's final given (it is the full name's while fitting).
Common::String compose(const Common::String &verb, const Common::String &a, const Common::String &b,
					   const KoreanSentenceParts &p, KoreanFinal bFinal, bool utf8) {
	Common::String line;
	if (!p.hasA || a.empty())
		return verb;

	// "What is" asks a question: "이게 뭐지? 램프" reads, "램프 이게 뭐지?" does not.
	if (!verb.empty() && verb.lastChar() == '?') {
		join(line, verb);
		join(line, a);
		return line;
	}

	if (p.prep >= 1 && p.prep <= 4) {
		const PrepRule &rule = kPrepRules[p.prep];
		const bool chosen = p.hasB && !b.empty();
		const KoreanJosa josa = p.instrumentB ? rule.tool : ((!chosen || p.bIsActor) ? rule.person : rule.thing);
		join(line, (chosen ? b : Common::String(kPlaceholder)) + koreanJosa(chosen ? bFinal : kKoFinalUnknown, josa, utf8));
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

int koreanCharLength(const char *p, bool utf8) {
	const byte c = (byte)*p;
	if (c < 0x80)
		return 1;
	int n;
	if (utf8)
		n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
	else
		n = (c >= 0x81 && c <= 0xFE) ? 2 : 1;
	for (int i = 1; i < n; i++) {
		if (!p[i])
			return i;	// a truncated character: never step past the NUL
	}
	return n;
}

KoreanFinal koreanFinalOf(const Common::String &noun, bool utf8) {
	const Common::String s = clean(noun);
	if (s.empty())
		return kKoFinalUnknown;
	const uint start = lastCharStart(s, utf8);
	const byte *c = (const byte *)s.c_str() + start;
	const int len = s.size() - start;

	int jong = -1;
	if (utf8 && len == 3) {
		const uint32 cp = ((c[0] & 0x0F) << 12) | ((c[1] & 0x3F) << 6) | (c[2] & 0x3F);
		if (cp >= 0xAC00 && cp <= 0xD7A3)
			jong = (cp - 0xAC00) % 28;
	} else if (!utf8 && len == 2) {
		// The KS X 1001 syllables (the 2350 the translations use); the
		// CP949 extension's syllables and other double-byte characters
		// fall through to the vowel-final rule below.
		if (c[0] >= 0xB0 && c[0] <= 0xC8 && c[1] >= 0xA1 && c[1] <= 0xFE)
			jong = checkJongsung(c[0], c[1]);
	}
	if (jong < 0 || jong == 0)
		return kKoFinalNone;	// also Latin letters, digits, symbols: read as vowel-final
	return (jong == 8) ? kKoFinalRieul : kKoFinalConsonant;
}

Common::String koreanJosa(KoreanFinal final, KoreanJosa josa, bool utf8) {
	int form;
	switch (final) {
	case kKoFinalConsonant: form = 0; break;
	case kKoFinalNone:      form = 1; break;
	case kKoFinalRieul:     form = 2; break;
	default:                form = 3; break;
	}
	return (utf8 ? kJosaUtf8 : kJosaCp949)[josa][form];
}

Common::String koreanWithJosa(const Common::String &noun, KoreanJosa josa, bool utf8) {
	const Common::String s = clean(noun);
	return s + koreanJosa(koreanFinalOf(s, utf8), josa, utf8);
}

Common::String composeKoreanSentence(const KoreanSentenceParts &parts, bool utf8) {
	const Common::String b = clean(parts.objB);
	return compose(clean(parts.verb), clean(parts.objA), b, parts, koreanFinalOf(b, utf8), utf8);
}

Common::String fitKoreanSentence(const KoreanSentenceParts &parts, bool utf8, int maxWidth,
								 KoreanWidthFunc width, void *ctx) {
	const Common::String verb = clean(parts.verb);
	Common::String a = clean(parts.objA);
	Common::String b = clean(parts.objB);
	const KoreanFinal bFinal = koreanFinalOf(b, utf8);	// the full name's particle, however short B gets

	Common::String line;
	for (;;) {
		line = compose(verb, a, b, parts, bFinal, utf8);
		if (width(line, ctx) <= maxWidth)
			return line;
		// Shorten the wider name that still has more than one character.
		const bool canA = parts.hasA && charCount(a, utf8) > 1;
		const bool canB = parts.hasB && charCount(b, utf8) > 1;
		if (!canA && !canB)
			break;
		if (canB && (!canA || width(b, ctx) > width(a, ctx)))
			b = dropLastChar(b, utf8);
		else
			a = dropLastChar(a, utf8);
	}

	// Still too wide: keep the longest run of whole characters that fits.
	Common::String fit;
	uint pos = 0;
	while (pos < line.size()) {
		const int n = koreanCharLength(line.c_str() + pos, utf8);
		Common::String next(line.c_str(), pos + n);
		if (width(next, ctx) > maxWidth)
			break;
		fit = next;
		pos += n;
	}
	return fit;
}

bool koreanSentenceApplies(int version, Common::Platform platform, Common::Language language, bool hasBundle) {
	return version == 2 && platform != Common::kPlatformNES && language == Common::KO_KOR && hasBundle;
}

void cutV2Sentence(const char *sentence, bool nes, byte *out, int outSize) {
	const int maxChars = nes ? 60 : 40;
	// A character, the NES line break after it, and the NUL.
	const int room = nes ? 4 : 2;
	const char *ptr = sentence;
	int i = 0, len = 0;
	while (*ptr) {
		if (*ptr != '@')
			len++;
		if (len > maxChars)
			break;
		if (i + room > outSize)
			break;

		out[i++] = *ptr++;

		if (nes && len == 30) {
			out[i++] = 0xFF;
			out[i++] = 8;
		}
	}
	out[i] = 0;
}

} // End of namespace Scumm
