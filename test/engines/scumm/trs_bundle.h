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
};
