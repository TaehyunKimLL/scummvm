#include <cxxtest/TestSuite.h>

#include <stdio.h>
#include <stdlib.h>

#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/map_pick.h"

#include "../system/null_osystem.h"

/**
 * hires_text_font and the three maps of a Korean game pack: which map
 * pickHiResMap() takes, and how it moves on to the next kind when a map or
 * one of its faces is not there.
 */
class HiResMapPickTestSuite : public CxxTest::TestSuite {
	struct FakeIni {
		Common::HashMap<Common::String, Common::String> game;
	};

	static bool get(const char *key, bool, Common::String &value, void *ctx) {
		return ((FakeIni *)ctx)->game.tryGetVal(key, value);
	}

	static Graphics::HiResIniOverrides read(FakeIni &fake, Common::Array<Common::String> &warnings) {
		return Graphics::readHiResIni(&get, &fake, warnings);
	}

	// The faces that "exist": by base name, so the tests need no font files.
	static Common::Array<Common::String> &presentFaces() {
		static Common::Array<Common::String> faces;
		return faces;
	}

	static bool faceExists(const Common::Path &face) {
		const Common::String name = face.baseName();
		for (uint i = 0; i < presentFaces().size(); ++i) {
			if (presentFaces()[i].equalsIgnoreCase(name))
				return true;
		}
		return false;
	}

	// The binary's own faces (fonts-cjk.dat's): .OTF here.
	static bool faceBuiltin(const Common::Path &face) {
		return face.baseName().hasSuffixIgnoreCase(".OTF");
	}

	static void writeMap(const Common::Path &dir, const char *file, const char *face) {
		Common::DumpFile f;
		TS_ASSERT(f.open(dir.appendComponent(file)));
		f.writeString(Common::String::format("[map]\nversion=2\n[fonts]\nui=%s\n[font]\nface=ui\n", face));
		f.close();
	}

	static Common::String pick(const Graphics::HiResIniOverrides &ini, const Common::Path &dir,
							   Graphics::HiResFontKind &kind, Common::Array<Common::String> &notes) {
		Common::Path path;
		kind = Graphics::kHiResFontAuto;
		if (!Graphics::pickHiResMap(ini, dir, Common::Array<Common::String>(), Graphics::kHiResKeysSci,
									path, kind, notes, &faceExists, &faceBuiltin))
			return "";
		return path.baseName();
	}

public:
	void test_ini_keys() {
		FakeIni fake;
		Common::Array<Common::String> warnings;
		Graphics::HiResIniOverrides ini = read(fake, warnings);
		TS_ASSERT(!ini.fontSet);
		TS_ASSERT_EQUALS(ini.font, Graphics::kHiResFontAuto);
		TS_ASSERT(!ini.kindMapSet[0] && !ini.kindMapSet[1] && !ini.kindMapSet[2]);

		fake.game["hires_text_font"] = "Bitmap";
		fake.game["hires_text_map_svf"] = "OTHERVF.MAP";
		ini = read(fake, warnings);
		TS_ASSERT(warnings.empty());
		TS_ASSERT(ini.fontSet);
		TS_ASSERT_EQUALS(ini.font, Graphics::kHiResFontBitmap);
		TS_ASSERT(ini.kindMapSet[Graphics::kHiResFontSvf - 1]);
		TS_ASSERT_EQUALS(ini.kindMap[Graphics::kHiResFontSvf - 1], "OTHERVF.MAP");

		fake.game["hires_text_font"] = "pixel";
		fake.game["hires_text_map_ttf"] = "";
		ini = read(fake, warnings);
		TS_ASSERT(!ini.fontSet);
		TS_ASSERT(!ini.kindMapSet[Graphics::kHiResFontTtf - 1]);
		TS_ASSERT_EQUALS(warnings.size(), 2U);
	}

