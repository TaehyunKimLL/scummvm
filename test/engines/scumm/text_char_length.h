#include <cxxtest/TestSuite.h>

#include <stdlib.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/file.h"
#include "common/str.h"
#include "common/ustr.h"
#include "graphics/hires_text/text_layout.h"

#include "engines/scumm/charset.h"
#include "engines/scumm/text_utf8.h"

#include "../../system/null_osystem.h"

/**
 * SCUMM's character length and escape rules for UTF-8 translation bundles
 * (C11 T7, I18N_TEXT_DESIGN.md sections 3.3 and 4.3): textCharLength() is
 * the UTF-8 length when the text is UTF-8 and exactly the old DBCS
 * expression otherwise; ScummTextDecoder turns SCUMM's escapes into control
 * units at their own byte offsets; layoutLinebreaks() wraps UTF-8 text with
 * the shared layout stage the way addLinebreaks() wraps CP949 text.
 */
namespace {

#pragma push_macro("getenv")
#undef getenv
const char *kortrsDirT7() {
	return getenv("SCUMMVM_TEST_KORTRS");
}
#pragma pop_macro("getenv")

/// Widths for the layout comparison: ASCII by a small table, any other
/// character one wide cell (the legacy "2-byte char = _2byteWidth + 1").
struct FixedWidths : public Graphics::LayoutMetrics {
	int wide;
	explicit FixedWidths(int w) : wide(w) {}
	static int ascii(uint32 c) {
		if (c == ' ')
			return 3;
		if (c == 'i' || c == 'l' || c == '.' || c == ',' || c == '\'' || c == '!')
			return 2;
		if (c == 'm' || c == 'w' || c == 'M' || c == 'W')
			return 7;
		if (c < 0x20)
			return 0;
		return 5;
	}
	int advance(uint32 cp) override {
		return cp < 0x80 ? ascii(cp) : wide;
	}
};

/// The layout hooks the engine uses, with fixed widths and no charset.
struct FixedHooks : public Scumm::ScummLayoutHooks {
	int wide;
	explicit FixedHooks(int w) : wide(w) {}
	int advance(uint32 cp) override {
		return cp < 0x80 ? FixedWidths::ascii(cp) : wide;
	}
};

/**
 * CharsetRenderer::addLinebreaks() for a Korean fan-translation target
 * (isScummvmKorTarget(), _useCJKMode, not centred), v5, with the widths
 * above: the branch the CP949 korean.trs bundles take, reduced to what it
 * does with a string and a width.
 */
void legacyKoreanLinebreaks(byte *str, int maxwidth, int wide) {
	int lastKoreanLineBreak = -1;
	const int origPos = 0;
	int pos = 0;
	int lastspace = -1;
	int curw = 1;
	int chr;
	const int strLength = (int)strlen((const char *)str);

	while ((chr = str[pos++]) != 0) {
		if (chr == '@')
			continue;
		if (chr == 255 || chr == 254) {
			chr = str[pos++];
			if (chr == 3)
				break;
			if (chr == 8) {
				while (str[pos] == ' ')
					str[pos++] = '@';
				continue;
			}
			if (chr == 10 || chr == 21 || chr == 12 || chr == 13) {
				pos += 2;
				continue;
			}
			if (chr == 1) {
				curw = 1;
				continue;
			}
			if (chr == 2)
				break;
			if (chr == 14) {
				pos += 2;
				continue;
			}
		}
		if (chr == ' ')
			lastspace = pos - 1;

		if (chr & 0x80) {
			pos++;
			curw += wide;
		} else {
			curw += FixedWidths::ascii(chr);
		}

		if (chr & 0x80) {
			if (Scumm::checkKSCode(chr, str[pos - 1])
				&& !(pos - 4 >= origPos && str[pos - 3] == '`' && str[pos - 4] == ' ')
				&& !(pos - 4 >= origPos && str[pos - 3] == '\'' && str[pos - 4] == ' ')
				&& !(pos - 3 >= origPos && str[pos - 3] == '('))
				lastKoreanLineBreak = pos - 2;
		} else {
			if (chr == '(' && pos - 3 >= origPos && Scumm::checkKSCode(str[pos - 3], str[pos - 2]))
				lastKoreanLineBreak = pos - 1;
		}
		if (lastspace == -1 && lastKoreanLineBreak == -1)
			continue;
		if (curw > maxwidth) {
			if (lastspace >= lastKoreanLineBreak) {
				str[lastspace] = 0xD;
				curw = 1;
				pos = lastspace + 1;
				lastspace = -1;
				lastKoreanLineBreak = -1;
			} else {
				byte *breakPtr = str + lastKoreanLineBreak;
				memmove(breakPtr + 1, breakPtr, strLength - lastKoreanLineBreak + 1);
				str[lastKoreanLineBreak] = 0xD;
				curw = 1;
				pos = lastKoreanLineBreak + 1;
				lastspace = -1;
				lastKoreanLineBreak = -1;
			}
		}
	}
}

/// Character index of every 0x0D, counting one per character (not byte).
Common::Array<int> breakIndices(const byte *s, bool utf8) {
	Common::Array<int> out;
	int chars = 0;
	const byte *end = s + strlen((const char *)s);
	const byte *p = s;
	while (p < end) {
		if (*p == 0xFF || *p == 0xFE) {
			const byte code = p + 1 < end ? p[1] : 0;
			p += 2;
			if (code == 9 || code == 10 || code == 12 || code == 13 || code == 14 || code == 21)
				p += 2;
			continue;
		}
		if (*p == 0x0D)
			out.push_back(chars);
		if (*p >= 0x80 && utf8)
			p += Scumm::textCharLength(true, Common::UNK_LANG, p, end);
		else if (*p >= 0x80)
			p += 2;
		else
			p++;
		chars++;
	}
	return out;
}

} // End of anonymous namespace

