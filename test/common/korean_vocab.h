#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/memstream.h"
#include "common/ptr.h"
#include "common/str-enc.h"

#include "test/korean_vocab_fixture.h"
#include "test/korean_vocab_golden.h"
#include "test/system/null_osystem.h"

// The word table common/ owns for SCI (SCIKOR.DAT, sci-ko.dat) and AGI (AGIKOR.DAT, agi-ko.dat): one layout, the magic
// says whose it is; version 2 (no header) and version 3 (u32 offsets, fingerprint header).
class KoreanVocabTestSuite : public CxxTest::TestSuite {
	static Common::U32String u(const char *utf8) {
		return Common::String(utf8).decode(Common::kUtf8);
	}

	static const char *header() {
		return "#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci "
		       "vocab=fedcba9876543210fedcba9876543210";
	}

	static bool loadImage(Common::KoreanVocabulary &v, const Common::Array<byte> &img, const char *magicWanted,
	                      const Common::TranslationFileHeader &expected = Common::TranslationFileHeader()) {
		Common::MemoryReadStream s(img.data(), img.size());
		return v.loadFromStream(s, magicWanted, "fixture", expected);
	}

	static Common::Array<byte> rowsImage(const char *magicInFile, uint32 version, const char *head = "") {
		static const KoreanVocabFixture::Row rows[] = {
			{ "\xEC\x97\xB4\xEC\x87\xA0", 12, 0 },                               // 열쇠 key
			{ "\xEC\xA7\x91\xEB\x8B\xA4", 20, 1 },                               // 집다 get (verb)
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 30, 0x8001 }, // 들여다봐 look+
			{ "\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90", 169, 0 },     //          in
		};
		return KoreanVocabFixture::build(magicInFile, rows, ARRAYSIZE(rows), version, head);
	}

	static bool loadJoinRows(Common::KoreanVocabulary &v, uint32 version) {
		static const KoreanVocabFixture::Row rows[] = {
			{ "나무", 10, 0 },       // tree
			{ "보다", 11, 1 },       // look (verb)
			{ "물을내리다", 12, 1 }, // flush (verb, stored joined)
			{ "고기", 13, 0 },       // meat
			{ "고다", 14, 1 },       // a verb that 고기 must not become
			{ "사다", 15, 1 },       // buy
			{ "웃다", 16, 1 },       // laugh
		};
		const char *head = "#! format=1 game=kq1 fingerprint=0123456789abcdef0123456789abcdef engine=sci";
		return loadImage(v, KoreanVocabFixture::build("SCIKOR", rows, ARRAYSIZE(rows), version, head), "SCIKOR");
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_magic_is_checked() {
		Common::KoreanVocabulary v;
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 2), "AGIKOR"));
		TS_ASSERT(!v.isLoaded());
		TS_ASSERT(loadImage(v, rowsImage("AGIKOR", 2), "AGIKOR"));
		TS_ASSERT_EQUALS(v.size(), 4u);
		TS_ASSERT_EQUALS(v.getVersion(), 2u);
		TS_ASSERT(!v.getHeader().present());
	}

	void test_exact_and_stemmed_lookup() {
		for (uint32 version = 2; version <= 3; ++version) {
			Common::KoreanVocabulary v;
			const char *head = "#! format=1 game=lsl1 fingerprint=0123456789abcdef0123456789abcdef engine=agi";
			TS_ASSERT(loadImage(v, rowsImage("AGIKOR", version, head), "AGIKOR"));
			uint16 g[4], c[4];
			uint8 role = 0xFF;
			TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(g[0], 12);
			TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleNone);
			// 열쇠를: the object particle is stripped and named
			TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(g[0], 12);
			TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleObject);
			// 열쇠로: with
			TS_ASSERT_EQUALS(v.lookupAll(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA1\x9C"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(role, (uint8)Common::kKoreanRoleWith);
			// exact only: 열쇠를 is a reading, not a stored form
			TS_ASSERT_EQUALS(v.lookupExact(u("\xEC\x97\xB4\xEC\x87\xA0\xEB\xA5\xBC"), g, c, 4), 0u);
			TS_ASSERT_EQUALS(v.lookupExact(u("\xEC\x97\xB4\xEC\x87\xA0"), g, c, 4), 1u);
			uint16 group = 0, wclass = 0;
			TS_ASSERT(v.lookup(u("\xEC\xA7\x91\xEB\x8B\xA4"), group, wclass));
			TS_ASSERT_EQUALS(group, 20);
			TS_ASSERT_EQUALS(wclass, 1);
			TS_ASSERT(!v.lookup(u("abc"), group, wclass));
		}
	}

	void test_sequence_rows_come_back_in_order() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadImage(v, rowsImage("AGIKOR", 2), "AGIKOR"));
		uint16 g[4], c[4];
		uint8 role;
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90"), g, c, 4, role), 2u);
		TS_ASSERT_EQUALS(g[0], 30);
		TS_ASSERT(c[0] & Common::KoreanVocabulary::kSequenceFlag);
		TS_ASSERT_EQUALS(g[1], 169);
		// max is respected
		TS_ASSERT_EQUALS(v.lookupAll(u("\xEB\x93\xA4\xEC\x97\xAC\xEB\x8B\xA4\xEB\xB4\x90"), g, c, 1, role), 1u);
	}

	// Two typed words are one word only when the table stores them joined: the joined form as typed, or the first
	// word as typed with a reading of the second (물을 내린다 -> 물을내리다). A reading of the joined string must never
	// swallow the second word: 나무 보다 is tree + look, not 나무보다 read as 나무 + the comparative particle.
	void test_a_pair_joins_only_on_a_stored_joined_form() {
		for (uint32 version = 2; version <= 3; ++version) {
			Common::KoreanVocabulary v;
			TS_ASSERT(loadJoinRows(v, version));
			TS_ASSERT(v.lookupJoined(u("물을"), u("내린다")));
			TS_ASSERT(v.lookupJoined(u("물을"), u("내리다")));
			TS_ASSERT(!v.lookupJoined(u("나무"), u("보다")));
			TS_ASSERT(!v.lookupJoined(u("나무"), u("처럼")));
			TS_ASSERT(!v.lookupJoined(u("나무"), u("이다")));
			TS_ASSERT(!v.lookupJoined(u("나무를"), u("보라")));
			// An auxiliary verb typed apart is still the verb's own ending: 사 줘 is 사줘 (buy), 웃어 봐 is 웃어봐.
			TS_ASSERT(v.lookupJoined(u("사"), u("줘")));
			TS_ASSERT(v.lookupJoined(u("웃어"), u("봐")));
			uint16 g[4], c[4];
			uint8 role;
			TS_ASSERT_EQUALS(v.lookupAll(u("나무보다"), g, c, 4, role), 1u);
			TS_ASSERT_EQUALS(g[0], 10);
		}
	}

	// A noun the table holds wins over a verb reading of it: 고기 is meat, never 고다 + the -기 nominaliser.
	void test_a_stored_noun_wins_over_a_gi_reading() {
		Common::KoreanVocabulary v;
		TS_ASSERT(loadJoinRows(v, 3));
		uint16 g[4], c[4];
		uint8 role;
		TS_ASSERT_EQUALS(v.lookupAll(u("고기"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 13);
		TS_ASSERT_EQUALS(v.lookupAll(u("고기를"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 13);
		TS_ASSERT_EQUALS(v.lookupAll(u("보기"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 11);
		TS_ASSERT_EQUALS(v.lookupAll(u("보라"), g, c, 4, role), 1u);
		TS_ASSERT_EQUALS(g[0], 11);
	}

	void test_truncated_table_is_refused() {
		static const byte bad[] = { 'A', 'G', 'I', 'K', 'O', 'R', 0, 0, 2, 0, 0, 0, 9, 0, 0, 0 };
		Common::MemoryReadStream s(bad, sizeof(bad));
		Common::KoreanVocabulary v;
		TS_ASSERT(!v.loadFromStream(s, "AGIKOR", "bad"));
		TS_ASSERT(!v.isLoaded());
	}

	// Bytes k6bake.py wrote (test/korean_vocab_golden.h) read back as the rows of its master, in both versions.
	void test_golden_tables_from_k6bake() {
		Common::KoreanVocabulary v2, v3;
		Common::MemoryReadStream s2(kKoreanVocabGoldenV2, sizeof(kKoreanVocabGoldenV2));
		Common::MemoryReadStream s3(kKoreanVocabGoldenV3, sizeof(kKoreanVocabGoldenV3));
		TS_ASSERT(v2.loadFromStream(s2, "SCIKOR", "golden v2"));
		TS_ASSERT(v3.loadFromStream(s3, "SCIKOR", "golden v3"));
		TS_ASSERT_EQUALS(v2.size(), 7u);
		TS_ASSERT_EQUALS(v3.size(), 7u);
		TS_ASSERT_EQUALS(v3.getVersion(), 3u);
		TS_ASSERT_EQUALS(v3.getHeader().toString(), Common::String(header()));
		TS_ASSERT_EQUALS(v3.getHeader().game, "kq1");
		TS_ASSERT_EQUALS(v3.getHeader().engine, "sci");
		const struct {
			const char *word;
			uint n;
			uint16 g0, c0, g1, c1;
		} want[] = {
			{ "열쇠", 1, 12, 0x0800, 0, 0 },
			{ "가방", 1, 12, 0x0800, 0, 0 },
			{ "집다", 1, 20, 0x0080, 0, 0 },
			{ "들여다봐", 2, 30, 0x8080, 169, 0x0010 },
			{ "문", 2, 83, 0x0800, 166, 0x0800 },
			{ "문들", 0, 0, 0, 0, 0 },
		};
		for (uint i = 0; i < ARRAYSIZE(want); ++i) {
			const Common::KoreanVocabulary *tables[2] = { &v2, &v3 };
			for (int t = 0; t < 2; ++t) {
				uint16 g[4] = { 0, 0, 0, 0 }, c[4] = { 0, 0, 0, 0 };
				TSM_ASSERT_EQUALS(want[i].word, tables[t]->lookupExact(u(want[i].word), g, c, 4), want[i].n);
				TSM_ASSERT_EQUALS(want[i].word, g[0], want[i].g0);
				TSM_ASSERT_EQUALS(want[i].word, c[0], want[i].c0);
				TSM_ASSERT_EQUALS(want[i].word, g[1], want[i].g1);
				TSM_ASSERT_EQUALS(want[i].word, c[1], want[i].c1);
			}
		}
	}

	// u32 offsets: a blob past 65535 code units, which version 2 cannot address.
	void test_version3_offsets_past_64k_units() {
		Common::Array<Common::String> words;
		Common::Array<KoreanVocabFixture::Row> rows;
		// 4,500 words of 16 syllables: 72,000 code units.
		for (uint i = 0; i < 4500; ++i) {
			Common::U32String w;
			for (uint k = 0; k < 16; ++k)
				w += (Common::u32char_type_t)(0xAC00 + (k == 15 ? i % 11172 : (i / 11172 + k) % 11172));
			words.push_back(Common::convertFromU32String(w, Common::kUtf8));
		}
		for (uint i = 0; i < words.size(); ++i) {
			KoreanVocabFixture::Row r = { words[i].c_str(), (uint16)(i + 1), 0 };
			rows.push_back(r);
		}
		const char *head = "#! format=1 game=big fingerprint=0123456789abcdef0123456789abcdef engine=sci";
		Common::KoreanVocabulary v;
		TS_ASSERT(loadImage(v, KoreanVocabFixture::build("SCIKOR", rows.data(), rows.size(), 3, head), "SCIKOR"));
		TS_ASSERT_EQUALS(v.size(), 4500u);
		uint16 g[2], c[2];
		TS_ASSERT_EQUALS(v.lookupExact(words[4499].decode(Common::kUtf8), g, c, 2), 1u);
		TS_ASSERT_EQUALS(g[0], 4500);
		TS_ASSERT_EQUALS(v.lookupExact(words[0].decode(Common::kUtf8), g, c, 2), 1u);
		TS_ASSERT_EQUALS(g[0], 1);
	}

	// The fingerprint header decides: a table made for another game is refused; a version 2 table (no header) loads.
	void test_fingerprint_is_checked() {
		Common::TranslationFileHeader same, other, none;
		same.game = "kq1";
		same.fingerprint = "0123456789ABCDEF0123456789ABCDEF";
		same.vocab = "fedcba9876543210fedcba9876543210";
		other = same;
		other.fingerprint = "ffffffffffffffffffffffffffffffff";
		Common::KoreanVocabulary v;
		TS_ASSERT(loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", same));
		TS_ASSERT(loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", none));
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", other));
		TS_ASSERT(!v.isLoaded());
		other = same;
		other.vocab = "00000000000000000000000000000000";
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 3, header()), "SCIKOR", other));
		TS_ASSERT(loadImage(v, rowsImage("SCIKOR", 2), "SCIKOR", same));
		TS_ASSERT_EQUALS(v.getVersion(), 2u);
	}

	void test_bad_version3_files_are_refused() {
		Common::KoreanVocabulary v;
		// engine= must match the magic
		TS_ASSERT(!loadImage(v, rowsImage("AGIKOR", 3, header()), "AGIKOR"));
		// no header line
		TS_ASSERT(!loadImage(v, rowsImage("SCIKOR", 3, "no header"), "SCIKOR"));
		// header size not a multiple of 4
		Common::Array<byte> img = rowsImage("SCIKOR", 3, header());
		img[16] = 3;
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
		// an unknown version
		img = rowsImage("SCIKOR", 2);
		img[8] = 4;
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
		// entries out of order: the last two (열쇠, 집다) swapped
		img = rowsImage("SCIKOR", 2);
		for (uint i = 0; i < 8; ++i) {
			const byte t = img[16 + 8 * 2 + i];
			img[16 + 8 * 2 + i] = img[16 + 8 * 3 + i];
			img[16 + 8 * 3 + i] = t;
		}
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
		// an entry past the blob
		img = rowsImage("SCIKOR", 2);
		img[16 + 2] = 200;
		TS_ASSERT(!loadImage(v, img, "SCIKOR"));
	}

	// Developer data: when the working directory has korean-vocab/ with <game>.v2.dat, <game>.v3.dat and <game>.tsv
	// (tools/korean/k6bake.py build --format 2|3 and dump of the harness masters), every word of the dump resolves
	// exactly to its rows, in both versions. Passes without the directory (as hires-maps/ in P1).
	void test_harness_tables_when_given() {
		Common::FSNode dir(Common::Path("korean-vocab"));
		Common::FSList files;
		if (!dir.isDirectory() || !dir.getChildren(files, Common::FSNode::kListFilesOnly))
			return;
		uint games = 0;
		for (uint i = 0; i < files.size(); ++i) {
			const Common::String name = files[i].getName();
			if (!name.hasSuffix(".tsv"))
				continue;
			const Common::String game(name.c_str(), name.size() - 4);
			Common::ScopedPtr<Common::SeekableReadStream> dump(files[i].createReadStream());
			TS_ASSERT(dump);
			if (!dump)
				continue;
			Common::String magic = "SCIKOR";
			Common::Array<Common::String> lines;
			while (!dump->eos()) {
				const Common::String line = dump->readLine();
				if (line.hasPrefix("@magic "))
					magic = Common::String(line.c_str() + 7, 6);
				else if (!line.empty() && line[0] != '#' && line[0] != '@')
					lines.push_back(line);
			}
			for (int version = 2; version <= 3; ++version) {
				Common::KoreanVocabulary v;
				Common::ScopedPtr<Common::SeekableReadStream> in(
					dir.getChild(game + Common::String::format(".v%d.dat", version)).createReadStream());
				TSM_ASSERT(game.c_str(), in && v.loadFromStream(*in, magic.c_str(), game));
				if (!v.isLoaded())
					continue;
				TS_ASSERT_EQUALS(v.size(), lines.size());
				uint k = 0;
				while (k < lines.size()) {
					const Common::String word(lines[k].c_str(), lines[k].findFirstOf('\t'));
					uint16 g[16], c[16];
					const uint n = v.lookupExact(word.decode(Common::kUtf8), g, c, 16);
					const bool lookedUp = word.decode(Common::kUtf8).size() <= Common::KoreanStems::kMaxLen;
					uint rows = 0;
					while (k < lines.size() && lines[k].hasPrefix(word + "\t")) {
						if (lookedUp && rows < n) {
							const Common::String want = Common::String::format("%s\t%u\t0x%04x", word.c_str(), g[rows], c[rows]);
							TSM_ASSERT_EQUALS(game.c_str(), lines[k], want);
						}
						++rows;
						++k;
					}
					if (lookedUp)
						TSM_ASSERT_EQUALS((game + " " + word).c_str(), n, rows);
				}
			}
			++games;
		}
		TS_ASSERT_LESS_THAN(0u, games);
	}
};
