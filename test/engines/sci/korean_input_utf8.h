#include <cxxtest/TestSuite.h>

#include "common/scummsys.h"
#include "common/str.h"
#include "../../system/null_osystem.h"

#include "engines/sci/graphics/koreaninput.h"
#include "engines/sci/utf8.h"

/**
 * The Korean composer in a game whose heap strings are UTF-8
 * (heapStringsAreUtf8()), next to its EUC-KR mode (korean_input.h): the same
 * keys, the same composition, a different encoding of the run. The edit
 * control picks the mode from heapStringsAreUtf8() on every key.
 */
class SciKoreanInputUtf8TestSuite : public CxxTest::TestSuite {
	Common::String type(Sci::KoreanComposer::Encoding enc, const char *keys) {
		Sci::KoreanComposer comp;
		comp.setEncoding(enc);
		comp.setEnabled(true);
		Common::String text;
		uint runStart = 0;
		for (const char *p = keys; *p; ++p) {
			if (*p == '~') {
				if (!comp.backspace(text, runStart) && !text.empty())
					text.deleteLastChar();
				continue;
			}
			if (comp.feed(*p, text, runStart))
				continue;
			text += *p;
			runStart = text.size();
		}
		return text;
	}

	static Common::String utf8Of(uint32 cp) {
		byte buf[4];
		const int n = Sci::encodeUtf8Char(cp, buf);
		return Common::String((const char *)buf, n);
	}

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_the_default_encoding_is_eucKr() {
		Sci::KoreanComposer comp;
		TS_ASSERT_EQUALS(comp.encoding(), Sci::KoreanComposer::kCp949);
	}

	void test_composed_utf8_equals_literal_utf8() {
		// 문 열어 (U+BB38 U+0020 U+C5F4 U+C5B4)
		const Common::String want = utf8Of(0xBB38) + " " + utf8Of(0xC5F4) + utf8Of(0xC5B4);
		TS_ASSERT_EQUALS(type(Sci::KoreanComposer::kUtf8, "ans dufdj"), want);
	}

	void test_same_keys_give_the_same_text_in_both_encodings() {
		// Decode the EUC-KR run and compare code points with the UTF-8 run.
		const Common::String ko = type(Sci::KoreanComposer::kCp949, "gksrmf");
		const Common::String u8 = type(Sci::KoreanComposer::kUtf8, "gksrmf");
		TS_ASSERT_EQUALS(ko.size(), 4u);
		TS_ASSERT_EQUALS(u8.size(), 6u);
		const uint32 a = Sci::decodeCodePagePair((byte)ko[0], (byte)ko[1], Common::kWindows949);
		const uint32 b = Sci::decodeCodePagePair((byte)ko[2], (byte)ko[3], Common::kWindows949);
		TS_ASSERT_EQUALS(utf8Of(a) + utf8Of(b), u8);
		TS_ASSERT_EQUALS(u8, utf8Of(0xD55C) + utf8Of(0xAE00));
	}

	void test_backspace_decomposes_in_utf8_too() {
		// 한 -> 하 -> ㅎ -> nothing
		TS_ASSERT_EQUALS(type(Sci::KoreanComposer::kUtf8, "gks~"), utf8Of(0xD558));
		TS_ASSERT_EQUALS(type(Sci::KoreanComposer::kUtf8, "gks~~"), utf8Of(0x314E));
		TS_ASSERT_EQUALS(type(Sci::KoreanComposer::kUtf8, "gks~~~"), Common::String());
	}

	void test_utf8_shows_a_lone_jamo_eucKr_holds_it_back() {
		// The legacy font has no glyph for a compatibility jamo, so EUC-KR
		// mode keeps ㅎ out of the string; the hi-res font set can draw it.
		TS_ASSERT_EQUALS(type(Sci::KoreanComposer::kUtf8, "g"), utf8Of(0x314E));
		TS_ASSERT_EQUALS(type(Sci::KoreanComposer::kCp949, "g"), Common::String());
	}

	void test_utf8_mode_never_writes_a_half_character() {
		const Common::String keys = "dkssudgktpdy";
		for (uint n = 1; n <= keys.size(); ++n) {
			Common::String part(keys.c_str(), n);
			const Common::String t = type(Sci::KoreanComposer::kUtf8, part.c_str());
			// every prefix must be well-formed UTF-8: lengths agree
			uint chars = Sci::utf8Length((const byte *)t.c_str());
			uint walked = 0;
			for (uint i = 0; i < t.size();) {
				int b;
				Sci::decodeUtf8Char((const byte *)t.c_str() + i, b);
				i += b;
				walked++;
			}
			TS_ASSERT_EQUALS(chars, walked);
		}
	}
};
