#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/id_plan.h"

class HiResIdPlanTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap _map;
	Graphics::HiResFontScope _engine;   // SCUMM-like: basic-latin = same, advance game

	void load(const char *text) {
		const Common::String full = Common::String("[map]\nversion=2\n") + text;
		Common::MemoryReadStream s((const byte *)full.c_str(), full.size());
		Common::Array<Common::String> q;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/maps", '/'), q, Graphics::kHiResKeysScumm, _map));
	}

	Graphics::HiResIdPlan plan(int id, const Graphics::HiResIniOverrides &ini = Graphics::HiResIniOverrides()) {
		Common::Array<Common::String> w;
		return Graphics::compileIdPlan(_map, true, id, ini, _engine, Common::Path("/maps", '/'),
									   Common::Path("/games/g", '/'), w);
	}

	static Common::String first(const Graphics::HiResFaceChain *c) {
		return (c && !c->faces.empty()) ? c->faces[0].path.toString('/') : Common::String("<game>");
	}

public:
	void setUp() {
		_map.clear();
		_engine = Graphics::HiResFontScope();
		Graphics::HiResRangeSpec latin;
		Common::String error;
		Graphics::parseRangeSpec("basic-latin", latin, error);
		Graphics::HiResFontValue same;
		Graphics::HiResFaceEntry e;
		e.kind = Graphics::kHiResFaceSame;
		same.entries.push_back(e);
		_engine.rangeSpecs.push_back(latin);
		_engine.rangeValues.push_back(same);
		_engine.advanceSpecs.push_back(latin);
		_engine.advanceValues.push_back(Graphics::kHiResAdvanceGame);
	}

	void test_id_chain_precedence() {
		load("[font]\nface=A.SVF\n[font.4]\nface=B.SVF\n");
		TS_ASSERT_EQUALS(first(plan(4).chainFor(0xAC00)), "/maps/B.SVF");
		TS_ASSERT_EQUALS(first(plan(1).chainFor(0xAC00)), "/maps/A.SVF");
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "C.TTF";
		TS_ASSERT_EQUALS(first(plan(4, ini).chainFor(0xAC00)), "/games/g/C.TTF");   // ini paths: game folder
	}

	void test_range_rule_then_id_chain_appended() {
		load("[fonts]\nlat=LAT.SVF\n[font]\nface=KO.SVF\nrange.basic-latin=lat\n");
		const Graphics::HiResIdPlan p = plan(0);
		const Graphics::HiResFaceChain *c = p.chainFor('A');
		TS_ASSERT_EQUALS(c->faces.size(), 2u);
		TS_ASSERT_EQUALS(c->faces[0].path.toString('/'), "/maps/LAT.SVF");
		TS_ASSERT_EQUALS(c->faces[1].path.toString('/'), "/maps/KO.SVF");
		TS_ASSERT_EQUALS(first(p.chainFor(0xAC00)), "/maps/KO.SVF");
	}

	void test_same_expands_in_place_and_original_stops() {
		load("[fonts]\ndisp=D.TTF\n[font]\nface=KO.SVF\n[font.4]\nrange.general-punctuation=disp, same\n"
			 "range.U+2020=disp, original\n");
		const Graphics::HiResIdPlan p = plan(4);
		const Graphics::HiResFaceChain *c = p.chainFor(0x2026);
		TS_ASSERT_EQUALS(c->faces.size(), 2u);
		TS_ASSERT(!c->endsInOriginal);
		const Graphics::HiResFaceChain *d = p.chainFor(0x2020);
		TS_ASSERT_EQUALS(d->faces.size(), 1u);
		TS_ASSERT(d->endsInOriginal);
	}

	void test_engine_scope_is_below_font() {
		load("[font]\nface=KO.SVF\n");
		TS_ASSERT_EQUALS(first(plan(0).chainFor('A')), "/maps/KO.SVF");            // engine: basic-latin = same
		TS_ASSERT_EQUALS(plan(0).advanceFor('A'), Graphics::kHiResAdvanceGame);    // engine default
		TS_ASSERT_EQUALS(plan(0).advanceFor(0xAC00), Graphics::kHiResAdvanceEngine);
		load("[font]\nface=KO.SVF\nrange.basic-latin=original\nadvance.basic-latin=font\n");
		TS_ASSERT_EQUALS(plan(0).chainFor('A'), (const Graphics::HiResFaceChain *)nullptr);
		TS_ASSERT_EQUALS(plan(0).advanceFor('A'), Graphics::kHiResAdvanceFont);
	}

	void test_face_original_turns_the_id_off_except_its_own_ranges() {
		load("[font]\nface=KO.SVF\nrange.U+2026=E.SVF\n[font.2]\nface=original\nrange.U+0021=X.SVF\n");
		const Graphics::HiResIdPlan p = plan(2);
		TS_ASSERT(p.original);
		TS_ASSERT_EQUALS(p.chainFor(0xAC00), (const Graphics::HiResFaceChain *)nullptr);
		TS_ASSERT_EQUALS(p.chainFor(0x2026), (const Graphics::HiResFaceChain *)nullptr);   // [font]'s rule not applied
		TS_ASSERT_EQUALS(first(p.chainFor(0x21)), "/maps/X.SVF");
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "original";
		TS_ASSERT_EQUALS(plan(2, ini).chainFor(0x21), (const Graphics::HiResFaceChain *)nullptr);
	}

	void test_ini_advance_beats_every_key() {
		load("[font]\nface=KO.SVF\nadvance=font\nadvance.basic-latin=cell\n");
		Graphics::HiResIniOverrides ini;
		ini.advanceSet = true;
		ini.advance = Graphics::kHiResAdvanceGame;
		TS_ASSERT_EQUALS(plan(0, ini).advanceFor('A'), Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(plan(0, ini).advanceFor(0xAC00), Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(plan(0).advanceFor(0xAC00), Graphics::kHiResAdvanceFont);
	}

	void test_missing_per_id() {
		load("[font]\nface=KO.SVF\nmissing=u+25a1\n[font.3]\nmissing=off\n");
		TS_ASSERT_EQUALS(plan(0).missing, 0x25A1u);
		TS_ASSERT_EQUALS(plan(3).missing, 0u);
	}

	void test_glyph_steps_remap_then_rules_apply() {
		load("[fonts]\nsym=SYM.SVF\n[font]\nface=KO.SVF\norigin.general-punctuation=face\norigin.misc-symbols=face\n"
			 "[glyphs]\n0x07=original\n0x5e=u+2026\n[glyphs.2]\n0x07=sym:u+2620\n0x08=ICONS.SVF:u+e001\n");
		uint32 cp = 0;
		const Graphics::HiResIdPlan p0 = plan(0);
		TS_ASSERT_EQUALS(p0.glyphFor(0x07, 0x07, cp), Graphics::kHiResGlyphStepGame);
		TS_ASSERT_EQUALS(p0.glyphFor(0x5e, 0x5e, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT_EQUALS(cp, 0x2026u);
		TS_ASSERT_EQUALS(p0.originFor(cp), Graphics::kHiResOriginFace);            // the remap goes through the rules
		TS_ASSERT_EQUALS(p0.glyphFor(0x41, 0x41, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT_EQUALS(cp, 0x41u);                                                 // no entry: decoded

		const Graphics::HiResIdPlan p2 = plan(2);
		TS_ASSERT_EQUALS(p2.glyphFor(0x07, 0x07, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT(cp >= Graphics::kHiResTargetBase);
		const Graphics::HiResGlyphTarget *t = p2.target(cp);
		TS_ASSERT(t);
		TS_ASSERT_EQUALS(t->cp, 0x2620u);
		TS_ASSERT_EQUALS(t->face.path.toString('/'), "/maps/SYM.SVF");
		TS_ASSERT_EQUALS(p2.chainFor(cp), (const Graphics::HiResFaceChain *)nullptr);  // range rules bypassed
		TS_ASSERT_EQUALS(p2.originFor(cp), Graphics::kHiResOriginFace);              // origin uses the real cp
		TS_ASSERT_EQUALS(p2.glyphFor(0x08, 0x08, cp), Graphics::kHiResGlyphStepDraw);
		TS_ASSERT_EQUALS(p2.target(cp)->cp, 0xE001u);                               // PUA target in a custom SVF
		TS_ASSERT_EQUALS(p2.glyphFor(0x5e, 0x5e, cp), Graphics::kHiResGlyphStepDraw);  // [glyphs] still applies to id 2
		TS_ASSERT_EQUALS(cp, 0x2026u);
	}

	void test_offset_and_fullwidth_recipe() {
		load("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n0x20=u+3000\n");
		uint32 cp = 0;
		plan(0).glyphFor('A', 'A', cp);
		TS_ASSERT_EQUALS(cp, 0xFF21u);
		plan(0).glyphFor(' ', ' ', cp);
		TS_ASSERT_EQUALS(cp, 0x3000u);
	}

	void test_hash_changes_with_rules() {
		load("[font]\nface=KO.SVF\n");
		const uint32 a = plan(0).hash();
		load("[font]\nface=KO.SVF\nrange.basic-latin=original\n");
		TS_ASSERT_DIFFERS(a, plan(0).hash());
	}

	// An embedded `same` (one entry among several) has no meaning under
	// any of design 5.2's three bullets (those only cover a bare `same`),
	// so it is dropped with a warning (10.2's default for "same where it
	// has no meaning"), not silently combined away.
	void test_embedded_same_in_face_value_warns_and_is_dropped() {
		load("[font]\nface=KO.SVF\n[font.4]\nface=A.SVF, same, B.SVF\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p = Graphics::compileIdPlan(_map, true, 4, Graphics::HiResIniOverrides(), _engine,
																 Common::Path("/maps", '/'), Common::Path("/games/g", '/'), w);
		const Graphics::HiResFaceChain *c = p.chainFor(0xAC00);
		TS_ASSERT_EQUALS(c->faces.size(), 2u);
		TS_ASSERT_EQUALS(c->faces[0].path.toString('/'), "/maps/A.SVF");
		TS_ASSERT_EQUALS(c->faces[1].path.toString('/'), "/maps/B.SVF");
		bool found = false;
		for (uint i = 0; i < w.size(); ++i)
			if (w[i] == "HIRESTXT.MAP: [font.4] face=same has no meaning here; ignoring it")
				found = true;
		TS_ASSERT(found);

		// Same shape, from the ini: warned with the ini's own wording.
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "A.SVF, same, B.SVF";
		Common::Array<Common::String> w2;
		const Graphics::HiResIdPlan p2 = Graphics::compileIdPlan(_map, true, 4, ini, _engine,
																  Common::Path("/maps", '/'), Common::Path("/games/g", '/'), w2);
		const Graphics::HiResFaceChain *c2 = p2.chainFor(0xAC00);
		TS_ASSERT_EQUALS(c2->faces.size(), 2u);
		TS_ASSERT_EQUALS(c2->faces[0].path.toString('/'), "/games/g/A.SVF");
		TS_ASSERT_EQUALS(c2->faces[1].path.toString('/'), "/games/g/B.SVF");
		bool found2 = false;
		for (uint i = 0; i < w2.size(); ++i)
			if (w2[i] == "hires_text_face=same has no meaning; ignoring it")
				found2 = true;
		TS_ASSERT(found2);
	}

	// mapLoaded=false makes every map-sourced key behave as if the map
	// were empty; the ini and the engine scope still apply.
	void test_map_loaded_false_ignores_the_map_but_keeps_ini_and_engine() {
		load("[font]\nface=KO.SVF\n");

		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan noMap = Graphics::compileIdPlan(_map, false, 0, Graphics::HiResIniOverrides(),
																	 _engine, Common::Path("/maps", '/'),
																	 Common::Path("/games/g", '/'), w);
		// The map's KO.SVF is ignored; the engine's basic-latin=same then
		// expands to an id chain that is empty, so there is no face at all.
		TS_ASSERT_EQUALS(noMap.chainFor('A'), (const Graphics::HiResFaceChain *)nullptr);
		TS_ASSERT_EQUALS(noMap.chainFor(0xAC00), (const Graphics::HiResFaceChain *)nullptr);

		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "C.TTF";
		Common::Array<Common::String> w2;
		const Graphics::HiResIdPlan withIni = Graphics::compileIdPlan(_map, false, 0, ini, _engine,
																	   Common::Path("/maps", '/'),
																	   Common::Path("/games/g", '/'), w2);
		TS_ASSERT_EQUALS(first(withIni.chainFor('A')), "/games/g/C.TTF");
	}

	// A rule value's `same` composed at the very end, on an id chain
	// that itself ends in `original` after a real face, propagates the id
	// chain's own endsInOriginal onto the composed chain (design 6.5 step
	// 4: with a `same` present, no extra implicit append happens, so the
	// value's own literal composition - `same` spliced in place - decides).
	void test_same_last_in_rule_value_inherits_id_chains_ends_in_original() {
		load("[font]\nface=KO.SVF, original\n[font.4]\nrange.U+2026=X.SVF, same\n");
		const Graphics::HiResIdPlan p = plan(4);
		const Graphics::HiResFaceChain *c = p.chainFor(0x2026);
		TS_ASSERT(c);
		TS_ASSERT_EQUALS(c->faces.size(), 2u);
		TS_ASSERT_EQUALS(c->faces[0].path.toString('/'), "/maps/X.SVF");
		TS_ASSERT_EQUALS(c->faces[1].path.toString('/'), "/maps/KO.SVF");
		TS_ASSERT(c->endsInOriginal);
	}

	// hash() regression coverage beyond a single range-rule change.
	void test_hash_covers_every_field() {
		load("[font]\nface=KO.SVF\n");
		const uint32 base = plan(0).hash();

		load("[font]\nface=KO.SVF\n[font.0]\nface=original\n");
		TS_ASSERT_DIFFERS(base, plan(0).hash());

		load("[font]\nface=KO.SVF\n");
		Graphics::HiResIniOverrides forced;
		forced.advanceSet = true;
		forced.advance = Graphics::kHiResAdvanceFont;
		TS_ASSERT_DIFFERS(base, plan(0, forced).hash());

		load("[font]\nface=KO.SVF\nadvance=font\n");
		TS_ASSERT_DIFFERS(base, plan(0).hash());

		load("[font]\nface=KO.SVF\norigin=face\n");
		TS_ASSERT_DIFFERS(base, plan(0).hash());

		load("[font]\nface=KO.SVF\nmirror=horizontal\n");
		TS_ASSERT_DIFFERS(base, plan(0).hash());

		load("[font]\nface=KO.SVF\nsize=20\n");
		TS_ASSERT_DIFFERS(base, plan(0).hash());

		load("[font]\nface=KO.SVF\n[glyphs]\n0x41=u+0042\n");
		TS_ASSERT_DIFFERS(base, plan(0).hash());

		load("[fonts]\nsym=SYM.SVF\n[font]\nface=KO.SVF\n[glyphs]\n0x41=sym:u+2620\n");
		TS_ASSERT_DIFFERS(base, plan(0).hash());
	}

	static bool coverageG(const Common::Path &face, void *) { return face.toString('/').hasSuffix("G.SVF"); }
	static bool coverageT(const Common::Path &face, void *) { return face.toString('/').hasSuffix("T.SVF"); }
	static bool coverageNone(const Common::Path &, void *) { return false; }

	void test_wanted_render_target_phase_one() {
		bool explicitTarget = false;
		Graphics::HiResIniOverrides ini;
		load("[render]\ntarget=rgb565\n");
		TS_ASSERT_EQUALS(Graphics::wantedRenderTarget(_map, true, ini, false, explicitTarget), Graphics::kHiResTargetRgb565);
		TS_ASSERT(explicitTarget);
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetAuto;                     // ini auto: no ini preference, the map decides
		TS_ASSERT_EQUALS(Graphics::wantedRenderTarget(_map, true, ini, false, explicitTarget), Graphics::kHiResTargetRgb565);
		ini.target = Graphics::kHiResTargetClut8;
		TS_ASSERT_EQUALS(Graphics::wantedRenderTarget(_map, true, ini, true, explicitTarget), Graphics::kHiResTargetClut8);
		ini = Graphics::HiResIniOverrides();
		load("[font]\nface=KO.SVF\n");
		TS_ASSERT_EQUALS(Graphics::wantedRenderTarget(_map, true, ini, true, explicitTarget), Graphics::kHiResTargetRgb888);
		TS_ASSERT(!explicitTarget);
		TS_ASSERT_EQUALS(Graphics::wantedRenderTarget(_map, true, ini, false, explicitTarget), Graphics::kHiResTargetClut8);
		load("[render]\nblend=off\n[font]\nface=KO.SVF\n");
		TS_ASSERT_EQUALS(Graphics::wantedRenderTarget(_map, true, ini, true, explicitTarget), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::wantedRenderTarget(_map, false, ini, true, explicitTarget), Graphics::kHiResTargetRgb888);
	}

	void test_map_has_coverage_walks_the_phase_one_view() {
		load("[fonts]\nui=G.SVF\n[fonts:clut8]\nui=L.SVF\n[font]\nface=L.SVF\n[font.4]\nrange.basic-latin=ui\n"
			 "[glyphs]\n0x07=T.SVF:u+2620\n");
		const Common::Path m("/maps", '/'), g("/games/g", '/');
		Graphics::HiResIniOverrides ini;
		TS_ASSERT(Graphics::mapHasCoverage(_map, true, ini, m, g, coverageG, nullptr));     // [fonts] ui, not [fonts:clut8]
		TS_ASSERT(Graphics::mapHasCoverage(_map, true, ini, m, g, coverageT, nullptr));     // a [glyphs] target counts
		TS_ASSERT(!Graphics::mapHasCoverage(_map, true, ini, m, g, coverageNone, nullptr));
		TS_ASSERT(!Graphics::mapHasCoverage(_map, false, ini, m, g, coverageG, nullptr));   // no map, no ini face
		ini.faceSet = true;
		ini.face = "X.SVF";
		TS_ASSERT(!Graphics::mapHasCoverage(_map, false, ini, m, g, coverageG, nullptr));
		ini.face = "XG.SVF";
		TS_ASSERT(Graphics::mapHasCoverage(_map, false, ini, m, g, coverageG, nullptr));    // /games/g/XG.SVF
	}
};
