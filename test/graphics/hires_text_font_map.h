#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

#include "../system/null_osystem.h"

/**
 * Tests for HiResFontMap's path and code page helpers.
 */
class HiResFontMapTestSuite : public CxxTest::TestSuite {
public:
	void test_codepage_names() {
		typedef Graphics::HiResFontMap M;

		TS_ASSERT_EQUALS(M::parseCodePage("cp949"), Common::kWindows949);
		TS_ASSERT_EQUALS(M::parseCodePage("uhc"), Common::kWindows949);
		TS_ASSERT_EQUALS(M::parseCodePage("sjis"), Common::kWindows932);
		TS_ASSERT_EQUALS(M::parseCodePage("cp932"), Common::kWindows932);
		TS_ASSERT_EQUALS(M::parseCodePage("gbk"), Common::kWindows936);
		TS_ASSERT_EQUALS(M::parseCodePage("big5"), Common::kWindows950);
		TS_ASSERT_EQUALS(M::parseCodePage("johab"), Common::kJohab);

		// Unicode input is the reason this layer works on code points.
		TS_ASSERT_EQUALS(M::parseCodePage("utf8"), Common::kUtf8);
		TS_ASSERT_EQUALS(M::parseCodePage("utf-8"), Common::kUtf8);

		// Names are matched without regard to case.
		TS_ASSERT_EQUALS(M::parseCodePage("UTF-8"), Common::kUtf8);
		TS_ASSERT_EQUALS(M::parseCodePage("SJIS"), Common::kWindows932);

		TS_ASSERT_EQUALS(M::parseCodePage("klingon"), Common::kCodePageInvalid);
	}

	void test_non_dbcs_encoding_selection() {
		typedef Graphics::HiResFontMap M;
		TS_ASSERT_EQUALS(M::parseCodePage("CP1252"), Common::kWindows1252);
		TS_ASSERT_EQUALS(M::parseCodePage("cp1251"), Common::kWindows1251);
		TS_ASSERT_EQUALS(M::parseCodePage("latin1"), Common::kISO8859_1);
		TS_ASSERT_EQUALS(M::parseCodePage("macroman"), Common::kMacRoman);
		TS_ASSERT_EQUALS(M::parseCodePage("cp850"), Common::kDos850);
		TS_ASSERT_EQUALS(M::parseCodePage("ascii"), Common::kASCII);
	}

	void test_relative_paths_resolve_against_the_map() {
		typedef Graphics::HiResFontMap M;
		const Common::Path base("/games/demo");

		// A translation ships its fonts next to the map and stays movable.
		TS_ASSERT_EQUALS(M::resolvePath("fonts/nanum.ttf", base).toString('/'),
						 "/games/demo/fonts/nanum.ttf");

		// An absolute path is used as it stands.
		TS_ASSERT_EQUALS(M::resolvePath("/usr/share/fonts/x.ttf", base).toString('/'),
						 "/usr/share/fonts/x.ttf");

		// So is a Windows drive letter.
		TS_ASSERT_EQUALS(M::resolvePath("C:/fonts/x.ttf", base).toString('/'),
						 "C:/fonts/x.ttf");

		TS_ASSERT(M::resolvePath("", base).empty());
	}

	// Fake file system for the data: tests: only these two files exist.
	static bool dataFileExists(const Common::Path &path) {
		const Common::String s = path.toString('/');
		return s == "/opt/data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf" ||
			   s == "/home/me/extra/hires_text/fonts/neodgm/neodgm.ttf";
	}

