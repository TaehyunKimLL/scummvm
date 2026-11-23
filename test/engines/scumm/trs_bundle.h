#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_text.h"
#include "engines/scumm/trs_bundle.h"

// Bundle names and the CP949 -> UTF-8 rebuild of a body (engines/scumm/trs_bundle.h).
class ScummTrsBundleTestSuite : public CxxTest::TestSuite {
public:
	void test_names() {
		Common::Array<Common::Path> names;
		Scumm::trsBundleNames(Common::KO_KOR, names);
		TS_ASSERT_EQUALS(names.size(), 2u);
		TS_ASSERT_EQUALS(names[0].toString(), "ko.trs");
		TS_ASSERT_EQUALS(names[1].toString(), "korean.trs");
		Scumm::trsBundleNames(Common::DE_DEU, names);
		TS_ASSERT_EQUALS(names.size(), 1u);
		Scumm::trsBundleNames(Common::UNK_LANG, names);
		TS_ASSERT_EQUALS(names.size(), 0u);
		const byte bom[] = { 0xEF, 0xBB, 0xBF, 'a' };
		TS_ASSERT(Scumm::trsBodyIsUtf8(bom, 4));
		TS_ASSERT(!Scumm::trsBodyIsUtf8(bom + 1, 3));
	}

	void test_nul_ends_a_bundle_string() {
		// MM's bundle: an original ends in an escape whose argument is 0 ("sound " FF 04 C8 00); the next string
		// starts after that NUL. A trail byte 0xFE (BC FE) stays part of its syllable.
		const byte body[] = { 's', 0xFF, 0x04, 0xC8, 0x00, 0xBC, 0xFE, 0x00 };
		Common::Array<uint32> orig, trans;
		orig.push_back(0);
		trans.push_back(5);
		Common::Array<byte> out;
		uint unmapped = 0;
		TS_ASSERT(Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
		TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[0]]), "\xec\x88\xad");
		TS_ASSERT_EQUALS(unmapped, 0u);
	}

	void test_body_to_utf8() {
		// original "Hi" @0, translation 안녕 @3 (CP949 BE C8 B3 E7), original 0x99 @8, translation "x" @10
		const byte body[] = { 'H', 'i', 0, 0xBE, 0xC8, 0xB3, 0xE7, 0, 0x99, 0, 'x', 0 };
		Common::Array<uint32> orig, trans;
		orig.push_back(0); orig.push_back(8);
		trans.push_back(3); trans.push_back(10);
		Common::Array<byte> out;
		uint unmapped = 0;
		TS_ASSERT(Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
		TS_ASSERT(Scumm::trsBodyIsUtf8(out.begin(), out.size()));
		TS_ASSERT_EQUALS(orig[0], 3u);
		TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[0]]), "\xec\x95\x88\xeb\x85\x95");
		TS_ASSERT_EQUALS(out[orig[1]], 0x99);	// the game's key byte, untouched
		TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[1]]), "x");
		TS_ASSERT_EQUALS(unmapped, 0u);
		trans[1] = 11;	// inside a string
		TS_ASSERT(!Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
	}

	void test_translation_runs_past_a_nul_argument() {
		// MI2 translations hold NUL as an escape argument: 안 FF 04 00 01 녕 (code 4 takes two arguments, 00 and 01).
		// The whole string is converted, not only the part before that NUL, or the later boundary would judge the
		// mixed string as CP949 and read its UTF-8 head as CP949.
		const byte body[] = { 'a', 0, 0xBE, 0xC8, 0xFF, 0x04, 0x00, 0x01, 0xB3, 0xE7, 0, 'z', 0 };
		Common::Array<uint32> orig, trans;
		orig.push_back(0);
		trans.push_back(2);
		Common::Array<byte> out;
		uint unmapped = 0;
		TS_ASSERT(Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
		const byte *s = &out[trans[0]];
		const byte want[] = { 0xEC, 0x95, 0x88, 0xFF, 0x04, 0x00, 0x01, 0xEB, 0x85, 0x95, 0 };
		TS_ASSERT_EQUALS(memcmp(s, want, sizeof(want)), 0);
		TS_ASSERT(Scumm::textIsUtf8(s, s + sizeof(want)));
		TS_ASSERT_EQUALS(out[out.size() - 2], 'z');	// the string after it is intact
		TS_ASSERT_EQUALS(unmapped, 0u);
	}

	void test_translation_stops_at_the_next_entry() {
		// A translation ends with an escape whose last argument is its terminating NUL (FF 04 C8 00): the escape would
		// run on into the next entry; the next entry (a known offset) is where it stops.
		const byte body[] = { 'a', 0, 0xBE, 0xC8, 0xFF, 0x04, 0xC8, 0, 0xBC, 0xFE, 0 };
		Common::Array<uint32> orig, trans;
		orig.push_back(0);
		trans.push_back(2);
		trans.push_back(8);
		orig.push_back(0);
		Common::Array<byte> out;
		uint unmapped = 0;
		TS_ASSERT(Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
		const byte first[] = { 0xEC, 0x95, 0x88, 0xFF, 0x04, 0xC8, 0 };
		TS_ASSERT_EQUALS(memcmp(&out[trans[0]], first, sizeof(first)), 0);
		TS_ASSERT_EQUALS(Common::String((const char *)&out[trans[1]]), "\xec\x88\xad");
		TS_ASSERT_EQUALS(unmapped, 0u);
	}

	void test_unterminated_translation_keeps_an_empty_next_entry() {
		// 안 FF 04 C8 with no NUL, then an empty string at 7 that is an entry of its own: it must not be skipped.
		const byte body[] = { 'a', 0, 0xBE, 0xC8, 0xFF, 0x04, 0xC8, 0, 0 };
		Common::Array<uint32> orig, trans;
		orig.push_back(0);
		orig.push_back(0);
		trans.push_back(2);
		trans.push_back(7);
		Common::Array<byte> out;
		uint unmapped = 0;
		TS_ASSERT(Scumm::convertTrsBodyToUtf8(body, sizeof(body), orig, trans, out, unmapped));
		TS_ASSERT_EQUALS(out[trans[0]], 0xEC);
		TS_ASSERT_EQUALS(out[trans[1]], 0);
		TS_ASSERT_EQUALS(trans[1] + 2, out.size());	// and the terminator after it is there too
	}
};