	void test_kind_names_parse_back() {
		const Graphics::HiResFontKind kinds[] = { Graphics::kHiResFontAuto, Graphics::kHiResFontTtf,
												   Graphics::kHiResFontSvf, Graphics::kHiResFontBitmap };
		for (uint i = 0; i < ARRAYSIZE(kinds); ++i) {
			Graphics::HiResFontKind k = Graphics::kHiResFontBitmap;
			TS_ASSERT(Graphics::parseHiResFontKind(Graphics::hiResFontKindName(kinds[i]), k));
			TS_ASSERT_EQUALS(k, kinds[i]);
		}
		Graphics::HiResFontKind k = Graphics::kHiResFontSvf;
		TS_ASSERT(Graphics::parseHiResFontKind("TTF", k));
		TS_ASSERT_EQUALS(k, Graphics::kHiResFontTtf);
		TS_ASSERT(!Graphics::parseHiResFontKind("pixel", k));
		TS_ASSERT_EQUALS(k, Graphics::kHiResFontTtf);	// untouched
	}

	void test_order_puts_the_chosen_kind_first() {
		const bool ttf = Graphics::hiResFontKindBuilt(Graphics::kHiResFontTtf);
		Common::Array<Graphics::HiResFontKind> order = Graphics::hiResFontOrder(Graphics::kHiResFontAuto);
		TS_ASSERT_EQUALS(order.size(), ttf ? 3U : 2U);
		TS_ASSERT_EQUALS(order[0], ttf ? Graphics::kHiResFontTtf : Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(order.back(), Graphics::kHiResFontBitmap);

		order = Graphics::hiResFontOrder(Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(order[0], Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(order.back(), Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(order.size(), ttf ? 3U : 2U);

		TS_ASSERT_EQUALS(Graphics::hiResFontKindDefaultMap(Graphics::kHiResFontTtf), Common::String("HIRESTTF.MAP"));
		TS_ASSERT_EQUALS(Graphics::hiResFontKindDefaultMap(Graphics::kHiResFontSvf), Common::String("HIRESVF.MAP"));
		TS_ASSERT_EQUALS(Graphics::hiResFontKindDefaultMap(Graphics::kHiResFontBitmap), Common::String("HIRESBIT.MAP"));
	}

	void test_a_chosen_kind_wins_over_hires_text_map() {
		FakeIni fake;
		Common::Array<Common::String> warnings;
		TS_ASSERT(Graphics::hiResFontPicksMap(read(fake, warnings)));     // nothing set: auto
		fake.game["hires_text_map"] = "HIRESVF.MAP";
		TS_ASSERT(!Graphics::hiResFontPicksMap(read(fake, warnings)));    // the map named
		fake.game["hires_text_font"] = "auto";
		TS_ASSERT(!Graphics::hiResFontPicksMap(read(fake, warnings)));
		fake.game["hires_text_font"] = "bitmap";
		TS_ASSERT(Graphics::hiResFontPicksMap(read(fake, warnings)));     // a choice wins
	}

	void test_pick_moves_on_when_a_map_or_a_face_is_missing() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		char name[] = "hires-map-pick-XXXXXX";
		TS_ASSERT(mkdtemp(name) != nullptr);
		char *full = realpath(name, nullptr);
		const Common::String dirName = full ? full : name;
		free(full);
		const Common::Path dir(dirName, '/');

		writeMap(dir, "HIRESTTF.MAP", "FACE.TTF");
		writeMap(dir, "hiresvf.map", "AA.SVF");       // any case
		writeMap(dir, "HIRESBIT.MAP", "MONO.SVF");
		writeMap(dir, "OTHERVF.MAP", "OTHER.SVF");

		const bool ttf = Graphics::hiResFontKindBuilt(Graphics::kHiResFontTtf);
		FakeIni fake;
		Common::Array<Common::String> warnings, notes;
		Graphics::HiResFontKind kind;

		// Every face there: this build's first kind.
		presentFaces().clear();
		presentFaces().push_back("FACE.TTF");
		presentFaces().push_back("AA.SVF");
		presentFaces().push_back("MONO.SVF");
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), ttf ? "HIRESTTF.MAP" : "hiresvf.map");
		TS_ASSERT(notes.empty());

		// The TrueType face missing: the SVF map.
		presentFaces().remove_at(0);
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "hiresvf.map");
		TS_ASSERT_EQUALS(kind, Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(notes.size(), ttf ? 1U : 0U);

		// No anti-aliased face either (no font pack): the bitmap map.
		presentFaces().remove_at(0);
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "HIRESBIT.MAP");
		TS_ASSERT_EQUALS(kind, Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(notes.back(), "svf map HIRESVF.MAP: face " + dir.appendComponent("AA.SVF").toString('/') + " is missing");

		// Nothing left: false, and the caller falls back.
		presentFaces().clear();
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "");
		TS_ASSERT_EQUALS(notes.size(), ttf ? 3U : 2U);

		// A chosen kind comes first even when an earlier kind would do.
		presentFaces().push_back("FACE.TTF");
		presentFaces().push_back("AA.SVF");
		presentFaces().push_back("MONO.SVF");
		fake.game["hires_text_font"] = "bitmap";
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "HIRESBIT.MAP");

