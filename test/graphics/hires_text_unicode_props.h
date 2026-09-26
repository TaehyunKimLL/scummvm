#include <cxxtest/TestSuite.h>

#include "common/language.h"
#include "common/list.h"
#include "common/str.h"
#include "common/util.h"
#include "graphics/hires_text/unicode_props.h"

namespace {

struct OldEawRun {
	uint32 first, last;
};

// A verbatim copy of the table TtfGlyphSource::isWide() searched before the
// move to Graphics::Unicode (glyph_source_ttf.cpp at i18n bee83518ad),
// kept only as the fixture of test_is_wide_moved.
const OldEawRun kOldEawWideRuns[] = {
	{ 0x01100, 0x0115F },
	{ 0x0231A, 0x0231B },
	{ 0x02329, 0x0232A },
	{ 0x023E9, 0x023EC },
	{ 0x023F0, 0x023F0 },
	{ 0x023F3, 0x023F3 },
	{ 0x025FD, 0x025FE },
	{ 0x02614, 0x02615 },
	{ 0x02630, 0x02637 },
	{ 0x02648, 0x02653 },
	{ 0x0267F, 0x0267F },
	{ 0x0268A, 0x0268F },
	{ 0x02693, 0x02693 },
	{ 0x026A1, 0x026A1 },
	{ 0x026AA, 0x026AB },
	{ 0x026BD, 0x026BE },
	{ 0x026C4, 0x026C5 },
	{ 0x026CE, 0x026CE },
	{ 0x026D4, 0x026D4 },
	{ 0x026EA, 0x026EA },
	{ 0x026F2, 0x026F3 },
	{ 0x026F5, 0x026F5 },
	{ 0x026FA, 0x026FA },
	{ 0x026FD, 0x026FD },
	{ 0x02705, 0x02705 },
	{ 0x0270A, 0x0270B },
	{ 0x02728, 0x02728 },
	{ 0x0274C, 0x0274C },
	{ 0x0274E, 0x0274E },
	{ 0x02753, 0x02755 },
	{ 0x02757, 0x02757 },
	{ 0x02795, 0x02797 },
	{ 0x027B0, 0x027B0 },
	{ 0x027BF, 0x027BF },
	{ 0x02B1B, 0x02B1C },
	{ 0x02B50, 0x02B50 },
	{ 0x02B55, 0x02B55 },
	{ 0x02E80, 0x02E99 },
	{ 0x02E9B, 0x02EF3 },
	{ 0x02F00, 0x02FD5 },
	{ 0x02FF0, 0x0303E },
	{ 0x03041, 0x03096 },
	{ 0x03099, 0x030FF },
	{ 0x03105, 0x0312F },
	{ 0x03131, 0x0318E },
	{ 0x03190, 0x031E5 },
	{ 0x031EF, 0x0321E },
	{ 0x03220, 0x03247 },
	{ 0x03250, 0x0A48C },
	{ 0x0A490, 0x0A4C6 },
	{ 0x0A960, 0x0A97C },
	{ 0x0AC00, 0x0D7A3 },
	{ 0x0F900, 0x0FAFF },
	{ 0x0FE10, 0x0FE19 },
	{ 0x0FE30, 0x0FE52 },
	{ 0x0FE54, 0x0FE66 },
	{ 0x0FE68, 0x0FE6B },
	{ 0x0FF01, 0x0FF60 },
	{ 0x0FFE0, 0x0FFE6 },
	{ 0x16FE0, 0x16FE4 },
	{ 0x16FF0, 0x16FF1 },
	{ 0x17000, 0x187F7 },
	{ 0x18800, 0x18CD5 },
	{ 0x18CFF, 0x18D08 },
	{ 0x1AFF0, 0x1AFF3 },
	{ 0x1AFF5, 0x1AFFB },
	{ 0x1AFFD, 0x1AFFE },
	{ 0x1B000, 0x1B122 },
	{ 0x1B132, 0x1B132 },
	{ 0x1B150, 0x1B152 },
	{ 0x1B155, 0x1B155 },
	{ 0x1B164, 0x1B167 },
	{ 0x1B170, 0x1B2FB },
	{ 0x1D300, 0x1D356 },
	{ 0x1D360, 0x1D376 },
	{ 0x1F004, 0x1F004 },
	{ 0x1F0CF, 0x1F0CF },
	{ 0x1F18E, 0x1F18E },
	{ 0x1F191, 0x1F19A },
	{ 0x1F200, 0x1F202 },
	{ 0x1F210, 0x1F23B },
	{ 0x1F240, 0x1F248 },
	{ 0x1F250, 0x1F251 },
	{ 0x1F260, 0x1F265 },
	{ 0x1F300, 0x1F320 },
	{ 0x1F32D, 0x1F335 },
	{ 0x1F337, 0x1F37C },
	{ 0x1F37E, 0x1F393 },
	{ 0x1F3A0, 0x1F3CA },
	{ 0x1F3CF, 0x1F3D3 },
	{ 0x1F3E0, 0x1F3F0 },
	{ 0x1F3F4, 0x1F3F4 },
	{ 0x1F3F8, 0x1F43E },
	{ 0x1F440, 0x1F440 },
	{ 0x1F442, 0x1F4FC },
	{ 0x1F4FF, 0x1F53D },
	{ 0x1F54B, 0x1F54E },
	{ 0x1F550, 0x1F567 },
	{ 0x1F57A, 0x1F57A },
	{ 0x1F595, 0x1F596 },
	{ 0x1F5A4, 0x1F5A4 },
	{ 0x1F5FB, 0x1F64F },
	{ 0x1F680, 0x1F6C5 },
	{ 0x1F6CC, 0x1F6CC },
	{ 0x1F6D0, 0x1F6D2 },
	{ 0x1F6D5, 0x1F6D7 },
	{ 0x1F6DC, 0x1F6DF },
	{ 0x1F6EB, 0x1F6EC },
	{ 0x1F6F4, 0x1F6FC },
	{ 0x1F7E0, 0x1F7EB },
	{ 0x1F7F0, 0x1F7F0 },
	{ 0x1F90C, 0x1F93A },
	{ 0x1F93C, 0x1F945 },
	{ 0x1F947, 0x1F9FF },
	{ 0x1FA70, 0x1FA7C },
	{ 0x1FA80, 0x1FA89 },
	{ 0x1FA8F, 0x1FAC6 },
	{ 0x1FACE, 0x1FADC },
	{ 0x1FADF, 0x1FAE9 },
	{ 0x1FAF0, 0x1FAF8 },
	{ 0x20000, 0x2FFFD },
	{ 0x30000, 0x3FFFD }
};

bool oldIsWide(uint32 cp) {
	int lo = 0, hi = ARRAYSIZE(kOldEawWideRuns) - 1;
	while (lo <= hi) {
		const int mid = lo + (hi - lo) / 2;
		if (cp < kOldEawWideRuns[mid].first)
			hi = mid - 1;
		else if (cp > kOldEawWideRuns[mid].last)
			lo = mid + 1;
		else
			return true;
	}
	return false;
}

// engines/sci/graphics/text16.cpp text16_shiftJIS_punctuation_SCI01, the
// Shift-JIS pairs read little-endian, and the same 29 characters decoded
// as CP932 (I18N_TEXT_DESIGN.md section 4.3; the generator asserts that the
// decode gives exactly this list).
const uint16 kSci01Sjis[] = {
	0x9F82, 0xA182, 0xA382, 0xA582, 0xA782, 0xC182, 0xE182, 0xE382, 0xE582, 0xEC82, 0x4083, 0x4283,
	0x4483, 0x4683, 0x4883, 0x6283, 0x8383, 0x8583, 0x8783, 0x8E83, 0x9583, 0x9683, 0x5B81, 0x4181,
	0x4281, 0x7681, 0x7881, 0x4981, 0x4881
};
const uint32 kSci01Cp932[] = {
	0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E,
	0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE,
	0x30F5, 0x30F6, 0x30FC, 0x3001, 0x3002, 0x300D, 0x300F, 0xFF01, 0xFF1F
};

} // anonymous namespace