	void test_data_prefix_names_a_file_in_the_data_directories() {
		typedef Graphics::HiResFontMap M;
		Common::Array<Common::Path> roots;
		roots.push_back(Common::Path("/home/me/extra"));
		roots.push_back(Common::Path("/opt/data"));

		// The prefix is recognised only at the start, case-sensitively.
		TS_ASSERT(M::isDataPath("data:hires_text/fonts/x.ttf"));
		TS_ASSERT(!M::isDataPath("fonts/data:x.ttf"));
		TS_ASSERT(!M::isDataPath("data"));
		TS_ASSERT(!M::isDataPath("DATA:x.ttf"));

		// The first root holding the file wins, in the order given.
		TS_ASSERT_EQUALS(M::resolveDataPath("hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf",
											roots, dataFileExists).toString('/'),
						 "/opt/data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf");
		TS_ASSERT_EQUALS(M::resolveDataPath("hires_text/fonts/neodgm/neodgm.ttf",
											roots, dataFileExists).toString('/'),
						 "/home/me/extra/hires_text/fonts/neodgm/neodgm.ttf");

		// A .ttc face suffix is looked up by its file, and kept.
		TS_ASSERT_EQUALS(M::resolveDataPath("hires_text/fonts/neodgm/neodgm.ttf#0",
											roots, dataFileExists).toString('/'),
						 "/home/me/extra/hires_text/fonts/neodgm/neodgm.ttf#0");

		// A missing file keeps its data: name, so the open fails with that
		// name in the warning rather than against some unrelated folder.
		TS_ASSERT_EQUALS(M::resolveDataPath("hires_text/fonts/none.ttf", roots, dataFileExists)
							 .toString('/'),
						 "data:hires_text/fonts/none.ttf");
		TS_ASSERT_EQUALS(M::resolveDataPath("hires_text/fonts/none.ttf",
											Common::Array<Common::Path>(), dataFileExists)
							 .toString('/'),
						 "data:hires_text/fonts/none.ttf");

		// A data: path stays inside the data folders: no absolute path,
		// no drive letter, no ".." component; such a value is not looked up.
		TS_ASSERT(M::isSafeDataRelative("hires_text/fonts/x.ttf"));
		TS_ASSERT(M::isSafeDataRelative("hires_text/..fonts/x..ttf"));
		TS_ASSERT(!M::isSafeDataRelative(""));
		TS_ASSERT(!M::isSafeDataRelative("/etc/x.ttf"));
		TS_ASSERT(!M::isSafeDataRelative("\\x.ttf"));
		TS_ASSERT(!M::isSafeDataRelative("C:/x.ttf"));
		TS_ASSERT(!M::isSafeDataRelative("../x.ttf"));
		TS_ASSERT(!M::isSafeDataRelative("hires_text/../../x.ttf"));
		TS_ASSERT(!M::isSafeDataRelative("hires_text\\..\\x.ttf"));
		TS_ASSERT(!M::isSafeDataRelative("hires_text/.."));
		TS_ASSERT_EQUALS(M::resolveDataPath("../opt/data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf",
											roots, dataFileExists).toString('/'),
						 "data:../opt/data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf");

		// (resolvePath() with dataRoots() on the live file system is in
		// test_data_path_falls_back_to_the_search_manager_folders().)
	}

	// A Windows build ships dists/engine-data next to scummvm.exe and sets no
	// extrapath or DATA_PATH; ScummVM finds its own .dat files there through
	// SearchMan's "." folder, which searches one level deep. data: must find
	// hires_text/maps/... and hires_text/fonts/<face>/... under that folder
	// all the same. The in-tree dists/engine-data plays the exe folder here.

	void test_data_path_falls_back_to_the_search_manager_folders() {
#if NULL_OSYSTEM_IS_AVAILABLE
		typedef Graphics::HiResFontMap M;
		Common::install_null_g_system();
		// An in-tree build runs the runner from the source root.
		const Common::FSNode dir("dists/engine-data");
		if (!dir.getChild("hires_text").getChild("maps").getChild("kq1-ko.map").exists()) {
			Common::uninstall_null_g_system();
			TS_SKIP("dists/engine-data/hires_text is not reachable from the runner's folder");
			return;
		}
		const Common::String relMap = "hires_text/maps/kq1-ko.map";
		const Common::String relFont = "hires_text/fonts/gowunbatang/GowunBatang-Bold.ttf";

		// A set that searches one level deep, as SearchMan's "." does; the
		// files are too deep for the set itself to find.
		Common::SearchSet set;
		set.addDirectory("exe-dir", dir, 0, 1);
		// An archive that is not a folder (here a nested set) has files
		// but no root to look under: skipped.
		Common::SearchSet *nested = new Common::SearchSet();
		nested->addDirectory("inner", dir, 0, 1);
		set.add("nested", nested, 5);
		TS_ASSERT(!set.hasFile(Common::Path(relMap)));
		const Common::Array<Common::Path> roots = M::searchSetRoots(set);
		TS_ASSERT_EQUALS(roots.size(), 1u);
		if (roots.size() == 1)
			TS_ASSERT(roots[0] == dir.getPath());
		const Common::Path map = M::resolveDataPath(relMap, roots);
		TS_ASSERT(Common::FSNode(map).exists());
		TS_ASSERT(map.toString('/').hasSuffix(relMap));
		const Common::Path font = M::resolveDataPath(relFont, roots);
		TS_ASSERT(Common::FSNode(font).exists());
		int32 face = -2;
		Common::String error;
		Common::SeekableReadStream *stream = Graphics::openFontFace(font, face, error);
		TS_ASSERT(stream != nullptr);
		TS_ASSERT_EQUALS(face, 0);
		delete stream;
		// The safety check still holds: ".." never reaches the folders.
		TS_ASSERT(M::resolveDataPath("hires_text/../hires_text/maps/kq1-ko.map", roots)
					  .toString('/').hasPrefix("data:"));
		TS_ASSERT(M::resolveDataPath("hires_text/maps/none.map", roots)
					  .toString('/').hasPrefix("data:"));

		// The same through the real resolver: SearchMan is part of dataRoots().
		const bool wasThere = SearchMan.hasArchive("hires_text_font_map");
		if (!wasThere)
			SearchMan.addDirectory("hires_text_font_map", dir, -10, 1);
		const Common::Path viaMan = M::resolvePath("data:" + relMap, Common::Path());
		TS_ASSERT(Common::FSNode(viaMan).exists());
		TS_ASSERT(viaMan.toString('/').hasSuffix(relMap));
		if (!wasThere)
			SearchMan.remove("hires_text_font_map");
		Common::uninstall_null_g_system();
#endif
	}
};

