#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/korean_vocab.h"
#include "common/ptr.h"
#include "common/stream.h"

#include "sci/engine/script_strings.h"

#include "test/system/null_osystem.h"

// Developer data (DESIGN.md section 10, "i18n gate with real data"): when the
// working directory has korean-packs/<FOLDER>/ko/ (a game folder of a
// harness pack, or a link to one), its tables load with this game's header:
// sci-ko.str through ScriptStrings, sci-ko.dat through KoreanVocabulary.
// The fingerprints are what the engine prints for the game ("This game's
// sci-ko.str header line is: ..."). Passes without the directory.
class SciKoreanPacksTestSuite : public CxxTest::TestSuite {
	struct Pack {
		const char *folder;      // GAMES/<FOLDER> of the pack
		const char *game;        // detection game id
		const char *fingerprint; // script 0 + main vocabulary
		const char *vocab;       // the vocabulary alone; nullptr: no parser table
		uint strings;            // sci-ko.str entries
		uint words;              // sci-ko.dat entries
	};

	static Common::TranslationFileHeader header(const Pack &p, bool withVocab) {
		Common::TranslationFileHeader h;
		h.format = 1;
		h.game = p.game;
		h.engine = "sci";
		h.fingerprint = p.fingerprint;
		if (withVocab && p.vocab)
			h.vocab = p.vocab;
		return h;
	}

	static void check(const Common::FSNode &ko, const Pack &p) {
		Common::ScopedPtr<Common::SeekableReadStream> str(ko.getChild("sci-ko.str").createReadStream());
		TSM_ASSERT(p.folder, str);
		if (str) {
			Sci::ScriptStrings t;
			TSM_ASSERT(p.folder, t.loadFromStream(*str, "sci-ko.str", header(p, false)));
			TSM_ASSERT(p.folder, t.getHeader().present());
			TSM_ASSERT_EQUALS(p.folder, t.entryCount(), p.strings);
		}
		if (!p.vocab)
			return;
		Common::ScopedPtr<Common::SeekableReadStream> dat(ko.getChild("sci-ko.dat").createReadStream());
		TSM_ASSERT(p.folder, dat);
		if (dat) {
			Common::KoreanVocabulary v;
			TSM_ASSERT(p.folder, v.loadFromStream(*dat, "SCIKOR", "sci-ko.dat", header(p, true)));
			TSM_ASSERT_EQUALS(p.folder, v.getVersion(), 3u);
			TSM_ASSERT_EQUALS(p.folder, v.size(), p.words);
		}
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_packs_when_given() {
		static const Pack packs[] = {
			{ "QFG2", "qfg2", "97c9d236d0f581f496021ff95e091630", "d8cfdb9771b88b9c4e8c7d620675e1b9", 2032, 2674 },
			{ "QFG1EGA", "qfg1", "10f6eb151614d69befc60b70db1869b4", "87d6713b42f26d10b7b1791449d66074", 175, 2247 },
			{ "LSL1EGA", "lsl1sci", "71e4c864f0be154d11f0965a2906370e", nullptr, 703, 0 },
			{ "LSL1VGA", "lsl1sci", "71e4c864f0be154d11f0965a2906370e", nullptr, 703, 0 },
			{ "KQ4", "kq4sci", "388d93cbfef9ba97d8d681e746b2e05a", "cc12dd209ebf67837113a035bb45f1e3", 222, 1168 },
			{ "LB1", "laurabow", "e7528cb4f9ae1c5d8f39c6ac1eccbe43", "dfec1d84860715fffa1bdff1eec2e640", 91, 2017 },
			{ "CAMELOT", "camelot", "b864990d81c9a5a090795686b4895d12", "f0016e432faf94bcc041078483c77282", 118, 5362 },
		};
		const Common::FSNode dir(Common::Path("korean-packs"));
		if (!dir.isDirectory()) {
			TS_WARN("korean-packs/ is not in the working directory: pack table check skipped");
			return;
		}
		uint seen = 0;
		for (uint i = 0; i < ARRAYSIZE(packs); ++i) {
			const Common::FSNode ko = dir.getChild(packs[i].folder).getChild("ko");
			if (!ko.isDirectory())
				continue;
			check(ko, packs[i]);
			++seen;
		}
		TS_ASSERT(seen > 0);
	}
};