class HiResTextUnicodePropsTestSuite : public CxxTest::TestSuite {
public:
	void test_is_combining_thai() {
		for (uint32 cp = 0x0E00; cp <= 0x0E7F; cp++) {
			const bool expected = cp == 0x0E31 || (cp >= 0x0E34 && cp <= 0x0E3A) || (cp >= 0x0E47 && cp <= 0x0E4E);
			TSM_ASSERT_EQUALS(Common::String::format("U+%04X", cp).c_str(), Graphics::Unicode::isCombining(cp), expected);
		}
		TS_ASSERT(!Graphics::Unicode::isCombining(0x0E33));	// SARA AM: Lo, spacing
		TS_ASSERT(!Graphics::Unicode::isCombining(0x0E01));
		TS_ASSERT(Graphics::Unicode::isCombining(0x0300));	// combining grave
		TS_ASSERT(Graphics::Unicode::isCombining(0x0301));	// combining acute
		TS_ASSERT(!Graphics::Unicode::isCombining(0xAC00));
		TS_ASSERT(!Graphics::Unicode::isCombining(0x0041));
	}

	void test_is_wide_moved() {
		uint32 mismatches = 0, first = 0;
		for (uint32 cp = 0; cp <= 0x10FFFF; cp++) {
			if (Graphics::Unicode::isWide(cp) != oldIsWide(cp)) {
				if (!mismatches)
					first = cp;
				mismatches++;
			}
		}
		TSM_ASSERT_EQUALS(Common::String::format("first mismatch U+%04X", first).c_str(), mismatches, 0u);
	}

