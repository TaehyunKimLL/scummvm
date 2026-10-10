#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/archive.h"
#include "common/array.h"
#include "common/stream.h"
#include "../../system/null_osystem.h"

#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/font_map.h"

/**
 * A Korean game folder must be offered as Korean, and must draw Korean.
 *
 * The DOS packs lay a sci-ko.str table over the original resources, so
 * the detection table's English entry matches and nothing else does: on
 * Windows the launcher offered Laura Bow 1 and Camelot in English only,
 * and the table never loaded. These pin the two things that fix it: the
 * detector adds the manifest's language in front of the match, and a
 * Korean game with no map falls back to the shipped maps.
 */
class SciKoreanDetectTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_a_manifest_language_is_offered_first() {
		Common::String det = readSource("engines/sci/detection.cpp");
		// The manifest's name is the engine's own convention (translation.cpp).
		TS_ASSERT(det.contains("name.hasPrefix(\"sci-\") || !name.hasSuffix(\".str\")"));
		TS_ASSERT(det.contains("manifestLanguages(fslist)"));
		// In front, so the launcher's chooser leads with the translation.
		TS_ASSERT(det.contains("games.insert_at(0, variant)"));
		// Only for a full match, and never a second copy of the same language.
		TS_ASSERT(det.contains("if (game.language == manifests[m] || game.hasUnknownFiles)"));
		// The variant is its own target, so both can be added from one folder.
		TS_ASSERT(det.contains("variant.preferredTarget = game.preferredTarget + \"-\" + Common::getLanguageCode(manifests[m])"));

		Common::String tr = readSource("engines/sci/engine/translation.cpp");
		TS_ASSERT(tr.contains("\"sci-%s.str\""));
	}

	void test_a_korean_game_without_a_map_uses_the_shipped_one() {
		Common::String st = readSource("engines/sci/graphics/hirestextstate.cpp");
		const int gate = st.find("g_sci->getLanguage() == Common::KO_KOR");
		const int own = st.find("\"hires_text/maps/%s-ko.map\"");
		const int general = st.find("\"hires_text/maps/korean-default.map\"");
		TS_ASSERT_LESS_THAN(0, gate);
		TS_ASSERT_LESS_THAN(gate, own);
		TS_ASSERT_LESS_THAN(own, general);
		// Only after the folder's own HIRESTXT.MAP and an explicit setting.
		TS_ASSERT_LESS_THAN(st.find("findDefaultHiResMap(_gameDir)"), gate);
		// A shipped map that is not there is not used, and not complained
		// about either (a DOS build has neither): the quiet resolver, then
		// an existence check.
		TS_ASSERT(st.contains("Graphics::HiResFontMap::resolveDataPath(candidates[i], roots)"));
		TS_ASSERT(st.contains("!Graphics::HiResFontMap::isDataPath(p.toString('/')) && Graphics::fontFileExists(p)"));
		TS_ASSERT(!st.contains("resolvePath(candidates"));

		// The shipped maps exist, and the per-game ones draw Latin with the
		// Korean face too, as the DOS packs' baked maps do.
		TS_ASSERT(exists("dists/engine-data/hires_text/maps/korean-default.map"));
		static const char *const perGame[] = { "kq1", "laurabow", "camelot", nullptr };
		for (uint i = 0; perGame[i]; ++i) {
			const Common::String rel = Common::String::format("dists/engine-data/hires_text/maps/%s-ko.map", perGame[i]);
			TS_ASSERT(exists(rel.c_str()));
			TS_ASSERT(readSource(rel.c_str()).contains("range.basic-latin=same"));
		}
	}

	// On Android the shipped maps and fonts are APK assets, an archive in
	// the search set rather than a folder, and no data root holds them:
	// the resolver falls back to the archive and the opener reads from it.
	void test_shipped_maps_and_fonts_resolve_from_the_search_set() {
		Common::FSNode data(Common::Path(Common::String(SCI_TEST_SRCDIR) + "/dists/engine-data", '/'));
		TS_ASSERT(data.isDirectory());
		SearchMan.addDirectory("korean_detect_data", data, 0, 4);

		const Common::Array<Common::Path> noRoots;
		const Common::Path map = Graphics::HiResFontMap::resolveDataPath("hires_text/maps/kq1-ko.map", noRoots);
		TS_ASSERT(!Graphics::HiResFontMap::isDataPath(map.toString('/')));
		TS_ASSERT(Graphics::fontFileExists(map));
		Common::SeekableReadStream *stream = Graphics::openDataFile(map);
		TS_ASSERT(stream != nullptr);
		delete stream;

		int32 face = -1;
		Common::String error;
		stream = Graphics::openFontFace(Common::Path("hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf"), face, error);
		TS_ASSERT(stream != nullptr);
		TS_ASSERT_EQUALS(face, 0);
		delete stream;

		SearchMan.remove("korean_detect_data");
		// Gone from the search set, the same name is nowhere: back to data:.
		const Common::Path gone = Graphics::HiResFontMap::resolveDataPath("hires_text/maps/kq1-ko.map", noRoots);
		TS_ASSERT(Graphics::HiResFontMap::isDataPath(gone.toString('/')));
	}

private:
	bool exists(const char *rel) {
		Common::String path = Common::String(SCI_TEST_SRCDIR) + "/" + rel;
		return Common::FSNode(Common::Path(path, '/')).exists();
	}

	Common::String readSource(const char *rel) {
		Common::String path = Common::String(SCI_TEST_SRCDIR) + "/" + rel;
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path).c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		return out;
	}
};
