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

#include "graphics/hires_text/text_layout.h"

#include "common/str.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

namespace {

const uint32 kReplacement = 0xFFFD;

bool isContinuation(byte b, byte lo = 0x80, byte hi = 0xBF) {
	return b >= lo && b <= hi;
}

/** Flags the run adds by code point: space, combining, wide. */
byte classify(uint32 cp) {
	if (cp == 0x20 || cp == 0x3000)
		return kUnitSpace;
	if (Unicode::isCombining(cp))
		return kUnitCombining;
	if (Unicode::isWide(cp))
		return kUnitWide;
	return 0;
}

} // End of anonymous namespace

// --- Decoders -----------------------------------------------------------

int Utf8TextDecoder::decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const {
	flags = 0;
	const byte b0 = p[0];
	if (b0 < 0x80) {
		cp = b0;
		if (b0 == '\n')
			flags = kUnitNewline;
		return 1;
	}

	// RFC 3629 table 3.7: the second byte's range depends on the lead, which
	// rules out overlong forms, surrogates and anything past U+10FFFF.
	int len;
	byte lo = 0x80, hi = 0xBF;
	uint32 value;
	if (b0 >= 0xC2 && b0 <= 0xDF) {
		len = 2;
		value = b0 & 0x1F;
	} else if (b0 >= 0xE0 && b0 <= 0xEF) {
		len = 3;
		value = b0 & 0x0F;
		if (b0 == 0xE0)
			lo = 0xA0;
		else if (b0 == 0xED)
			hi = 0x9F;
	} else if (b0 >= 0xF0 && b0 <= 0xF4) {
		len = 4;
		value = b0 & 0x07;
		if (b0 == 0xF0)
			lo = 0x90;
		else if (b0 == 0xF4)
			hi = 0x8F;
	} else {
		cp = kReplacement;
		return 1;
	}

	if (end - p < len || !isContinuation(p[1], lo, hi)) {
		cp = kReplacement;
		return 1;
	}
	value = (value << 6) | (p[1] & 0x3F);
	for (int k = 2; k < len; k++) {
		if (!isContinuation(p[k])) {
			cp = kReplacement;
			return 1;
		}
		value = (value << 6) | (p[k] & 0x3F);
	}
	cp = value;
	return len;
}

/**
 * How many bytes the character starting at @p p takes, in @p page.
 *
 * Returns 1 for anything that is not a lead byte, so a caller always makes
 * progress and never splits a string mid-character.
 *
 * Taken verbatim from SCUMM's charLength() (engines/scumm/hires_text.cpp),
 * which keeps its own copy until it switches to this decoder.
 */
int CodePageTextDecoder::charLength(Common::CodePage page, const byte *p, const byte *end) {
	const byte lead = *p;

	switch (page) {
	case Common::kUtf8:
		if (lead < 0x80)
			return 1;
		if ((lead & 0xE0) == 0xC0)
			return 2;
		if ((lead & 0xF0) == 0xE0)
			return 3;
		if ((lead & 0xF8) == 0xF0)
			return 4;
		return 1;   // a stray continuation byte

	case Common::kWindows932:
		// Shift-JIS: two lead byte ranges. Everything between them, including
		// half-width katakana at 0xA1..0xDF, is a single byte character - a
		// reminder that byte width says nothing about which script it is.
		return ((lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC)) ? 2 : 1;

	case Common::kWindows936:
	case Common::kWindows950:
		return (lead >= 0x81 && lead <= 0xFE) ? 2 : 1;

	case Common::kWindows949:
		return (lead >= 0x81 && lead <= 0xFE) ? 2 : 1;

	case Common::kJohab:
		return (lead >= 0x84 && lead <= 0xF9) ? 2 : 1;

	default:
		// A single byte page, or none named at all.
		return 1;
	}
}

int CodePageTextDecoder::decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const {
	flags = 0;
	int len = charLength(_page, p, end);
	if (end - p < len) {
		// A lead byte whose trail is missing: one byte, nothing to convert.
		cp = kReplacement;
		return 1;
	}

	// ASCII is ASCII in every page here, and converting it would need the
	// CJK tables loaded. With no page named, the byte is the code point.
	if ((len == 1 && *p < 0x80) || _page == Common::kCodePageInvalid) {
		cp = *p;
		if (cp == '\n')
			flags = kUnitNewline;
		return 1;
	}

	const Common::U32String decoded(Common::String((const char *)p, len), _page);
	cp = decoded.empty() ? kReplacement : (uint32)decoded[0];
	return len;
}