	void test_kinsoku_tables() {
		TS_ASSERT_EQUALS(ARRAYSIZE(kSci01Sjis), ARRAYSIZE(kSci01Cp932));
		TS_ASSERT_EQUALS(ARRAYSIZE(kSci01Cp932), 29u);
		for (uint i = 0; i < ARRAYSIZE(kSci01Cp932); i++) {
			const uint32 cp = kSci01Cp932[i];
			TSM_ASSERT(Common::String::format("U+%04X (SJIS %04X)", cp, kSci01Sjis[i]).c_str(), Graphics::Unicode::kinsokuNoStart(cp));
			TS_ASSERT(!Graphics::Unicode::kinsokuNoEnd(cp));
		}
		TS_ASSERT(Graphics::Unicode::kinsokuNoEnd(0x300C));		// 「
		TS_ASSERT(!Graphics::Unicode::kinsokuNoStart(0x300C));
		TS_ASSERT(!Graphics::Unicode::kinsokuNoStart(0x3042));	// あ
		TS_ASSERT(!Graphics::Unicode::kinsokuNoEnd(0x3042));
		// JIS X 4051 additions: a closer and an opener.
		TS_ASSERT(Graphics::Unicode::kinsokuNoStart(0xFF09));	// ）
		TS_ASSERT(Graphics::Unicode::kinsokuNoEnd(0xFF08));		// （
		// Context-dependent ASCII is the layout stage's, not the table's.
		TS_ASSERT(!Graphics::Unicode::kinsokuNoStart(','));
		TS_ASSERT(!Graphics::Unicode::kinsokuNoEnd('('));
	}

	void test_thai_classes() {
		TS_ASSERT(Graphics::Unicode::isThaiLeadingVowel(0x0E40));
		TS_ASSERT(!Graphics::Unicode::isThaiBase(0x0E48));
		TS_ASSERT(Graphics::Unicode::isThaiFollowingVowel(0x0E33));
		TS_ASSERT(Graphics::Unicode::isThaiBase(0x0E01));
		TS_ASSERT(Graphics::Unicode::isThaiBase(0x0E40));
		TS_ASSERT(!Graphics::Unicode::isThaiLeadingVowel(0x0E01));
		TS_ASSERT(!Graphics::Unicode::isThaiFollowingVowel(0x0E31));
	}
};

class CommonLanguageThaiVietnameseTestSuite : public CxxTest::TestSuite {
public:
	void test_language_codes() {
		TS_ASSERT_EQUALS(Common::parseLanguage("th"), Common::TH_THA);
		TS_ASSERT_EQUALS(Common::parseLanguage("vi"), Common::VI_VNM);
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageCode(Common::VI_VNM)), "vi");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageCode(Common::TH_THA)), "th");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageLocale(Common::TH_THA)), "th_TH");
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageDescription(Common::VI_VNM)), "Vietnamese");
		TS_ASSERT_EQUALS(Common::parseLanguage("ko"), Common::KO_KOR);
		TS_ASSERT_EQUALS(Common::String(Common::getLanguageCode(Common::KO_KOR)), "ko");
		TS_ASSERT_EQUALS(Common::parseLanguage("tw"), Common::ZH_TWN);
	}

	// The launcher's language pop-up is filled from g_languages, the same
	// table getLanguageList() walks.
	void test_language_list_has_thai_and_vietnamese() {
		const Common::List<Common::String> list = Common::getLanguageList();
		bool th = false, vi = false;
		for (Common::List<Common::String>::const_iterator it = list.begin(); it != list.end(); ++it) {
			if (*it == "th")
				th = true;
			if (*it == "vi")
				vi = true;
		}
		TS_ASSERT(th);
		TS_ASSERT(vi);
	}
};