/**
 * Tests for the version-2 map loader (design
 * docs/superpowers/specs/2026-09-30-hires-config-unify-design.md).
 *
 * The map file is the only user facing part of the hi-res text layer, so the
 * cases below double as a description of what a translation is allowed to
 * write in one.
 */
class HiResMapTestSuite : public CxxTest::TestSuite {
	bool load(const char *text, Graphics::HiResMap &out, const Graphics::HiResEngineKeys &keys = Graphics::kHiResKeysScumm,
			  const char *q0 = nullptr) {
		Common::Array<Common::String> qualifiers;
		if (q0)
			qualifiers.push_back(q0);
		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		return Graphics::HiResFontMap::loadMap(stream, Common::Path("/maps", '/'), qualifiers, keys, out);
	}

	static bool hasWarning(const Graphics::HiResMap &m, const char *text) {
		for (uint i = 0; i < m.warnings.size(); ++i)
			if (m.warnings[i] == text)
				return true;
		return false;
	}

public:
	void test_version_is_required() {
		Graphics::HiResMap m;
		TS_ASSERT(!load("[render]\nscale=2\n", m));
		TS_ASSERT(!load("[map]\nversion=1\n", m));
		TS_ASSERT(!load("[hires]\nscale=2\n[latin]\nmode=off\n", m));
		TS_ASSERT_EQUALS(m.warnings.back(),
			"HIRESTXT.MAP /maps: not a version 2 map; regenerate it (graphics/hires_text/README.md) (found [hires])");
		TS_ASSERT(load("[map]\nversion=2\n", m));
	}

	void test_render_section() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[render]\ntarget=rgb565\nblend=off\nscale=3\ngamma=2.2\n", m));
		TS_ASSERT(m.targetSet);
		TS_ASSERT_EQUALS(m.target, Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(m.scale, 3);
		TS_ASSERT_EQUALS(m.coverageGamma, 220);
	}

