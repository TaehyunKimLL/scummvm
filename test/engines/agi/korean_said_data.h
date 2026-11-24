#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/translation_file.h"

#include "agi/agi.h"
#include "agi/korparse.h"
#include "agi/words.h"

#include "test/system/null_osystem.h"

// Developer data (DESIGN.md section 10, the i18n gate with real data): with
// korean-packs/LSL1AGI/ (the pack's game folder, or a link to it) and
// korean-packs/LSL1AGI-said/{pairs.tsv,saids.tsv} (harness
// runs/lsl1a/parse/lsl1a-pairs.tsv and runs/lsl1a/src/saids.tsv) in the
// working directory, every walkthrough pair is judged as the harness's
// k16said_agi.py does through the debug socket: the Korean variant passes
// when it matches every said() spec its English command matches, and the
// first spec it matches is the English's first. Passes without the data.
class AgiKoreanSaidDataTestSuite : public CxxTest::TestSuite {
	static Common::FSNode child(const Common::FSNode &dir, const char *name) {
		Common::FSList list;
		dir.getChildren(list, Common::FSNode::kListAll);
		for (uint i = 0; i < list.size(); ++i)
			if (list[i].getName().equalsIgnoreCase(name))
				return list[i];
		return dir.getChild(name);
	}

	static Common::Array<int> matches(Agi::Words &w, const Common::String &text, const Common::Array<Common::Array<uint16> > &specs,
	                                  bool &unknown) {
		Agi::Words::ParseResult r;
		w.analyze(text.c_str(), r);
		Common::Array<int> out;
		unknown = r.unknownPos != 0;
		if (unknown)
			return out;
		for (uint i = 0; i < specs.size(); i++)
			for (uint k = 0; k < r.candidates.size(); k++)
				if (Agi::saidMatches(r.candidates[k], specs[i].data(), specs[i].size())) {
					out.push_back(i);
					break;
				}
		return out;
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_lsl1_said_baseline_when_given() {
		const Common::FSNode packs = Common::FSNode(Common::Path("korean-packs"));
		const Common::FSNode game = child(packs, "LSL1AGI");
		const Common::FSNode said = child(packs, "LSL1AGI-said");
		if (!game.isDirectory() || !said.isDirectory()) {
			TS_WARN("korean-packs/LSL1AGI and LSL1AGI-said are not in the working directory: LSL1 said() check skipped");
			return;
		}
		Agi::Words w(nullptr);
		Common::ScopedPtr<Common::SeekableReadStream> tok(child(game, "WORDS.TOK").createReadStream());
		TS_ASSERT(tok);
		if (!tok)
			return;
		TS_ASSERT_EQUALS(w.loadDictionary(*tok), 0);
		Common::ScopedPtr<Common::SeekableReadStream> dat(child(child(game, "ko"), "agi-ko.dat").createReadStream());
		TS_ASSERT(dat);
		if (!dat)
			return;
		Common::TranslationFileHeader h; // what AgiEngine::translationHeader(true) gives for LSL1 1.00 (DOS)
		h.format = 1;
		h.game = "lsl1";
		h.engine = "agi";
		h.fingerprint = "1d9311bbee31342e76c7570a29d7616d";
		h.vocab = "988a1baaf3ee0d9cd678274dc2f219f2";
		TS_ASSERT(w.loadKoreanTableFromStream(*dat, h));

		Common::Array<Common::Array<uint16> > specs;
		Common::ScopedPtr<Common::SeekableReadStream> st(child(said, "saids.tsv").createReadStream());
		while (st && !st->eos()) {
			const Common::String line = st->readLine();
			const char *p = strchr(line.c_str(), '\t');
			p = p ? strchr(p + 1, '\t') : nullptr; // logic, offset, then the words
			if (!p)
				continue;
			Common::Array<uint16> spec;
			for (p++; *p;) {
				char *end = nullptr;
				spec.push_back((uint16)strtoul(p, &end, 10));
				if (end == p)
					break;
				for (p = end; *p == ' '; p++)
					;
			}
			specs.push_back(spec);
		}
		TS_ASSERT_EQUALS(specs.size(), 2274u);

		uint pass = 0, partial = 0, nomatch = 0, unknown = 0, order = 0, english = 0, total = 0;
		Common::ScopedPtr<Common::SeekableReadStream> pt(child(said, "pairs.tsv").createReadStream());
		while (pt && !pt->eos()) {
			const Common::String line = pt->readLine();
			const char *tab = strchr(line.c_str(), '\t');
			if (!tab)
				continue;
			Common::String en(line.c_str(), tab), ko(tab + 1);
			en.trim();
			ko.trim();
			if (en.empty() || ko.empty())
				continue;
			en.toLowercase();
			total++;
			bool unk;
			const Common::Array<int> hits = matches(w, en, specs, unk);
			if (hits.empty()) {
				english++;
				continue;
			}
			const Common::Array<int> k = matches(w, ko, specs, unk);
			if (unk) {
				unknown++;
				continue;
			}
			uint in = 0;
			for (uint a = 0; a < hits.size(); a++)
				for (uint b = 0; b < k.size(); b++)
					if (k[b] == hits[a]) {
						in++;
						break;
					}
			if (in == hits.size())
				(k[0] == hits[0] ? pass : order)++;
			else if (in)
				partial++;
			else
				nomatch++;
		}
		// k16said_agi.py with the jamo build on the shipped agi-ko.dat
		// (harness runs/lsl1a/parse/said8.txt): 778 variants, pass 691,
		// partial 23, nomatch 20, unknown 7, order 6, english 31.
		TS_ASSERT_EQUALS(total, 778u);
		TS_ASSERT_EQUALS(pass, 691u);
		TS_ASSERT_EQUALS(partial, 23u);
		TS_ASSERT_EQUALS(nomatch, 20u);
		TS_ASSERT_EQUALS(unknown, 7u);
		TS_ASSERT_EQUALS(order, 6u);
		TS_ASSERT_EQUALS(english, 31u);
	}
};
