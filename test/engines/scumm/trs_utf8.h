#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"

#include "engines/scumm/text_utf8.h"
#include "engines/scumm/trs_bundle.h"

/**
 * UTF-8 .trs bundles (C11 T7, I18N_TEXT_DESIGN.md section 4.1): the body
 * BOM (EF BB BF after the room table) or ini text_encoding=utf8 marks one;
 * a bundle is named by the language code, and Korean still finds the
 * established korean.trs; with hi-res text off a UTF-8 bundle is transcoded
 * to the language's legacy code page, escapes kept.
 */
namespace {

void put16(Common::Array<byte> &b, uint16 v) {
	b.push_back(v & 0xFF);
	b.push_back(v >> 8);
}

void put32(Common::Array<byte> &b, uint32 v) {
	for (int i = 0; i < 4; i++)
		b.push_back((v >> (8 * i)) & 0xFF);
}

/// A two-entry SCVMTRS, one room, one script range; body BOM on request.
Common::Array<byte> twoEntryTrs(bool bom, const char *t0, const char *t1) {
	const char *o0 = "Hello";
	const char *o1 = "World";
	Common::Array<byte> b;
	const char magic[] = "SCVMTRS ";
	for (int i = 0; i < 8; i++)
		b.push_back(magic[i]);
	put16(b, 2);
	const uint32 indexPos = b.size();
	for (int i = 0; i < 2 * 10; i++)
		b.push_back(0);           // index filled in below
	b.push_back(1);               // one room
	b.push_back(0);               // room 0
	put16(b, 1);                  // one script range
	put32(b, 0);
	put16(b, 0);
	put16(b, 1);
	const uint32 bodyPos = b.size();
	if (bom) {
		b.push_back(0xEF);
		b.push_back(0xBB);
		b.push_back(0xBF);
	}
	const char *strs[4] = { o0, t0, o1, t1 };
	uint32 offs[4];
	for (int i = 0; i < 4; i++) {
		offs[i] = b.size();
		for (const char *p = strs[i]; *p; p++)
			b.push_back((byte)*p);
		b.push_back(0);
	}
	for (int e = 0; e < 2; e++) {
		const uint32 at = indexPos + e * 10;
		b[at] = e;
		b[at + 1] = 0;
		for (int k = 0; k < 4; k++) {
			b[at + 2 + k] = (offs[e * 2] >> (8 * k)) & 0xFF;
			b[at + 6 + k] = (offs[e * 2 + 1] >> (8 * k)) & 0xFF;
		}
	}
	(void)bodyPos;
	return b;
}

} // End of anonymous namespace

class ScummTrsUtf8TestSuite : public CxxTest::TestSuite {
public:
	void test_trs_body_bom() {
		const char *ja = "\xE3\x81\x93\xE3\x82\x93\xE3\x81\xAB\xE3\x81\xA1\xE3\x81\xAF";
		Common::Array<byte> marked = twoEntryTrs(true, ja, "x");
		Scumm::TrsHeader h;
		TS_ASSERT(Scumm::parseTrsHeader(marked.begin(), marked.size(), h));
		TS_ASSERT_EQUALS(h.numLines, 2u);
		TS_ASSERT(Scumm::trsBodyIsUtf8(marked.begin() + h.bodyPos, marked.size() - h.bodyPos));
		bool hint = false;
		TS_ASSERT(Scumm::decideTrsUtf8(marked.begin(), marked.size(), h, false, &hint));
		TS_ASSERT(!hint);

		// The same bundle without the BOM is a legacy (code page) bundle,
		// though it validates as UTF-8: that earns one hint, not UTF-8.
		Common::Array<byte> plain = twoEntryTrs(false, ja, "x");
		TS_ASSERT(Scumm::parseTrsHeader(plain.begin(), plain.size(), h));
		TS_ASSERT(!Scumm::trsBodyIsUtf8(plain.begin() + h.bodyPos, plain.size() - h.bodyPos));
		hint = false;
		TS_ASSERT(!Scumm::decideTrsUtf8(plain.begin(), plain.size(), h, false, &hint));
		TS_ASSERT(hint);

		// ini text_encoding=utf8 marks an unmarked bundle.
		TS_ASSERT(Scumm::decideTrsUtf8(plain.begin(), plain.size(), h, true, &hint));

		// A CP949 body (not valid UTF-8) gets no hint.
		Common::Array<byte> cp949 = twoEntryTrs(false, "\xC7\xD1\xB1\xDB", "x");
		TS_ASSERT(Scumm::parseTrsHeader(cp949.begin(), cp949.size(), h));
		hint = false;
		TS_ASSERT(!Scumm::decideTrsUtf8(cp949.begin(), cp949.size(), h, false, &hint));
		TS_ASSERT(!hint);
	}