// --- TextRun ------------------------------------------------------------

void TextRun::clear() {
	// Common::Array::clear() frees the storage; resize(0) keeps it.
	_cp.resize(0);
	_offset.resize(0);
	_flags.resize(0);
}

void TextRun::push(uint32 cp, byte flags, uint32 offset) {
	if (!(flags & kUnitControl))
		flags |= classify(cp);
	_cp.push_back(cp);
	_flags.push_back(flags);
	_offset.push_back(offset);
}

void TextRun::decode(const byte *text, uint32 len, const TextDecoder &dec) {
	clear();
	// At most one unit per byte; reserve() does nothing once the run has
	// grown that far, so a reused run allocates only for longer strings.
	_cp.reserve(len);
	_flags.reserve(len);
	_offset.reserve(len + 1);

	const byte *const end = text + len;
	uint32 pos = 0;
	while (pos < len) {
		uint32 cp = 0;
		byte flags = 0;
		int n = dec.decode(text + pos, end, cp, flags);
		if (n < 1)
			n = 1;
		if ((uint32)n > len - pos)
			n = len - pos;
		push(cp, flags, pos);
		pos += n;
	}
	_offset.push_back(len);
}

void TextRun::assign(const Common::U32String &text) {
	clear();
	const uint32 len = text.size();
	_cp.reserve(len);
	_flags.reserve(len);
	_offset.reserve(len + 1);
	for (uint32 i = 0; i < len; i++) {
		const uint32 cp = text[i];
		push(cp, cp == '\n' ? (byte)kUnitNewline : (byte)0, i);
	}
	_offset.push_back(len);
}

// --- LayoutMetrics ------------------------------------------------------

int LayoutMetrics::width(const TextRun &run, uint32 from, uint32 to) {
	int w = 0;
	for (uint32 i = from; i < to; i++) {
		if (run.flags(i) & (kUnitControl | kUnitCombining))
			continue;
		w += advance(run.cp(i));
	}
	return w;
}

int LayoutMetrics::extend(const TextRun &run, uint32 from, uint32 i, int widthSoFar) {
	(void)from;
	if (run.flags(i) & (kUnitControl | kUnitCombining))
		return widthSoFar;
	return widthSoFar + advance(run.cp(i));
}

// --- TextLayout ---------------------------------------------------------

