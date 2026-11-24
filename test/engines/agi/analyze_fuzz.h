#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/translation_file.h"

#include "agi/agi.h"
#include "agi/korparse.h"
#include "agi/words.h"

#include "test/system/null_osystem.h"

// Randomized robustness and invariant test of Words::analyze() with the real
// LSL1 dictionary and Korean table (korean-packs/LSL1AGI, as
// korean_said_data.h; skipped with a warning without it). Inputs are random
// mixes of ASCII words, known and made-up Hangul tokens, stray bytes,
// truncated multibyte sequences and very long lines, in exact-size heap
// blocks (a read past the terminator is a sanitizer report).
class AgiAnalyzeFuzzTestSuite : public CxxTest::TestSuite {
	uint32 _seed;

	uint32 rnd() {
		_seed = _seed * 1664525u + 1013904223u;
		return _seed >> 8;
	}

	static Common::FSNode child(const Common::FSNode &dir, const char *name) {
		Common::FSList list;
		dir.getChildren(list, Common::FSNode::kListAll);
		for (uint i = 0; i < list.size(); ++i)
			if (list[i].getName().equalsIgnoreCase(name))
				return list[i];
		return dir.getChild(name);
	}

	static void appendCp(Common::String &s, uint32 cp) {
		if (cp < 0x80) {
			s += (char)cp;
		} else if (cp < 0x800) {
			s += (char)(0xC0 | (cp >> 6));
			s += (char)(0x80 | (cp & 0x3F));
		} else {
			s += (char)(0xE0 | (cp >> 12));
			s += (char)(0x80 | ((cp >> 6) & 0x3F));
			s += (char)(0x80 | (cp & 0x3F));
		}
	}

	Common::String randomLine(const Common::Array<Common::String> &korean, const Common::Array<Common::String> &english) {
		Common::String s;
		const int tokens = rnd() % 8;
		for (int t = 0; t < tokens; t++) {
			if (t)
				s += (rnd() % 6 == 0) ? "  " : " ";
			switch (rnd() % 7) {
			case 0:
				if (!english.empty())
					s += english[rnd() % english.size()];
				break;
			case 1:
			case 2:
				if (!korean.empty())
					s += korean[rnd() % korean.size()];
				break;
			case 3: {
				const int n = 1 + rnd() % 5;
				for (int i = 0; i < n; i++)
					appendCp(s, 0xAC00 + rnd() % 11172);
				break;
			}
			case 4: { // English word with a Korean tail
				if (!english.empty())
					s += english[rnd() % english.size()];
				appendCp(s, 0xAC00 + rnd() % 11172);
				break;
			}
			case 5: { // stray bytes, possibly a lone lead byte
				const int n = 1 + rnd() % 4;
				for (int i = 0; i < n; i++)
					s += (char)(0x80 + rnd() % 0x80);
				break;
			}
			default: {
				const int n = rnd() % 40;
				for (int i = 0; i < n; i++)
					s += (char)(0x20 + rnd() % 0x5F);
				break;
			}
			}
		}
		if (rnd() % 10 == 0) { // cut inside a multibyte sequence, or a very long line
			if (s.size() > 1)
				s = Common::String(s.c_str(), 1 + rnd() % (s.size() - 1));
		}
		if (rnd() % 50 == 0) {
			const Common::String base(s);
			while (s.size() < 700 && !base.empty())
				s += " " + base;
		}
		return s;
	}

public:
	void setUp() override {
		_seed = 0xc0ffee11;
		Common::install_null_g_system();
	}
	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_analyzeKeepsItsInvariantsOnRandomLines() {
		const Common::FSNode packs = Common::FSNode(Common::Path("korean-packs"));
		const Common::FSNode game = child(packs, "LSL1AGI");
		const Common::FSNode said = child(packs, "LSL1AGI-said");
		if (!game.isDirectory() || !said.isDirectory()) {
			TS_WARN("korean-packs/LSL1AGI is not in the working directory: analyze fuzz skipped");
			return;
		}
		Agi::Words w(nullptr);
		Common::ScopedPtr<Common::SeekableReadStream> tok(child(game, "WORDS.TOK").createReadStream());
		TS_ASSERT(tok);
		Common::ScopedPtr<Common::SeekableReadStream> dat(child(child(game, "ko"), "agi-ko.dat").createReadStream());
		TS_ASSERT(dat);
		if (!tok || !dat)
			return;
		TS_ASSERT_EQUALS(w.loadDictionary(*tok), 0);
		Common::TranslationFileHeader h;
		h.format = 1;
		h.game = "lsl1";
		h.engine = "agi";
		h.fingerprint = "1d9311bbee31342e76c7570a29d7616d";
		h.vocab = "988a1baaf3ee0d9cd678274dc2f219f2";
		TS_ASSERT(w.loadKoreanTableFromStream(*dat, h));

		// token pools from the walkthrough pairs
		Common::Array<Common::String> korean, english;
		Common::ScopedPtr<Common::SeekableReadStream> pt(child(said, "pairs.tsv").createReadStream());
		while (pt && !pt->eos()) {
			const Common::String line = pt->readLine();
			const char *tab = strchr(line.c_str(), '\t');
			if (!tab)
				continue;
			Common::String en(line.c_str(), tab), ko(tab + 1);
			en.trim();
			ko.trim();
			en.toLowercase();
			for (uint i = 0, b = 0; i <= en.size(); i++)
				if (i == en.size() || en[i] == ' ') {
					if (i > b)
						english.push_back(Common::String(en.c_str() + b, i - b));
					b = i + 1;
				}
			for (uint i = 0, b = 0; i <= ko.size(); i++)
				if (i == ko.size() || ko[i] == ' ') {
					if (i > b)
						korean.push_back(Common::String(ko.c_str() + b, i - b));
					b = i + 1;
				}
		}
		TS_ASSERT(!korean.empty() && !english.empty());

		uint withUnknown = 0, withCandidates = 0;
		for (int iter = 0; iter < 30000; iter++) {
			const Common::String line = randomLine(korean, english);
			char *s = (char *)malloc(line.size() + 1);
			memcpy(s, line.c_str(), line.size() + 1);
			Agi::Words::ParseResult a, b;
			w.analyze(s, a);
			w.analyze(s, b); // pure: the same twice
			free(s);

			TS_ASSERT_EQUALS(a.candidates.size(), b.candidates.size());
			TS_ASSERT_EQUALS(a.unknownPos, b.unknownPos);
			TS_ASSERT(!a.candidates.empty());
			TS_ASSERT(a.candidates.size() <= 4u);
			for (uint c = 0; c < a.candidates.size(); c++) {
				TS_ASSERT(a.candidates[c].size() <= (uint)MAX_WORDS);
				if (c < b.candidates.size())
					TS_ASSERT_EQUALS(a.candidates[c].size(), b.candidates[c].size());
			}
			if (a.unknownPos) {
				withUnknown++;
				// the unknown word is the last one kept, so its position is inside the (at most
				// MAX_WORDS) words of every candidate: a script's "I don't know the word N" reads it
				TS_ASSERT(a.unknownPos >= 1u && a.unknownPos <= a.candidates[0].size());
				TS_ASSERT(a.unknownPos <= (uint)MAX_WORDS);
			} else if (a.candidates[0].size()) {
				withCandidates++;
			}
		}
		// the generator must reach both outcomes, else the test says nothing
		TS_ASSERT(withUnknown > 1000u);
		TS_ASSERT(withCandidates > 1000u);
	}
};
