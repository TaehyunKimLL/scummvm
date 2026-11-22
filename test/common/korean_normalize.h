#include <cxxtest/TestSuite.h>

#include "common/hangul.h"
#include "common/str.h"
#include "common/ustr.h"

// normalizeKoreanInput(): what an input method sends, brought to what a word table holds (DESIGN.md section 6).
class KoreanNormalizeTestSuite : public CxxTest::TestSuite {
	static Common::U32String n(const Common::U32String &in) {
		Common::U32String s = in;
		Common::normalizeKoreanInput(s);
		return s;
	}

	static Common::U32String cps(const uint32 *p) {
		Common::U32String s;
		for (; *p; ++p)
			s += (Common::u32char_type_t)*p;
		return s;
	}

public:
	// L V T and L V composed: 한 (U+1112 U+1161 U+11AB), 가 (U+1100 U+1161), then a precomposed 가 + T = 각.
	void test_conjoiningJamoCompose() {
		const uint32 han[] = { 0x1112, 0x1161, 0x11AB, 0 };
		const uint32 ga[] = { 0x1100, 0x1161, 0 };
		const uint32 gak[] = { 0xAC00, 0x11A8, 0 };
		const uint32 word[] = { 0x1112, 0x1161, 0x11AB, 0x1100, 0x1173, 0x11AF, 0 }; // 한글
		TS_ASSERT_EQUALS(n(cps(han)), Common::U32String("\xED\x95\x9C"));
		TS_ASSERT_EQUALS(n(cps(ga)), Common::U32String("\xEA\xB0\x80"));
		TS_ASSERT_EQUALS(n(cps(gak)), Common::U32String("\xEA\xB0\x81"));
		TS_ASSERT_EQUALS(n(cps(word)), Common::U32String("\xED\x95\x9C\xEA\xB8\x80"));
	}

	// Last and first of each range, and what does not compose.
	void test_rangeEdges() {
		const uint32 last[] = { 0x1112, 0x1175, 0x11C2, 0 };   // 힣 U+D7A3
		const uint32 lone[] = { 0x1100, 0x11A8, 0 };           // L T: no syllable
		const uint32 lvt[] = { 0xAC01, 0x11A8, 0 };            // 각 + T: already has a final
		const uint32 notT[] = { 0xAC00, 0x11A7, 0 };           // U+11A7 is no final
		const uint32 compat[] = { 0x3131, 0x314F, 0 };         // compatibility jamo stay
		TS_ASSERT_EQUALS(n(cps(last)), Common::U32String("\xED\x9E\xA3"));
		TS_ASSERT_EQUALS(n(cps(lone)), cps(lone));
		TS_ASSERT_EQUALS(n(cps(lvt)), cps(lvt));
		TS_ASSERT_EQUALS(n(cps(notT)), cps(notT));
		TS_ASSERT_EQUALS(n(cps(compat)), cps(compat));
	}

	// Full-width ASCII and the ideographic space become ASCII; case is kept.
	void test_fullwidthFolds() {
		const uint32 in[] = { 0xFF2C, 0xFF4F, 0xFF4F, 0xFF4B, 0x3000, 0xFF01, 0xFF5E, 0xFF5F, 0 };
		const uint32 out[] = { 'L', 'o', 'o', 'k', ' ', '!', '~', 0xFF5F, 0 };
		TS_ASSERT_EQUALS(n(cps(in)), cps(out));
		TS_ASSERT_EQUALS(n(Common::U32String("open door")), Common::U32String("open door"));
		TS_ASSERT_EQUALS(n(Common::U32String()), Common::U32String());
	}
};