namespace TextLayout {

namespace {

/** A control unit that is not a newline: glued to the text after it. */
bool isGlue(const TextRun &run, uint32 i) {
	const byte f = run.flags(i);
	return (f & kUnitControl) && !(f & kUnitNewline);
}

bool isHangul(uint32 cp) {
	return (cp >= 0x1100 && cp <= 0x11FF)     // Hangul Jamo
		|| (cp >= 0x3130 && cp <= 0x318F)     // Hangul Compatibility Jamo
		|| (cp >= 0xA960 && cp <= 0xA97F)     // Hangul Jamo Extended-A
		|| (cp >= 0xAC00 && cp <= 0xD7A3)     // Hangul Syllables
		|| (cp >= 0xD7B0 && cp <= 0xD7FF);    // Hangul Jamo Extended-B
}

/** ASCII closers that may not begin a line after a wide unit (JIS X 4051). */
bool isAsciiCloser(uint32 cp) {
	return cp == ')' || cp == ']' || cp == '}' || cp == ',' || cp == '.'
		|| cp == '!' || cp == '?' || cp == ':' || cp == ';';
}

/** ASCII openers that may not end a line before a wide unit. */
bool isAsciiOpener(uint32 cp) {
	return cp == '(' || cp == '[' || cp == '{';
}

/** Rule 4: a wide unit is a break opportunity on either side. */
bool breaksAsWide(uint32 cp, byte flags, const BreakRules &rules) {
	if (!(flags & kUnitWide))
		return false;
	return rules.hangul == kHangulBreakAny || !isHangul(cp);
}

/** Unit i is Thai, or a combining mark on a Thai base. */
bool isThaiContext(const TextRun &run, uint32 i) {
	while (i > 0 && (run.flags(i) & kUnitCombining))
		i--;
	const uint32 cp = run.cp(i);
	return cp >= 0x0E00 && cp <= 0x0E7F;
}

// --- Thai syllable clusters ---------------------------------------------
//
// Thai is written without spaces between words. Without a dictionary the
// best break opportunities are between orthographic syllables. The
// segmenter below starts from Thai Character Clusters (a leading vowel
// with its consonant, a base with its marks and following vowels, the
// vowels เ-ือ เ-ีย ัว, ๆ and ฯ) - no break is ever placed inside one -
// and merges them into syllables with a few spelling rules: a bare
// consonant after an open vowel is its final, two bare consonants are an
// inherent-vowel syllable ("ปก"), a bare consonant before a voweled one
// begins a cluster ("ทรี"), letters under ์ are silent. A wrong guess
// only moves a break to another cluster boundary.

enum ThaiClass {
	kThaiOther,      ///< not Thai (or past the end): ends the Thai run
	kThaiCons,       ///< U+0E01..U+0E2E
	kThaiLead,       ///< U+0E40..U+0E44, written before the consonant
	kThaiFollow,     ///< U+0E30 U+0E32 U+0E33 U+0E45
	kThaiVowelMark,  ///< U+0E31 U+0E34..U+0E39 U+0E47 U+0E4D
	kThaiKaran,      ///< U+0E4C, the silencer
	kThaiMark,       ///< tone marks and every other combining mark
	kThaiRepeat,     ///< U+0E2F, U+0E46
	kThaiDigit,      ///< U+0E50..U+0E59
	kThaiSign        ///< U+0E3F U+0E4F U+0E5A U+0E5B
};

enum {
	kThaiYoYak = 0x0E22, kThaiRoRua = 0x0E23, kThaiLoLing = 0x0E25, kThaiWoWaen = 0x0E27,
	kThaiHoHip = 0x0E2B, kThaiOAng = 0x0E2D, kThaiSaraA = 0x0E30, kThaiSaraAm = 0x0E33,
	kThaiMaiHanAkat = 0x0E31, kThaiSaraI = 0x0E34, kThaiSaraIi = 0x0E35, kThaiSaraUe = 0x0E36, kThaiSaraUee = 0x0E37,
	kThaiSaraU = 0x0E38, kThaiSaraE = 0x0E40, kThaiMaiTaiKhu = 0x0E47
};

int thaiClass(const TextRun &run, uint32 i) {
	const uint32 cp = run.cp(i);
	if (cp >= 0x0E01 && cp <= 0x0E2E)
		return kThaiCons;
	if (cp >= 0x0E40 && cp <= 0x0E44)
		return kThaiLead;
	if (cp == 0x0E30 || cp == 0x0E32 || cp == 0x0E33 || cp == 0x0E45)
		return kThaiFollow;
	if (cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E39) || cp == 0x0E47 || cp == 0x0E4D)
		return kThaiVowelMark;
	if (cp == 0x0E4C)
		return kThaiKaran;
	if (cp == 0x0E2F || cp == 0x0E46)
		return kThaiRepeat;
	if (cp >= 0x0E50 && cp <= 0x0E59)
		return kThaiDigit;
	if (cp == 0x0E3F || cp == 0x0E4F || cp == 0x0E5A || cp == 0x0E5B)
		return kThaiSign;
	if ((cp >= 0x0E00 && cp <= 0x0E7F) || (!(run.flags(i) & kUnitControl) && (run.flags(i) & kUnitCombining)))
		return kThaiMark;
	return kThaiOther;
}

/** Consonants that never close a syllable. */
bool thaiNeverFinal(uint32 cp) {
	return cp == 0x0E09 || cp == 0x0E1C || cp == 0x0E1D || cp == kThaiHoHip || cp == kThaiOAng || cp == 0x0E2E;
}

/**
 * Two consonants that begin one syllable: a true cluster (กร ปล คว ...),
 * a silent ห before a sonorant (หม หล ...) or อย.
 */
bool thaiInitialPair(uint32 c1, uint32 c2) {
	switch (c2) {
	case kThaiRoRua:   // กร ขร คร ตร ปร พร ทร บร ศร สร จร ซร
		return c1 == 0x0E01 || c1 == 0x0E02 || c1 == 0x0E04 || c1 == 0x0E15 || c1 == 0x0E1B
			|| c1 == 0x0E1E || c1 == 0x0E17 || c1 == 0x0E1A || c1 == 0x0E28 || c1 == 0x0E2A
			|| c1 == 0x0E08 || c1 == 0x0E0B || c1 == kThaiHoHip;
	case kThaiLoLing:  // กล ขล คล ปล พล ผล บล
		return c1 == 0x0E01 || c1 == 0x0E02 || c1 == 0x0E04 || c1 == 0x0E1B || c1 == 0x0E1E
			|| c1 == 0x0E1C || c1 == 0x0E1A || c1 == kThaiHoHip;
	case kThaiWoWaen:  // กว ขว คว
		return c1 == 0x0E01 || c1 == 0x0E02 || c1 == 0x0E04 || c1 == kThaiHoHip;
	case 0x0E07: case 0x0E0D: case 0x0E19: case 0x0E21:   // หง หญ หน หม
		return c1 == kThaiHoHip;
	case kThaiYoYak:   // หย อย
		return c1 == kThaiHoHip || c1 == kThaiOAng;
	default:
		return false;
	}
}

/** Splits a Thai run into syllable-ish segments. Escapes are skipped. */
class ThaiSyllables {
public:
	explicit ThaiSyllables(const TextRun &run) : _run(run), _n(run.size()) {}

