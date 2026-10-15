#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"

#include "engines/scumm/text_utf8.h"

/**
 * A save holds translated text as it was drawn when it was made: verb
 * names, object and actor names and string resources from a CP949 bundle
 * are CP949. Loaded under the same translation in UTF-8 they are
 * converted (convertSavedText()); the save says which encoding it holds in
 * a tagged trailer after the serialized state, and a save without one is
 * judged string by string.
 */
namespace {

Common::Array<byte> bytes(const char *s, uint n) {
	Common::Array<byte> a;
	for (uint i = 0; i < n; i++)
		a.push_back((byte)s[i]);
	return a;
}

bool sameAs(const Common::Array<byte> &a, const char *s, uint n) {
	if (a.size() != n)
		return false;
	for (uint i = 0; i < n; i++)
		if (a[i] != (byte)s[i])
			return false;
	return true;
}

} // End of anonymous namespace

class ScummSavedTextTestSuite : public CxxTest::TestSuite {
public:
	void test_legacy_hangul_to_utf8() {
		// "한글" in CP949 -> UTF-8.
		const char src[] = "\xC7\xD1\xB1\xDB";
		Common::Array<byte> out;
		Scumm::legacyScummTextToUtf8((const byte *)src, 4, Common::KO_KOR, 5, out);
		TS_ASSERT(sameAs(out, "\xED\x95\x9C\xEA\xB8\x80", 6));
	}

	void test_legacy_keeps_escapes_and_raw_bytes() {
		// "가" FF 0A B0 A1 (a talkie escape whose arguments look like a
		// pair) '@' 0x92 's' 0xFA "나": the escape and its arguments, '@'
		// and the game's own bytes 0x92 and 0xFA (not a lead byte of the
		// release's split, 0xB0-0xD0) are kept.
		const char src[] = "\xB0\xA1\xFF\x0A\xB0\xA1@\x92s\xFA\xB3\xAA";
		const char want[] = "\xEA\xB0\x80\xFF\x0A\xB0\xA1@\x92s\xFA\xEB\x82\x98";
		Common::Array<byte> out;
		Scumm::legacyScummTextToUtf8((const byte *)src, sizeof(src) - 1, Common::KO_KOR, 5, out);
		TS_ASSERT(sameAs(out, want, sizeof(want) - 1));
	}

	void test_looks_utf8() {
		TS_ASSERT(Scumm::scummTextLooksUtf8((const byte *)"\xED\x95\x9C\xEA\xB8\x80", 6, 5));
		TS_ASSERT(!Scumm::scummTextLooksUtf8((const byte *)"\xC7\xD1\xB1\xDB", 4, 5));
		TS_ASSERT(!Scumm::scummTextLooksUtf8((const byte *)"Open", 4, 5));        // nothing to tell
		TS_ASSERT(!Scumm::scummTextLooksUtf8((const byte *)"Don\x92t", 5, 5));
		// MI1's 0xFA in a UTF-8 line, and an escape: still UTF-8.
		TS_ASSERT(Scumm::scummTextLooksUtf8((const byte *)"St.\xFA\xEA\xB0\x80\xFF\x0A\x81\x92", 10, 5));
	}