	void test_qualified_render_wins() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[render]\ntarget=clut8\n[render:monkey2]\ntarget=rgb888\n", m,
					   Graphics::kHiResKeysScumm, "monkey2"));
		TS_ASSERT_EQUALS(m.target, Graphics::kHiResTargetRgb888);
	}

	void test_font_scopes_and_ranges() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[fonts]\nko=KO.SVF\nlat=LAT.SVF\n"
					   "[font]\nface=ko\nmissing=u+25a1\nrange.basic-latin=lat, same\nadvance.basic-latin=font\n"
					   "origin.U+2026=face\n"
					   "[font.4]\nface=CARD.SVF\nrange.U+0020-007E=original\nmissing=off\n", m));
		TS_ASSERT(m.font.faceSet);
		TS_ASSERT_EQUALS(m.font.face.entries[0].path.toString('/'), "/maps/KO.SVF");
		TS_ASSERT_EQUALS(m.font.missing, 0x25A1u);
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 1u);
		TS_ASSERT_EQUALS(m.font.rangeValues[0].entries.size(), 2u);
		TS_ASSERT_EQUALS(m.font.advanceValues[0], Graphics::kHiResAdvanceFont);
		TS_ASSERT_EQUALS(m.font.originSpecs[0].span.lo, 0x2026u);
		const Graphics::HiResFontScope *cs4 = m.fontIdScope(4);
		TS_ASSERT(cs4);
		TS_ASSERT(cs4->missingSet);
		TS_ASSERT_EQUALS(cs4->missing, 0u);
		TS_ASSERT(cs4->rangeValues[0].endsInOriginal());
	}

	void test_duplicate_and_overlapping_spans() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font]\nrange.basic-latin=A.SVF\nrange.U+0020-007E=B.SVF\n"
					   "range.U+0100-010F=C.SVF\nrange.U+0108-0117=D.SVF\n", m));
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 2u);   // the later spelling and the equal-width overlap dropped
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font] range.U+0020-007E repeats range.basic-latin; ignoring it"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font] range.U+0108-0117 overlaps range.U+0100-010F at the same width; ignoring it"));
	}

	void test_qualified_font_section_merges_span_by_span() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font.2]\nrange.basic-latin=A.SVF\nrange.U+2026=B.SVF\n"
					   "[font.2:pc]\nrange.basic-latin=C.SVF\n", m, Graphics::kHiResKeysSci, "pc"));
		const Graphics::HiResFontScope *s = m.fontIdScope(2);
		TS_ASSERT_EQUALS(s->rangeSpecs.size(), 2u);
		bool sawC = false, sawB = false;
		for (uint i = 0; i < s->rangeValues.size(); ++i) {
			sawC |= s->rangeValues[i].entries[0].written == "C.SVF";
			sawB |= s->rangeValues[i].entries[0].written == "B.SVF";
		}
		TS_ASSERT(sawC && sawB);
	}

	void test_glyphs_sections() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[fonts]\nsym=SYM.SVF\n"
					   "[glyphs]\n0x07=original\n0x5e=u+2026\n0x21-0x23=+0xFEE0\n"
					   "[glyphs.2]\n0x07=sym:u+2620\n", m));
		TS_ASSERT_EQUALS(m.glyphs[0x07].kind, Graphics::kHiResGlyphOriginal);
		TS_ASSERT_EQUALS(m.glyphs[0x5e].value, 0x2026u);
		TS_ASSERT_EQUALS(m.glyphs[0x22].kind, Graphics::kHiResGlyphOffset);
		TS_ASSERT(m.glyphIds.contains(2));
		TS_ASSERT_EQUALS(m.glyphIds[2][0x07].kind, Graphics::kHiResGlyphTarget);
		TS_ASSERT_EQUALS(m.glyphIds[2][0x07].face.path.toString('/'), "/maps/SYM.SVF");
	}

	void test_removed_and_unknown_keys_warn() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[latin]\nmode=off\n[font.4]\nbitmap=X.SVF\nfase=Y.SVF\n", m));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [latin] is not read any more; see \"Ranges\" in graphics/hires_text/README.md"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [font.4] bitmap"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [font.4] fase"));
		TS_ASSERT(!m.fontIdScope(4) || !m.fontIdScope(4)->faceSet);
	}

	void test_unhonoured_keys_warn_per_engine() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font.4]\nmirror=horizontal\n[shadow]\nmode=outline\n", m, Graphics::kHiResKeysSci));
		TS_ASSERT(hasWarning(m, "SCI does not use [font.4] mirror"));
		TS_ASSERT(hasWarning(m, "SCI does not use [shadow] mode"));
		Graphics::HiResMap s;
		TS_ASSERT(load("[map]\nversion=2\n[font]\nshift=2\ncell=glyph\n", s, Graphics::kHiResKeysScumm));
		TS_ASSERT(hasWarning(s, "SCUMM does not use [font] shift"));
		TS_ASSERT(hasWarning(s, "SCUMM does not use [font] cell"));
	}

	// A qualified section whose qualifier the caller did not pass gets
	// no "does not use" warning (it is not this engine/game's section at
	// all), though a genuinely unknown key in it would still be reported.
	void test_unhonoured_key_skipped_for_foreign_qualifier() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[shadow:v5]\nmode=outline\n", m, Graphics::kHiResKeysSci, "pc"));
		TS_ASSERT_EQUALS(m.warnings.size(), 0u);
	}

	void test_invalid_values_are_ignored_not_substituted() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[render]\ntarget=truecolor\nscale=9\n[font]\nadvance=ttf\n[shadow]\nmode=glow\n", m));
		TS_ASSERT(!m.targetSet);
		TS_ASSERT(!m.scaleSet);
		TS_ASSERT(!m.font.advanceSet);
		TS_ASSERT_EQUALS(m.shadowMode, Graphics::kHiResShadowGame);
		TS_ASSERT_EQUALS(m.warnings.size(), 4u);
	}

	void test_plus_is_legal_in_key_names() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font]\nrange.U+2026=A.SVF\n", m));
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 1u);
	}

	// [font] face=same is warned once, in the loader (not deferred to
	// a later per-id compile step).
	void test_bare_font_face_same_is_warned() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font]\nface=same\n", m));
		TS_ASSERT(!m.font.faceSet);
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font] face=same has nothing to inherit; ignoring it"));
	}

	// [font.N] face=same means "inherit": identical to leaving the key out,
	// no warning.
	void test_font_id_face_same_means_inherit() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[font.2]\nface=same\n", m));
		const Graphics::HiResFontScope *s = m.fontIdScope(2);
		TS_ASSERT(s);
		TS_ASSERT(!s->faceSet);
		TS_ASSERT_EQUALS(m.warnings.size(), 0u);
	}

	// [translation.<lang>] is reserved for later use and the
	// v2 loader skips it whole - no unknown-section warning, no unknown-key
	// warnings inside it, nothing stored.
	void test_translation_lang_section_is_reserved() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[translation.ko]\nanything=goes\nfoo=bar\n", m));
		TS_ASSERT_EQUALS(m.warnings.size(), 0u);
	}

	// Review fix round 1, item 1: every other key goes through
	// stripInlineComment(); [map] version= must too.
	void test_map_version_ignores_inline_comment() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2 ; the current version\n", m));
		TS_ASSERT_EQUALS(m.version, 2);
	}

	// Review fix round 1, item 2: [glyphs:csN] is the removed old per-charset
	// form (design 3.3, "now [glyphs.N]"), not an ordinary qualified [glyphs]
	// section - it must get the "is not read any more" diagnostic, and its
	// keys must not be read into the map at all.
	void test_old_glyphs_csn_section_is_removed() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n[glyphs:cs0]\n0x07=original\n", m));
		TS_ASSERT_EQUALS(m.warnings.size(), 1u);
		TS_ASSERT_EQUALS(m.warnings[0], "HIRESTXT.MAP: [glyphs:cs0] is not read any more; use [glyphs.0] instead");
		TS_ASSERT(!m.glyphs.contains(0x07));
		// A genuine engine qualifier (not "csN") is still an ordinary
		// qualified [glyphs] section, merged as usual.
		Graphics::HiResMap m2;
		TS_ASSERT(load("[map]\nversion=2\n[glyphs:monkey2]\n0x07=original\n", m2, Graphics::kHiResKeysScumm, "monkey2"));
		TS_ASSERT_EQUALS(m2.warnings.size(), 0u);
		TS_ASSERT(m2.glyphs.contains(0x07));
	}

	// Review fix round 1, item 2 (continued): every other removed key form
	// design 3.3 lists ("font=" alias of face=, "bitmap=", "latin*=",
	// "metrics=", "baseline=", "[render] alpha=/mode=/metrics=") is already
	// covered by the generic unknown-key path (they are simply not in the
	// v2 known-key tables), matching spec 10.2's umbrella wording; this pins
	// that each of them actually does warn rather than being silently
	// accepted.
	void test_other_removed_key_forms_still_warn() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n"
					   "[font.5]\nfont=OLD.SVF\nlatin=half\nmetrics=game\nbaseline=2\n"
					   "[render]\nalpha=true\nmode=string\nmetrics=font\n", m));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [font.5] font"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [font.5] latin"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [font.5] metrics"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [font.5] baseline"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [render] alpha"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [render] mode"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: unknown key [render] metrics"));
	}

	// Review fix round 1, item 3: an invalid value is ignored (never
	// substituted) for the scalar keys test_invalid_values_are_ignored_not_substituted
	// does not already cover.
	void test_more_invalid_scalar_values_are_ignored() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n"
					   "[font]\nsize=999\npixel=999\nalign=sideways\ncell=weird\nmissing=notacode\n"
					   "origin=nowhere\nmirror=upsidedown\n"
					   "[layout]\nhangul=sideways\nkinsoku=maybe\nthai=2\n"
					   "[text]\nencoding=nonsense\n"
					   "[shadow]\noffset=notanumber\ncolor=999\nwidth=notanumber\nstyle=wobbly\n"
					   "shadow=notapair\nshadow_color=999\nshadow_alpha=999\n", m));
		TS_ASSERT(!m.font.sizeSet);
		TS_ASSERT(!m.font.pixelSet);
		TS_ASSERT(!m.font.alignSet);
		TS_ASSERT(!m.font.cellSet);
		TS_ASSERT(!m.font.missingSet);
		TS_ASSERT(!m.font.originSet);
		TS_ASSERT(!m.font.mirrorSet);
		TS_ASSERT(!m.layout.hangulSet);
		TS_ASSERT(!m.layout.kinsokuSet);
		TS_ASSERT(!m.layout.thaiSet);
		TS_ASSERT(!m.encodingSet);
		TS_ASSERT_EQUALS(m.shadowOffset, -1);
		TS_ASSERT(!m.shadowColorSet);
		TS_ASSERT_EQUALS(m.shadowWidthQ, -1);
		TS_ASSERT_EQUALS(m.shadowStyle, Graphics::kHiResOutlineRound);
		TS_ASSERT(!m.shadowShiftSet);
		TS_ASSERT(!m.shadowShiftColorSet);
		TS_ASSERT_EQUALS(m.shadowAlpha, 255);
		// A representative sample of the actual warning texts, so a future
		// change that drops the warning call itself (rather than the value
		// check) would still be caught.
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font] align 'sideways' is not game, cell or font; ignoring it"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [text] encoding 'nonsense' is not known; ignoring it"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [shadow] offset 'notanumber' is invalid; ignoring it"));
	}

	// mirror= is off|horizontal|vertical|both only (design 6.2.1): the old
	// true/on/yes/1/false/no/0/none/rotate aliases are rejected with a
	// warning, not accepted as an alias for one of the four values.
	void test_mirror_alias_v2_only() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=2\n"
					   "[font.2]\nmirror=true\n"
					   "[font.3]\nmirror=off\n"
					   "[font.4]\nmirror=horizontal\n"
					   "[font.5]\nmirror=vertical\n"
					   "[font.6]\nmirror=both\n"
					   "[font.7]\nmirror=on\n"
					   "[font.8]\nmirror=false\n"
					   "[font.9]\nmirror=rotate\n", m));
		const Graphics::HiResFontScope *f2 = m.fontIdScope(2);
		const Graphics::HiResFontScope *f3 = m.fontIdScope(3);
		const Graphics::HiResFontScope *f4 = m.fontIdScope(4);
		const Graphics::HiResFontScope *f5 = m.fontIdScope(5);
		const Graphics::HiResFontScope *f6 = m.fontIdScope(6);
		const Graphics::HiResFontScope *f7 = m.fontIdScope(7);
		const Graphics::HiResFontScope *f8 = m.fontIdScope(8);
		const Graphics::HiResFontScope *f9 = m.fontIdScope(9);
		TS_ASSERT(f3 && f4 && f5 && f6);
		if (!f3 || !f4 || !f5 || !f6)
			return;
		TS_ASSERT(f3->mirrorSet);
		TS_ASSERT_EQUALS(f3->mirror, Graphics::kHiResMirrorNone);
		TS_ASSERT(f4->mirrorSet);
		TS_ASSERT_EQUALS(f4->mirror, Graphics::kHiResMirrorHorizontal);
		TS_ASSERT(f5->mirrorSet);
		TS_ASSERT_EQUALS(f5->mirror, Graphics::kHiResMirrorVertical);
		TS_ASSERT(f6->mirrorSet);
		TS_ASSERT_EQUALS(f6->mirror, Graphics::kHiResMirrorBoth);
		// The legacy aliases: rejected, key left unset, each warned about.
		TS_ASSERT(!f2 || !f2->mirrorSet);
		TS_ASSERT(!f7 || !f7->mirrorSet);
		TS_ASSERT(!f8 || !f8->mirrorSet);
		TS_ASSERT(!f9 || !f9->mirrorSet);
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font.2] mirror 'true' is not off, horizontal, vertical or both; ignoring it"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font.7] mirror 'on' is not off, horizontal, vertical or both; ignoring it"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font.8] mirror 'false' is not off, horizontal, vertical or both; ignoring it"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font.9] mirror 'rotate' is not off, horizontal, vertical or both; ignoring it"));
	}
};