	/** The first unit at or after i that is not an escape. */
	uint32 sig(uint32 i) const {
		while (i < _n && isGlue(_run, i))
			i++;
		return i;
	}
	uint32 next(uint32 i) const { return sig(i + 1); }
	int cls(uint32 i) const {
		if (i >= _n || (_run.flags(i) & (kUnitControl | kUnitSpace)))
			return kThaiOther;
		return thaiClass(_run, i);
	}
	uint32 cp(uint32 i) const { return i < _n ? _run.cp(i) : 0; }

	/** A mark or a following vowel: what gives the consonant before it a vowel. */
	bool carries(uint32 i) const {
		const int c = cls(i);
		return c == kThaiVowelMark || c == kThaiMark || c == kThaiKaran || c == kThaiFollow;
	}

	/**
	 * Consonant p begins a syllable with a written vowel: a mark or a
	 * following vowel on it, อ as the vowel ออ, ว as ัว before a final
	 * (สวน), or an initial pair whose second consonant has one.
	 */
	bool voweled(uint32 p) const {
		const uint32 q = next(p);
		if (carries(q))
			return true;
		if (cls(q) != kThaiCons)
			return false;
		const uint32 d = cp(q);
		const uint32 f = next(q);
		if (d == kThaiOAng && !carries(f) && !(cp(f) == kThaiYoYak && voweled(f)))
			return true;
		if (d == kThaiWoWaen && cls(f) == kThaiCons && !carries(next(f)))
			return true;
		return thaiInitialPair(cp(p), d) && voweled(q);
	}

	/** A consonant with nothing written on it. */
	bool bare(uint32 p) const { return cls(p) == kThaiCons && !voweled(p); }

	/**
	 * A syllable begins at y: anything but a bare consonant; a bare
	 * consonant of an initial pair (ตรง, หม้อ, อยู่); two bare consonants
	 * before anything but a third, or before silent letters (บน in
	 * "ดินบนโต๊ะ", รบ in "สิ่งรบกวน", มน in "มนตร์"). Not ก in "ผู้ปกครอง":
	 * ปก is a syllable, ป no final. Ambiguous without a dictionary: a
	 * voweled consonant after a bare one makes that one a final (การ|พยา),
	 * which misreads a prefix syllable (มีข|นาด for มี|ขนาด).
	 */
	bool opens(uint32 y) const {
		if (!bare(y))
			return true;
		const uint32 z = next(y);
		if (thaiInitialPair(cp(y), cp(z)))
			return true;
		return bare(z) && (!bare(next(z)) || silent(next(z), 2) != next(z));
	}

	/** Skips tone marks, other marks and ์. */
	uint32 skipMarks(uint32 p) const {
		while (cls(p) == kThaiMark || cls(p) == kThaiKaran)
			p = next(p);
		return p;
	}