	void test_unmarked_save_under_utf8() {
		Common::Array<byte> out;
		// A CP949 verb name, NUL and what follows it in the buffer.
		Common::Array<byte> verb = bytes("\xBF\xAD\xB1\xE2\0xy", 7);
		TS_ASSERT(Scumm::convertSavedText(verb.begin(), verb.size(), Common::KO_KOR, 5,
										  Scumm::kSavedTextUnknown, true, out));
		TS_ASSERT(sameAs(out, "\xEC\x97\xB4\xEA\xB8\xB0\0xy", 9));    // "열기"

		// Already UTF-8, English, a raw apostrophe: left alone.
		Common::Array<byte> utf8 = bytes("\xEC\x97\xB4\xEA\xB8\xB0\0", 7);
		TS_ASSERT(!Scumm::convertSavedText(utf8.begin(), utf8.size(), Common::KO_KOR, 5,
										   Scumm::kSavedTextUnknown, true, out));
		Common::Array<byte> en = bytes("Open\0", 5);
		TS_ASSERT(!Scumm::convertSavedText(en.begin(), en.size(), Common::KO_KOR, 5,
										   Scumm::kSavedTextUnknown, true, out));
		Common::Array<byte> apos = bytes("Don\x92t\0", 6);
		TS_ASSERT(!Scumm::convertSavedText(apos.begin(), apos.size(), Common::KO_KOR, 5,
										   Scumm::kSavedTextUnknown, true, out));
		// The same CP949 text with the legacy bundle active: kept.
		TS_ASSERT(!Scumm::convertSavedText(verb.begin(), verb.size(), Common::KO_KOR, 5,
										   Scumm::kSavedTextUnknown, false, out));
	}

	void test_marked_saves() {
		Common::Array<byte> out;
		// "é" as UTF-8 is also a CP949 pair. Unmarked it is taken as UTF-8;
		// a save marked legacy is converted.
		Common::Array<byte> amb = bytes("\xC3\xA9\0", 3);
		TS_ASSERT(!Scumm::convertSavedText(amb.begin(), amb.size(), Common::KO_KOR, 5,
										   Scumm::kSavedTextUnknown, true, out));
		TS_ASSERT(Scumm::convertSavedText(amb.begin(), amb.size(), Common::KO_KOR, 5,
										  Scumm::kSavedTextLegacy, true, out));
		TS_ASSERT(sameAs(out, "\xEC\xB1\x95\0", 4));    // U+CC55
		// A UTF-8 save under UTF-8: untouched.
		Common::Array<byte> utf8 = bytes("\xEC\x97\xB4\xEA\xB8\xB0\0", 7);
		TS_ASSERT(!Scumm::convertSavedText(utf8.begin(), utf8.size(), Common::KO_KOR, 5,
										   Scumm::kSavedTextUtf8, true, out));
		// A UTF-8 save under a CP949 bundle: back to CP949, escapes and
		// raw bytes kept.
		Common::Array<byte> mixed = bytes("\xEC\x97\xB4\xFF\x0A\x01\x02\xFA\xEA\xB8\xB0\0", 12);
		TS_ASSERT(Scumm::convertSavedText(mixed.begin(), mixed.size(), Common::KO_KOR, 5,
										  Scumm::kSavedTextUtf8, false, out));
		TS_ASSERT(sameAs(out, "\xBF\xAD\xFF\x0A\x01\x02\xFA\xB1\xE2\0", 10));
		// A legacy save under a legacy bundle: untouched.
		Common::Array<byte> cp = bytes("\xBF\xAD\0", 3);
		TS_ASSERT(!Scumm::convertSavedText(cp.begin(), cp.size(), Common::KO_KOR, 5,
										   Scumm::kSavedTextLegacy, false, out));
		// A language without a legacy page converts nothing.
		TS_ASSERT(!Scumm::convertSavedText(cp.begin(), cp.size(), Common::EN_ANY, 5,
										   Scumm::kSavedTextLegacy, true, out));
	}

	void test_trailer_round_trip() {
		for (int utf8 = 0; utf8 < 2; utf8++) {
			Common::MemoryWriteStreamDynamic w(DisposeAfterUse::YES);
			Scumm::writeSavedTextEncoding(w, utf8 != 0);
			Common::MemoryReadStream r(w.getData(), w.size());
			TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(r),
							 utf8 ? Scumm::kSavedTextUtf8 : Scumm::kSavedTextLegacy);
		}
		// A save that ends with its state (no trailer), or has other bytes.
		Common::MemoryReadStream empty((const byte *)"", 0);
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(empty), Scumm::kSavedTextUnknown);
		Common::MemoryReadStream other((const byte *)"ABCD\x01", 5);
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(other), Scumm::kSavedTextUnknown);
	}
};