class ScummTextCharLengthTestSuite : public CxxTest::TestSuite {
public:
	void test_utf8_lengths() {
		const byte a[] = { 'A' };
		const byte ga[] = { 0xEA, 0xB0, 0x80 };
		const byte emoji[] = { 0xF0, 0x9F, 0x98, 0x80 };
		const byte stray[] = { 0x80, 'x' };
		const byte two[] = { 0xC3, 0xA9 };
		TS_ASSERT_EQUALS(Scumm::textCharLength(true, Common::JA_JPN, a, a + 1), 1);
		TS_ASSERT_EQUALS(Scumm::textCharLength(true, Common::JA_JPN, ga, ga + 3), 3);
		TS_ASSERT_EQUALS(Scumm::textCharLength(true, Common::JA_JPN, emoji, emoji + 4), 4);
		TS_ASSERT_EQUALS(Scumm::textCharLength(true, Common::JA_JPN, stray, stray + 2), 1);
		TS_ASSERT_EQUALS(Scumm::textCharLength(true, Common::JA_JPN, two, two + 2), 2);
		// Never past the end of the data: a truncated sequence is one byte.
		TS_ASSERT_EQUALS(Scumm::textCharLength(true, Common::JA_JPN, ga, ga + 2), 1);
	}

	void test_escape_bytes_never_inside_utf8() {
		// SCUMM scans for its escapes byte by byte; UTF-8 must never carry
		// one of those bytes inside a character (design section 3.3).
		int bad = 0;
		for (uint32 cp = 0x80; cp <= 0xFFFF; cp++) {
			if (cp >= 0xD800 && cp <= 0xDFFF)
				continue;
			const Common::String s = Common::U32String(Common::u32char_type_t(cp)).encode(Common::kUtf8);
			for (uint i = 0; i < s.size(); i++) {
				const byte b = (byte)s[i];
				if (b == 0xFE || b == 0xFF || b == '@' || b == '^' || b == '\\' || b == '`' || b < 0x80)
					bad++;
			}
		}
		TS_ASSERT_EQUALS(bad, 0);
	}