	/** Silent letters: up to max consonants (and ิ or ุ) under ์. */
	uint32 silent(uint32 p, int max) const {
		for (;;) {
			uint32 q = p;
			int k = 0;
			while (k < max && cls(q) == kThaiCons) {
				q = next(q);
				k++;
			}
			if (!k)
				return p;
			if (cp(q) == kThaiSaraI || cp(q) == kThaiSaraU)
				q = next(q);
			if (cls(q) != kThaiKaran)
				return p;
			p = skipMarks(q);
		}
	}

	/** The end of the segment that starts at s (s < size, a Thai unit). */
	uint32 end(uint32 s) const {
		uint32 p = s;
		for (;;) {
			const int c = cls(p);
			if (c == kThaiDigit) {
				while (cls(p) == kThaiDigit || cls(p) == kThaiMark)
					p = next(p);
				return repeats(p);
			}
			if (c != kThaiCons && c != kThaiLead)
				return repeats(skipMarks(next(p)));

			uint32 lead = 0;
			if (c == kThaiLead) {
				lead = cp(p);
				p = next(p);
				while (cls(p) == kThaiLead)   // "เเ" typed for "แ"
					p = next(p);
				if (cls(p) != kThaiCons)
					return repeats(skipMarks(p));
			}

			// The initial consonant, and after a leading vowel the second
			// one of an initial pair (เปลี่ยน, ใหม่, แหวน, แปลก, เพราะ).
			const uint32 c1 = cp(p);
			p = next(p);
			if (lead && cls(p) == kThaiCons && thaiInitialPair(c1, cp(p))) {
				const uint32 a = next(p);
				if (c1 == kThaiHoHip || cls(a) == kThaiVowelMark || cls(a) == kThaiMark
						|| (cp(p) == kThaiRoRua && cls(a) == kThaiFollow)
						|| (bare(a) && opens(next(a))))
					p = a;
			}

			// The vowel and tone marks, the following vowels.
			bool voweled = lead != 0, closed = false, follow = false;
			uint32 vowel = 0;
			for (;;) {
				const int k = cls(p);
				if (k == kThaiVowelMark) {
					voweled = true;
					vowel = cp(p);
				} else if (k == kThaiFollow) {
					voweled = true;
					follow = true;
					if (cp(p) == kThaiSaraA || cp(p) == kThaiSaraAm)
						closed = true;
				} else if (k != kThaiMark && k != kThaiKaran) {
					break;
				}
				p = next(p);
			}

			// Vowels spelled with a consonant: เ-ีย, เ-ือ, -ือ, เ-อ(ะ), ัว,
			// ออ, and ัว written without ั before a final (สวน).
			bool spelled = false;
			if (!closed && cls(p) == kThaiCons) {
				const uint32 d = cp(p);
				const uint32 f = next(p);
				const bool free = !carries(f);
				if (d == kThaiYoYak)
					spelled = lead == kThaiSaraE && vowel == kThaiSaraIi;
				else if (d == kThaiOAng)
					spelled = (vowel == kThaiSaraUee && free)
						|| (lead == kThaiSaraE && !vowel && !follow && (free || cp(f) == kThaiSaraA))
						|| (!voweled && free && !(cp(f) == kThaiYoYak && this->voweled(f)));
				else if (d == kThaiWoWaen)
					spelled = vowel == kThaiMaiHanAkat || (!voweled && free && bare(f) && opens(next(f)));
				else if (d == kThaiRoRua && cp(f) == kThaiRoRua && !voweled && !lead) {
					// รร, the vowel -ั- (ธรรม, กรรม).
					voweled = spelled = true;
					p = next(f);
					if (bare(p) && !thaiNeverFinal(cp(p)) && opens(next(p)))
						p = next(p);
					p = silent(p, 2);
					return repeats(p);
				}
				if (spelled) {
					voweled = true;
					p = skipMarks(f);
					if (cp(p) == kThaiSaraA) {
						closed = true;
						p = next(p);
					}
				}
			}

			p = silent(p, 1);   // เวิร์ด; not ยัก|ษ์: that ก is the final

			// The final consonant.
			if (!closed && cls(p) == kThaiCons) {
				const uint32 x = p;
				if (!voweled) {
					if (!bare(x)) {
						// A bare consonant before a voweled one: an initial
						// pair or a syllable too short to stand alone (ปรา,
						// ทรี, สมัย). Same segment.
						continue;
					}
					p = next(x);
					if (thaiInitialPair(c1, cp(x))) {
						// An initial pair and a final: ตรง, ครบ.
						if (bare(p) && !thaiNeverFinal(cp(p)) && opens(next(p)))
							p = next(p);
					}
					// Otherwise two bare consonants: คน, ปก.
				} else if (!spelled && (vowel == kThaiMaiHanAkat || vowel == kThaiSaraUee
						|| (vowel == kThaiMaiTaiKhu && lead))) {
					p = next(x);   // ั, ื and เ-็ always take a final
				} else if (bare(x) && !thaiNeverFinal(cp(x)) && (opens(next(x)) || (!follow && !spelled
						&& (vowel == kThaiSaraI || vowel == kThaiSaraUe || vowel == kThaiSaraU)))) {
					// A bare consonant after an open vowel, unless it
					// begins a syllable with the next one (ผู้|ปก). The
					// short ิ ึ ุ are nearly always closed: ดิน|ขนาด.
					p = next(x);
				}
			}

			p = silent(p, 2);   // จันทร์
			// A last bare consonant before a leading vowel or the end of
			// the Thai run has no syllable to begin: silent (จักร).
			if (bare(p) && cls(next(p)) != kThaiCons)
				p = next(p);
			return repeats(p);
		}
	}

private:
	uint32 repeats(uint32 p) const {
		while (cls(p) == kThaiRepeat)
			p = next(p);
		return p;
	}