class HiResMapTargetTestSuite : public CxxTest::TestSuite {
	bool load(const char *text, Graphics::HiResMap &out, Graphics::HiResRenderTarget target,
			  const char *q0 = nullptr, bool quiet = false) {
		Common::Array<Common::String> qualifiers;
		if (q0)
			qualifiers.push_back(q0);
		const Common::String full = Common::String("[map]\nversion=2\n") + text;
		Common::MemoryReadStream stream((const byte *)full.c_str(), full.size());
		Graphics::HiResMapLoadOptions options;
		options.target = target;
		options.quiet = quiet;
		// kHiResKeysSci, not kHiResKeysScumm: twoPresets() below (align=/cell=,
		// design section 3.2's "[font]"/"[font.N]" table) exercises SCI-only
		// keys; kHiResKeysScumm would warn "SCUMM does not use [font] align"
		// and "... [font.40] cell" (design 10.3, kHiResKeysScumm's bitmask),
		// which is a correct, already-tested behaviour unrelated to this
		// suite's render-target qualifiers.
		return Graphics::HiResFontMap::loadMap(stream, Common::Path("/maps", '/'), qualifiers,
											   Graphics::kHiResKeysSci, out, options);
	}

	static bool hasWarning(const Graphics::HiResMap &m, const char *text) {
		for (uint i = 0; i < m.warnings.size(); ++i)
			if (m.warnings[i] == text)
				return true;
		return false;
	}