	void test_non_utf8_is_old_expression() {
		const Common::Language langs[] = { Common::KO_KOR, Common::JA_JPN, Common::ZH_CHN,
		                                   Common::ZH_TWN, Common::EN_ANY, Common::DE_DEU };
		for (uint l = 0; l < ARRAYSIZE(langs); l++) {
			for (int c = 0; c < 256; c++) {
				const byte s[2] = { (byte)c, 0xA1 };
				const int want = Scumm::is2ByteCharacter(langs[l], (byte)c) ? 2 : 1;
				TS_ASSERT_EQUALS(Scumm::textCharLength(false, langs[l], s, s + 2), want);
			}
		}
	}

	void test_scumm_decoder_escapes() {
		// "a" FF 0A 12 34 "b" FF 01 "@" "c" FE 03, then UTF-8 "가"
		const byte s[] = { 'a', 0xFF, 0x0A, 0x12, 0x34, 'b', 0xFF, 0x01, '@', 'c', 0xFE, 0x03,
		                   0xEA, 0xB0, 0x80, 0x0A, 0x0D };
		Scumm::ScummTextDecoder dec(5, false, 0);
		Graphics::TextRun run;
		run.decode(s, sizeof(s), dec);
		TS_ASSERT_EQUALS(run.size(), 10u);
		TS_ASSERT_EQUALS(run.cp(0), (uint32)'a');
		TS_ASSERT_EQUALS(run.cp(1), Graphics::kControlUnit);
		TS_ASSERT_EQUALS(run.byteOffset(1), 1u);
		TS_ASSERT_EQUALS(run.byteOffset(2), 5u);     // FF 0A spans its two argument bytes
		TS_ASSERT(run.flags(1) & Graphics::kUnitControl);
		TS_ASSERT(!(run.flags(1) & Graphics::kUnitNewline));
		TS_ASSERT_EQUALS(run.cp(2), (uint32)'b');
		TS_ASSERT(run.flags(3) & Graphics::kUnitNewline); // FF 01
		TS_ASSERT(run.flags(3) & Graphics::kUnitControl);
		TS_ASSERT_EQUALS(run.byteOffset(4), 8u);
		TS_ASSERT(run.flags(4) & Graphics::kUnitControl);  // '@' is a zero-width control
		TS_ASSERT_EQUALS(run.cp(5), (uint32)'c');
		TS_ASSERT_EQUALS(run.byteOffset(6), 10u);         // FE 03
		TS_ASSERT(run.flags(6) & Graphics::kUnitControl);
		TS_ASSERT_EQUALS(run.cp(7), 0xAC00u);
		TS_ASSERT(run.flags(7) & Graphics::kUnitWide);
		// 0x0A is not a line break in SCUMM (only 0x0D and the escapes are)
		TS_ASSERT_EQUALS(run.cp(8), 0x0Au);
		TS_ASSERT(!(run.flags(8) & (Graphics::kUnitNewline | Graphics::kUnitControl)));
		TS_ASSERT(run.flags(9) & Graphics::kUnitNewline);
		TS_ASSERT_EQUALS(run.byteOffset(10), (uint32)sizeof(s));

		// A newline character a CJK release names (FF <nl>) is a newline too.
		const byte t[] = { 'x', 0xFF, 0xFE, 'y' };
		Scumm::ScummTextDecoder dec2(5, false, 0xFE);
		run.decode(t, sizeof(t), dec2);
		TS_ASSERT_EQUALS(run.size(), 3u);
		TS_ASSERT(run.flags(1) & Graphics::kUnitNewline);
	}