	const TextRun &_run;
	const uint32 _n;
};

/** Whether unit j (Thai, not an escape) begins a segment of its Thai run,
 *  j - 1 being Thai too. Segmentation starts at the last leading vowel
 *  before j (always a segment start) or at the start of the Thai run. */
bool isThaiSegmentStart(const TextRun &run, uint32 j) {
	const ThaiSyllables th(run);
	uint32 k = j;
	for (;;) {
		// The unit before k, escapes skipped.
		uint32 pk = k;
		bool none = true;
		while (pk > 0) {
			pk--;
			if (!isGlue(run, pk)) {
				none = false;
				break;
			}
		}
		const bool runStart = none || th.cls(pk) == kThaiOther;
		if (th.cls(k) == kThaiLead && (runStart || th.cls(pk) != kThaiLead)) {
			if (k == j)
				return true;
			break;
		}
		if (runStart)
			break;
		k = pk;
	}
	uint32 s = k;
	while (s < j)
		s = th.end(s);
	return s == j;
}

void finish(const TextRun &run, LineSpan &l, LayoutMetrics &m) {
	l.byteStart = run.byteOffset(l.first);
	l.byteEnd = run.byteOffset(l.end);
	l.byteNext = run.byteOffset(l.next);
	// The ink width: spaces and escapes at the end of the line hang.
	uint32 ink = l.end;
	while (ink > l.first && ((run.flags(ink - 1) & kUnitSpace) || isGlue(run, ink - 1)))
		ink--;
	l.width = ink > l.first ? m.width(run, l.first, ink) : 0;
}

void trimTrailingSpaces(const TextRun &run, LineSpan &l) {
	while (l.end > l.first && (run.flags(l.end - 1) & kUnitSpace))
		l.end--;
}

} // End of anonymous namespace