		// hires_text_map_svf renames the SVF map; its face is missing, so on.
		fake.game["hires_text_font"] = "svf";
		fake.game["hires_text_map_svf"] = "OTHERVF.MAP";
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), ttf ? "HIRESTTF.MAP" : "HIRESBIT.MAP");
		TS_ASSERT_EQUALS(notes.size(), 1U);
		presentFaces().push_back("OTHER.SVF");
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "OTHERVF.MAP");

		// A renamed map that is not there: on to the next kind.
		fake.game["hires_text_map_svf"] = "NONE.MAP";
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), ttf ? "HIRESTTF.MAP" : "HIRESBIT.MAP");
		TS_ASSERT_EQUALS(notes[0], "svf map NONE.MAP: not in the game folder");

		// A face chain needs one face of it: the font pack's first, then the
		// binary's own (fonts-cjk.dat).
		{
			Common::DumpFile f;
			TS_ASSERT(f.open(dir.appendComponent("HIRESTTF.MAP")));
			f.writeString("[map]\nversion=2\n[fonts]\npack=PACK.TTF\ncjk=CJK.OTF\n[font]\nface=pack,cjk\n");
			f.close();
		}
		fake.game.erase("hires_text_font");
		fake.game.erase("hires_text_map_svf");
		presentFaces().clear();
		presentFaces().push_back("CJK.OTF");
		notes.clear();
		// Only the built-in face: every map fails pass 1, pass 2 takes the TTF map.
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), ttf ? "HIRESTTF.MAP" : "");
		if (ttf)
			TS_ASSERT_EQUALS(notes.back(), "no map has its own fonts; ttf map HIRESTTF.MAP draws with the built-in ones");
		// The built-in face does not keep the SVF and bitmap maps from their turn.
		presentFaces().push_back("AA.SVF");
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "hiresvf.map");
		presentFaces().remove_at(1);
		presentFaces().push_back("MONO.SVF");
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "HIRESBIT.MAP");
		// The font pack's own face wins in pass 1.
		presentFaces().push_back("PACK.TTF");
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), ttf ? "HIRESTTF.MAP" : "HIRESBIT.MAP");
		presentFaces().clear();
		notes.clear();
		TS_ASSERT_EQUALS(pick(read(fake, warnings), dir, kind, notes), "");
		if (ttf)
			TS_ASSERT_EQUALS(notes[0], "ttf map HIRESTTF.MAP: face " + dir.appendComponent("PACK.TTF").toString('/') +
									   ", " + dir.appendComponent("CJK.OTF").toString('/') + " is missing");

		const char *files[] = { "HIRESTTF.MAP", "hiresvf.map", "HIRESBIT.MAP", "OTHERVF.MAP" };
		for (uint i = 0; i < ARRAYSIZE(files); ++i)
			remove(dir.appendComponent(files[i]).toString('/').c_str());
		remove(dirName.c_str());
		Common::uninstall_null_g_system();
#endif
	}
};