	void test_layout_replaces_space_and_inserts_between_ideographs() {
		// 10 wide characters of width 10 at maxwidth 51 (curw starts at 1):
		// five fit per line.
		Common::String text;
		for (int i = 0; i < 10; i++)
			text += "\xE3\x81\x82";   // U+3042
		byte buf[64];
		memset(buf, 0, sizeof(buf));
		memcpy(buf, text.c_str(), text.size() + 1);
		FixedHooks hooks(10);
		Graphics::BreakRules rules;
		Scumm::layoutLinebreaks(buf, sizeof(buf), 0, 51, hooks, rules, 5, 0);
		TS_ASSERT_EQUALS(buf[15], 0x0D);
		TS_ASSERT_EQUALS(strlen((const char *)buf), 31u);

		// Latin: the space before the overflowing word becomes 0x0D.
		byte lat[64] = "aaaa bbbb cccc";
		Scumm::layoutLinebreaks(lat, sizeof(lat), 0, 45, hooks, rules, 5, 0);
		TS_ASSERT_EQUALS(Common::String((const char *)lat), Common::String("aaaa bbbb\rcccc"));
	}

	void test_layout_insert_is_bounded() {
		// No room for the inserted 0x0D: the text loses its last character
		// (on a character boundary) rather than running past the buffer.
		Common::String text;
		for (int i = 0; i < 4; i++)
			text += "\xE3\x81\x82";
		byte buf[13];
		memcpy(buf, text.c_str(), 13);    // 12 bytes + NUL: full
		FixedHooks hooks(10);
		Graphics::BreakRules rules;
		Scumm::layoutLinebreaks(buf, sizeof(buf), 0, 21, hooks, rules, 5, 0);
		TS_ASSERT_EQUALS(buf[6], 0x0D);
		TS_ASSERT_EQUALS(strlen((const char *)buf), 10u);  // "ああ\rあ": one character dropped
		TS_ASSERT_EQUALS(buf[10], 0);
	}

	void test_layout_never_splits_a_word() {
		// No break opportunity fits: the word runs past the width, as SCUMM
		// lets it, and the line breaks at the first opportunity after.
		byte buf[64] = "aaaaaaaaaaaa bb cc";
		FixedHooks hooks(10);
		Graphics::BreakRules rules;
		Scumm::layoutLinebreaks(buf, sizeof(buf), 0, 30, hooks, rules, 5, 0);
		TS_ASSERT_EQUALS(Common::String((const char *)buf), Common::String("aaaaaaaaaaaa\rbb cc"));
	}

	void test_layout_stops_at_wait() {
		// FF 03 'Wait' ends what is wrapped now; the text after it is
		// wrapped when it is shown.
		byte buf[64] = "aaaa bbbb\xFF\x03" "cccc dddd eeee";
		FixedHooks hooks(10);
		Graphics::BreakRules rules;
		Scumm::layoutLinebreaks(buf, sizeof(buf), 0, 45, hooks, rules, 5, 0);
		TS_ASSERT_EQUALS(Common::String((const char *)buf), Common::String("aaaa bbbb\xFF\x03" "cccc dddd eeee"));
		// ...and from there on a second call wraps the rest.
		Scumm::layoutLinebreaks(buf, sizeof(buf), 11, 45, hooks, rules, 5, 0);
		TS_ASSERT_EQUALS(Common::String((const char *)buf), Common::String("aaaa bbbb\xFF\x03" "cccc dddd\reeee"));
	}

	void test_layout_thai_marks_stay_on_base() {
		// ที่ ที่ ... without spaces: every break is before a base, never
		// between U+0E17 and its marks.
		Common::String text;
		for (int i = 0; i < 8; i++)
			text += "\xE0\xB8\x97\xE0\xB8\xB5\xE0\xB9\x88";   // U+0E17 U+0E35 U+0E48
		byte buf[128];
		memset(buf, 0, sizeof(buf));
		memcpy(buf, text.c_str(), text.size() + 1);
		struct ThaiHooks : public Scumm::ScummLayoutHooks {
			int advance(uint32 cp) override { return (cp == 0x0E35 || cp == 0x0E48) ? 0 : 8; }
		} hooks;
		Graphics::BreakRules rules;
		Scumm::layoutLinebreaks(buf, sizeof(buf), 0, 25, hooks, rules, 5, 0);
		const byte *p = buf;
		int lines = 1;
		while (*p) {
			if (*p == 0x0D) {
				lines++;
				TS_ASSERT_EQUALS(p[1], 0xE0);
				TS_ASSERT_EQUALS(p[2], 0xB8);
				TS_ASSERT_EQUALS(p[3], 0x97);   // the next line starts with the base
			}
			p++;
		}
		TS_ASSERT_EQUALS(lines, 3);   // 3 + 3 + 2 clusters of width 8 in 24
	}