bool canBreakBefore(const TextRun &run, uint32 i, const BreakRules &rules) {
	const uint32 n = run.size();
	if (i == 0 || i >= n)
		return false;

	// 1. Never right after an escape, and never before a combining mark.
	const byte fa = run.flags(i - 1);
	if (isGlue(run, i - 1))
		return false;
	// A run of escapes is judged by the unit after it.
	uint32 j = i;
	while (j < n && isGlue(run, j))
		j++;
	if (j >= n)
		return false;   // trailing escapes stay on the line
	const byte fb = run.flags(j);
	if (fb & kUnitCombining)
		return false;
	if ((fa & kUnitNewline) || (fb & kUnitNewline))
		return true;

	const uint32 a = run.cp(i - 1);
	const uint32 b = run.cp(j);

	// ๆ and ฯ never begin a line, not even after a space ("รอบ ๆ").
	if (rules.thaiFallback && (b == 0x0E46 || b == 0x0E2F))
		return false;

	// 2. After a space run. In "space, escapes, space, text" the only
	// opportunity is before the text: the escapes end the line before.
	if ((fa & kUnitSpace) && !(fb & kUnitSpace))
		return true;

	// 3. Kinsoku.
	if (rules.kinsoku) {
		if (Unicode::kinsokuNoStart(b) || (isAsciiCloser(b) && (fa & kUnitWide)))
			return false;
		if (Unicode::kinsokuNoEnd(a) || (isAsciiOpener(a) && (fb & kUnitWide)))
			return false;
	}

	// 4. Ideographic: either side wide.
	if (breaksAsWide(a, fa, rules) || breaksAsWide(b, fb, rules))
		return true;

	// 5. Thai: between syllable-ish segments (ThaiSyllables above).
	if (rules.thaiFallback && Unicode::isThaiBase(b) && isThaiContext(run, i - 1))
		return isThaiSegmentStart(run, j);

	// 6. Inside a word.
	return false;
}

bool isClusterBoundary(const TextRun &run, uint32 i) {
	if (i == 0 || i >= run.size())
		return true;
	if (run.flags(i) & kUnitCombining)
		return false;
	return !isGlue(run, i - 1);
}

LineSpan fitLine(const TextRun &run, uint32 from, int maxWidth, LayoutMetrics &m, const BreakRules &rules) {
	const uint32 n = run.size();
	LineSpan l;
	l.first = l.end = l.next = from;
	if (from >= n) {
		l.first = l.end = l.next = n;
		finish(run, l, m);
		return l;
	}

	uint32 lastBreak = from;   // from itself means "none"
	uint32 i = from;
	int w = 0;      // width of [from, i + 1), kept through LayoutMetrics::extend()
	int inkW = 0;   // width of [from, k + 1), k the last unit before i that is neither a space nor an escape
	for (; i < n; i++) {
		if (run.flags(i) & kUnitNewline) {
			l.end = i;
			l.next = i + 1;
			l.forced = true;
			trimTrailingSpaces(run, l);
			finish(run, l, m);
			return l;
		}
		// A break before i keeps [from, i) minus its trailing spaces; its
		// ink width is inkW (spaces and escapes at its end hang).
		if (i > from && inkW <= maxWidth && canBreakBefore(run, i, rules))
			lastBreak = i;
		w = m.extend(run, from, i, w);
		// Spaces hang past the edge, and so do escapes after them: an
		// escape followed by spaces ends its line.
		if ((run.flags(i) & kUnitSpace) || isGlue(run, i))
			continue;
		inkW = w;
		// A combining mark is zero wide: it cannot be the unit that
		// overflows, even after a hanging space.
		if (run.flags(i) & kUnitCombining)
			continue;
		if (w > maxWidth)
			break;
	}

	if (i >= n && (inkW <= maxWidth || lastBreak == from)) {
		l.end = l.next = n;
		trimTrailingSpaces(run, l);
		finish(run, l, m);
		return l;
	}

	// Unit i does not fit (or, at the end, a mark pinned hanging spaces
	// past the edge).
	uint32 end = lastBreak;
	if (end == from) {
		l.emergency = true;
		end = i;
		while (end > from && !isClusterBoundary(run, end))
			end--;
		if (end == from) {
			// Not even the first cluster fits: it goes on the line alone.
			end = from + 1;
			while (end < n && !isClusterBoundary(run, end))
				end++;
		}
	}

	l.end = l.next = end;
	while (l.next < n && (run.flags(l.next) & kUnitSpace) && !(run.flags(l.next) & kUnitNewline))
		l.next++;
	trimTrailingSpaces(run, l);
	finish(run, l, m);
	return l;
}

void breakLines(const TextRun &run, int maxWidth, LayoutMetrics &m, const BreakRules &rules,
				Common::Array<LineSpan> &out) {
	out.resize(0);
	uint32 from = 0;
	while (from < run.size()) {
		const LineSpan l = fitLine(run, from, maxWidth, m, rules);
		out.push_back(l);
		from = l.next;
	}
}

} // End of namespace TextLayout

} // End of namespace Graphics