	void test_trs_header_rejects_bad_data() {
		Common::Array<byte> b = twoEntryTrs(true, "a", "b");
		Scumm::TrsHeader h;
		TS_ASSERT(!Scumm::parseTrsHeader(b.begin(), 20, h));     // index cut short
		b[0] = 'X';
		TS_ASSERT(!Scumm::parseTrsHeader(b.begin(), b.size(), h));
	}

	void test_bundle_names_by_language_code() {
		Common::Array<Common::Path> names;
		Scumm::getTrsBundleNames(Common::KO_KOR, names);
		TS_ASSERT_EQUALS(names.size(), 2u);
		if (names.size() == 2) {
			// The language code first, the established Korean name second.
			TS_ASSERT_EQUALS(names[0].toString('/'), Common::String("ko.trs"));
			TS_ASSERT_EQUALS(names[1].toString('/'), Common::String("korean.trs"));
		}
		Scumm::getTrsBundleNames(Common::JA_JPN, names);
		TS_ASSERT_EQUALS(names.size(), 1u);
		if (names.size() == 1)
			TS_ASSERT_EQUALS(names[0].toString('/'), Common::String("ja.trs"));
		Scumm::getTrsBundleNames(Common::TH_THA, names);
		TS_ASSERT_EQUALS(names.size(), 1u);
		if (names.size() == 1)
			TS_ASSERT_EQUALS(names[0].toString('/'), Common::String("th.trs"));
		Scumm::getTrsBundleNames(Common::UNK_LANG, names);
		TS_ASSERT(names.empty());
		// Both Korean names are recognised as Korean by the detector.
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("ko.trs"), Common::KO_KOR);
		TS_ASSERT_EQUALS(Scumm::getTrsBundleLanguage("korean.trs"), Common::KO_KOR);
	}

	void test_transcode_keeps_escapes() {
		// "가" FF 0A 12 34 "@" "a" FF 07 03 80 "나": UTF-8 -> CP949, the
		// escapes and their argument bytes untouched.
		const byte src[] = { 0xEA, 0xB0, 0x80, 0xFF, 0x0A, 0x12, 0x34, '@', 'a', 0xFF, 0x07, 0x03, 0x80,
		                     0xEB, 0x82, 0x98 };
		const byte want[] = { 0xB0, 0xA1, 0xFF, 0x0A, 0x12, 0x34, '@', 'a', 0xFF, 0x07, 0x03, 0x80,
		                      0xB3, 0xAA };
		Common::Array<byte> out;
		Scumm::transcodeScummText(src, sizeof(src), Common::kUtf8, Common::kWindows949, out, nullptr);
		TS_ASSERT_EQUALS(out.size(), sizeof(want));
		if (out.size() == sizeof(want))
			TS_ASSERT_SAME_DATA(out.begin(), want, sizeof(want));

		// And back.
		Common::Array<byte> back;
		Scumm::transcodeScummText(want, sizeof(want), Common::kWindows949, Common::kUtf8, back, nullptr);
		TS_ASSERT_EQUALS(back.size(), sizeof(src));
		if (back.size() == sizeof(src))
			TS_ASSERT_SAME_DATA(back.begin(), src, sizeof(src));
	}

	void test_transcode_unmappable_and_invalid() {
		// Thai ก has no CP949 form: '?', and it is reported once. A byte
		// that is not UTF-8 at all (MI1's own glyph 0xFA) is kept as it is,
		// so a legacy bundle's raw bytes survive the round trip.
		const byte src[] = { 0xE0, 0xB8, 0x81, 'x', 0xE0, 0xB8, 0x81, 0xFA, 'y' };
		Common::Array<byte> out;
		Common::HashMap<uint32, bool> unmapped;
		Scumm::transcodeScummText(src, sizeof(src), Common::kUtf8, Common::kWindows949, out, &unmapped);
		const byte want[] = { '?', 'x', '?', 0xFA, 'y' };
		TS_ASSERT_EQUALS(out.size(), sizeof(want));
		if (out.size() == sizeof(want))
			TS_ASSERT_SAME_DATA(out.begin(), want, sizeof(want));
		TS_ASSERT_EQUALS(unmapped.size(), 1u);
		TS_ASSERT(unmapped.contains(0x0E01));
	}

	void test_legacy_page_for_language() {
		TS_ASSERT_EQUALS(Scumm::legacyTextPage(Common::KO_KOR), Common::kWindows949);
		TS_ASSERT_EQUALS(Scumm::legacyTextPage(Common::JA_JPN), Common::kWindows932);
		TS_ASSERT_EQUALS(Scumm::legacyTextPage(Common::ZH_CHN), Common::kWindows936);
		TS_ASSERT_EQUALS(Scumm::legacyTextPage(Common::ZH_TWN), Common::kWindows950);
		TS_ASSERT_EQUALS(Scumm::legacyTextPage(Common::TH_THA), Common::kCodePageInvalid);
		TS_ASSERT_EQUALS(Scumm::legacyTextPage(Common::DE_DEU), Common::kCodePageInvalid);
	}
};