	void test_linebreaks_layout_matches_cp949() {
#if NULL_OSYSTEM_IS_AVAILABLE
		const char *kortrs = kortrsDirT7();
		if (!kortrs || !*kortrs) {
			TS_TRACE("SCUMMVM_TEST_KORTRS is not set: skipped");
			return;
		}
		Common::install_null_g_system();
		Common::FSNode node(Common::Path(kortrs, Common::Path::kNativeSeparator)
			.appendComponent("mi1ute").appendComponent("Ultimate Talkie Version with Midi Music")
			.appendComponent("korean.trs"));
		if (!node.exists()) {
			TS_TRACE("no MI1 UTE korean.trs under SCUMMVM_TEST_KORTRS: skipped");
			return;
		}
		Common::SeekableReadStream *s = node.createReadStream();
		TS_ASSERT(s);
		if (!s)
			return;
		Common::Array<byte> data;
		data.resize(s->size());
		s->read(data.begin(), data.size());
		delete s;

		Scumm::TrsHeader h;
		TS_ASSERT(Scumm::parseTrsHeader(data.begin(), data.size(), h));
		const int widths[] = { 120, 160, 200, 240, 300 };
		const int wide = 9;   // MI1's 8-px Korean cell + the Korean 1-px gap
		int compared = 0, differ = 0;
		for (uint e = 0; e < 200 && e < h.numLines; e++) {
			const uint32 off = h.translatedOffset[e];
			if (off >= data.size())
				continue;
			const byte *src = data.begin() + off;
			const uint32 len = strlen((const char *)src);
			if (len == 0 || len > 400)
				continue;
			Common::Array<byte> utf8;
			Scumm::transcodeScummText(src, len, Common::kWindows949, Common::kUtf8, utf8, nullptr);
			for (uint w = 0; w < ARRAYSIZE(widths); w++) {
				byte a[1024], b[1024];
				memset(a, 0, sizeof(a));
				memset(b, 0, sizeof(b));
				memcpy(a, src, len);
				memcpy(b, utf8.begin(), utf8.size());
				legacyKoreanLinebreaks(a, widths[w], wide);
				FixedHooks hooks(wide);
				Graphics::BreakRules rules;
				rules.hangul = Graphics::kHangulBreakAny;
				Scumm::layoutLinebreaks(b, sizeof(b), 0, widths[w], hooks, rules, 5, 0);
				const Common::Array<int> ia = breakIndices(a, false);
				const Common::Array<int> ib = breakIndices(b, true);
				compared++;
				bool same = ia.size() == ib.size();
				for (uint k = 0; same && k < ia.size(); k++)
					same = ia[k] == ib[k];
				if (!same) {
					differ++;
					if (differ <= 5) {
						Common::String sa, sb;
						for (uint k = 0; k < ia.size(); k++)
							sa += Common::String::format(" %d", ia[k]);
						for (uint k = 0; k < ib.size(); k++)
							sb += Common::String::format(" %d", ib[k]);
						TS_WARN(Common::String::format("entry %u width %d: cp949 breaks at%s, layout at%s", e, widths[w], sa.c_str(), sb.c_str()).c_str());
					}
				}
			}
		}
		TS_TRACE(Common::String::format("%d of %d (entry, width) pairs break identically",
		                                compared - differ, compared).c_str());
		TS_ASSERT(compared > 0);
		TS_ASSERT_EQUALS(differ, 0);
#endif
	}
};