	static Common::String facePath(const Graphics::HiResFontScope *s) {
		return (s && s->faceSet && !s->face.entries.empty()) ? s->face.entries[0].path.toString('/') : Common::String("<none>");
	}

	static const char *twoPresets() {
		return "[fonts]\nui=KO2350G.SVF\nbody=KO2350B.SVF\n[fonts:clut8]\nui=KO2350.SVF\nbody=KO2350.SVF\n"
			   "[render]\nblend=auto\n[render:clut8]\nblend=off\n"
			   "[font]\nface=ui\nalign=cell\nmissing=u+25a1\n[font:clut8]\nalign=game\n"
			   "[font.4]\nface=body\n[font.40]\nface=body\ncell=glyph\nsize=18\n[font.40:clut8]\ncell=game\nsize=16\n";
	}

public:
	void test_bare_sections_serve_every_rgb_target_and_phase_one() {
		const Graphics::HiResRenderTarget targets[] = {
			Graphics::kHiResTargetAuto, Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb888 };
		for (uint i = 0; i < ARRAYSIZE(targets); ++i) {
			Graphics::HiResMap m;
			TS_ASSERT(load(twoPresets(), m, targets[i]));
			TS_ASSERT_EQUALS(facePath(&m.font), "/maps/KO2350G.SVF");
			TS_ASSERT_EQUALS(facePath(m.fontIdScope(4)), "/maps/KO2350B.SVF");
			TS_ASSERT_EQUALS(m.font.align, Graphics::kHiResAlignCell);
			TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendAuto);
			TS_ASSERT_EQUALS(m.fontIdScope(40)->size, 18);
			TS_ASSERT_EQUALS(m.loadedFor, targets[i]);
			TS_ASSERT(m.warnings.empty());
		}
	}

	void test_clut8_sections_win_key_by_key() {
		Graphics::HiResMap m;
		TS_ASSERT(load(twoPresets(), m, Graphics::kHiResTargetClut8));
		TS_ASSERT_EQUALS(facePath(&m.font), "/maps/KO2350.SVF");          // [fonts:clut8] refines the name
		TS_ASSERT_EQUALS(facePath(m.fontIdScope(4)), "/maps/KO2350.SVF");
		TS_ASSERT_EQUALS(m.font.align, Graphics::kHiResAlignGame);
		TS_ASSERT_EQUALS(m.font.missing, 0x25A1u);                         // not in [font:clut8]: the bare key
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(m.fontIdScope(40)->cell, Graphics::kHiResCellGame);
		TS_ASSERT_EQUALS(m.fontIdScope(40)->size, 16);
		TS_ASSERT(m.warnings.empty());
	}

	void test_target_ranges_merge_span_by_span() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[font]\nrange.basic-latin=A.SVF\nrange.U+2026=B.SVF\n[font:clut8]\nrange.U+0020-007E=C.SVF\n",
					   m, Graphics::kHiResTargetClut8));
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 2u);
		bool sawC = false, sawB = false, sawA = false;
		for (uint i = 0; i < m.font.rangeValues.size(); ++i) {
			sawA |= m.font.rangeValues[i].entries[0].written == "A.SVF";
			sawB |= m.font.rangeValues[i].entries[0].written == "B.SVF";
			sawC |= m.font.rangeValues[i].entries[0].written == "C.SVF";
		}
		TS_ASSERT(sawC && sawB && !sawA);
	}

	void test_target_glyph_sections() {
		const char *text = "[glyphs]\n0x5e=u+2026\n0x07=original\n[glyphs:clut8]\n0x07=u+2022\n[glyphs.2:clut8]\n0x5e=original\n";
		Graphics::HiResMap c;
		TS_ASSERT(load(text, c, Graphics::kHiResTargetClut8));
		TS_ASSERT_EQUALS(c.glyphs[0x07].value, 0x2022u);
		TS_ASSERT_EQUALS(c.glyphs[0x5e].value, 0x2026u);
		TS_ASSERT(c.glyphIds.contains(2));
		TS_ASSERT_EQUALS(c.glyphIds[2][0x5e].kind, Graphics::kHiResGlyphOriginal);
		Graphics::HiResMap r;
		TS_ASSERT(load(text, r, Graphics::kHiResTargetRgb888));
		TS_ASSERT_EQUALS(r.glyphs[0x07].kind, Graphics::kHiResGlyphOriginal);
		TS_ASSERT(!r.glyphIds.contains(2));
	}

	void test_render_target_key_in_a_target_section_is_refused() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[render]\ntarget=rgb888\n[render:clut8]\ntarget=clut8\nscale=2\n", m, Graphics::kHiResTargetClut8));
		TS_ASSERT_EQUALS(m.target, Graphics::kHiResTargetRgb888);
		TS_ASSERT(m.scaleSet);
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [render:clut8] target cannot depend on the render target; ignoring it"));
	}

	void test_engine_qualifier_beats_target_and_both_combine() {
		const char *all = "[font]\nface=A.SVF\n[font:clut8]\nface=B.SVF\n[font:monkey2]\nface=C.SVF\n"
						  "[font:monkey2:clut8]\nface=D.SVF\n";
		Graphics::HiResMap m;
		TS_ASSERT(load(all, m, Graphics::kHiResTargetClut8, "monkey2"));
		TS_ASSERT_EQUALS(facePath(&m.font), "/maps/D.SVF");
		TS_ASSERT(load(all, m, Graphics::kHiResTargetRgb888, "monkey2"));
		TS_ASSERT_EQUALS(facePath(&m.font), "/maps/C.SVF");
		TS_ASSERT(load(all, m, Graphics::kHiResTargetClut8, "tentacle"));
		TS_ASSERT_EQUALS(facePath(&m.font), "/maps/B.SVF");
		TS_ASSERT(load("[font]\nface=A.SVF\n[font:clut8]\nface=B.SVF\n[font:monkey2]\nface=C.SVF\n", m,
					   Graphics::kHiResTargetClut8, "monkey2"));
		TS_ASSERT_EQUALS(facePath(&m.font), "/maps/C.SVF");                // engine-only beats target-only
	}

	void test_malformed_target_qualifiers_warn_and_are_ignored() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[font:auto]\nface=X.SVF\n[font:clut8:monkey2]\nface=Y.SVF\n[font:pc:v5]\nface=Z.SVF\n"
					   "[font:a:b:clut8]\nface=W.SVF\n[text:clut8]\nencoding=cp949\n[layout:clut8]\nhangul=any\n",
					   m, Graphics::kHiResTargetClut8, "monkey2"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font:auto]: auto is not a render-target qualifier; ignoring the section"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font:clut8:monkey2]: the render target goes last ([font:monkey2:clut8]); ignoring the section"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font:pc:v5]: the second qualifier must be clut8, rgb565 or rgb888; ignoring the section"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [font:a:b:clut8]: a section takes at most two qualifiers; ignoring the section"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [text:clut8]: [text] cannot depend on the render target; ignoring the section"));
		TS_ASSERT(hasWarning(m, "HIRESTXT.MAP: [layout:clut8]: [layout] cannot depend on the render target; ignoring the section"));
		TS_ASSERT(!m.font.faceSet);
		TS_ASSERT(!m.encodingSet);
		TS_ASSERT(!m.layout.hangulSet);
		TS_ASSERT_EQUALS(m.warnings.size(), 6u);
	}

	void test_quiet_load_collects_the_same_warnings() {
		Graphics::HiResMap loud, quiet;
		TS_ASSERT(load("[font:auto]\nface=X.SVF\n", loud, Graphics::kHiResTargetAuto));
		TS_ASSERT(load("[font:auto]\nface=X.SVF\n", quiet, Graphics::kHiResTargetAuto, nullptr, true));
		TS_ASSERT_EQUALS(quiet.warnings.size(), loud.warnings.size());
		TS_ASSERT_EQUALS(quiet.warnings.size(), 1u);
	}
};
