#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/config-manager.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/pixelformat.h"

#include "../../system/null_osystem.h"

class ScummHiResRenderTargetTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap map(const char *render) {
		const Common::String text = Common::String("[map]\nversion=2\n[render]\n") + render;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		Common::Array<Common::String> q;
		Graphics::HiResMap m;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/m", '/'), q, Graphics::kHiResKeysScumm, m));
		return m;
	}

	static void writeFile(const Common::Path &path, const Common::String &text) {
		Common::DumpFile f;
		TS_ASSERT(f.open(path));
		f.write(text.c_str(), text.size());
		f.close();
	}

	/// A minimal SVFN header: magic, then the bpp byte at offset 8 (the
	/// coverage probe hires_alpha_default.h's faceHasCoverage() test reads;
	/// mapHasCoverage() here reaches the same probe through the map).
	static void writeSvf(const Common::Path &path, byte bpp) {
		Common::Array<byte> b(16, (byte)0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		b[8] = bpp;
		Common::DumpFile f;
		TS_ASSERT(f.open(path));
		f.write(b.begin(), b.size());
		f.close();
	}

	/// A directory of this run's own (mkdtemp in the working directory), and
	/// every file a test wrote into it, removed again by tearDown().
	Common::String _tmpDir;
	Common::Array<Common::String> _tmpFiles;

	Common::Path tmpDir() {
		if (_tmpDir.empty()) {
			char name[] = "hires-render-target-XXXXXX";
			if (mkdtemp(name)) {
				// Absolute: a map's relative face paths resolve against it.
				char *full = realpath(name, nullptr);
				_tmpDir = full ? full : name;
				free(full);
			}
			TS_ASSERT(!_tmpDir.empty());
		}
		return Common::Path(_tmpDir, '/');
	}

	Common::Path tmpFile(const char *name) {
		const Common::Path p = tmpDir().join(Common::Path(name, '/'));
		_tmpFiles.push_back(p.toString('/'));
		return p;
	}

	/// Points ConfMan's active domain at a fresh game domain holding @p keys
	/// ("key=value" lines), for loadConfig(), which reads the ini through it.
	static void useDomain(const char *keys) {
		if (ConfMan.hasGameDomain(kDomain))
			ConfMan.removeGameDomain(kDomain);
		ConfMan.addGameDomain(kDomain);
		ConfMan.setActiveDomain(kDomain);
		Common::String rest(keys);
		while (!rest.empty()) {
			const char *nl = strchr(rest.c_str(), '\n');
			const Common::String line = nl ? Common::String(rest.c_str(), nl) : rest;
			rest = nl ? Common::String(nl + 1) : Common::String();
			const char *eq = strchr(line.c_str(), '=');
			if (eq)
				ConfMan.set(Common::String(line.c_str(), eq), Common::String(eq + 1), kDomain);
		}
	}

	static constexpr const char *kDomain = "hires-render-target-test";

	/// What a desktop backend offers: both RGB families and CLUT8.
	static const Common::List<Graphics::PixelFormat> &formats() {
		static Common::List<Graphics::PixelFormat> list;
		if (list.empty()) {
			list.push_back(rgb565());
			list.push_back(Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0));
			list.push_back(Graphics::PixelFormat::createFormatCLUT8());
		}
		return list;
	}

	static Graphics::PixelFormat rgb888() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }
	static Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }

	/// A map naming one face for id 0 per render target, as the shipped
	/// two-preset maps do: @p bare for the bare sections, @p rgb for
	/// [font.0:rgb888]; @p extra is appended as is.
	Common::Path writeMap(const char *render, const char *extra = "") {
		const Common::Path one = tmpFile("one.svf");
		const Common::Path two = tmpFile("two.svf");
		writeSvf(one, 1);
		writeSvf(two, 2);
		const Common::Path mapPath = tmpFile("HIRESTXT.MAP");
		writeFile(mapPath, Common::String::format(
			"[map]\nversion=2\n"
			"[render]\n%s\n"
			"[font.0]\nface=%s\n"
			"[font.0:clut8]\nface=%s\n"
			"%s",
			render, two.toString('/').c_str(), one.toString('/').c_str(), extra));
		return mapPath;
	}

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
		for (uint i = 0; i < _tmpFiles.size(); ++i)
			remove(_tmpFiles[i].c_str());
		_tmpFiles.clear();
		if (!_tmpDir.empty())
			remove(_tmpDir.c_str());
		_tmpDir.clear();
		if (ConfMan.hasGameDomain(kDomain))
			ConfMan.removeGameDomain(kDomain);
		ConfMan.setActiveDomain("");
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_explicit_map_target() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb565\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetRgb565);
	}

	void test_ini_beats_map() {
		Graphics::HiResIniOverrides ini;
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetClut8;
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb888\n"), true, ini, 5, true, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT(w.empty());
	}

	void test_auto_follows_coverage_and_blend() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=auto\n"), true, Graphics::HiResIniOverrides(), 5, false, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("blend=auto\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("blend=off\n"), true, Graphics::HiResIniOverrides(), 5, true, w),
						 Graphics::kHiResTargetClut8);
	}

	void test_ini_auto_defers_to_the_map() {
		Graphics::HiResIniOverrides ini;
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetAuto;
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb565\n"), true, ini, 5, true, w),
						 Graphics::kHiResTargetRgb565);
	}

	void test_v7_stays_paletted() {
		Common::String w;
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::wantedTarget(map("target=rgb888\n"), true, Graphics::HiResIniOverrides(), 7, true, w),
						 Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(w, "SCUMM v7+ keeps a paletted screen; render_target=rgb888 ignored");
	}

	void test_scale_default_limits_and_dos() {
		Common::String w;
		const Graphics::HiResScaleLimits desktop = { 1, 3 };
		const Graphics::HiResScaleLimits dos = { 2, 2 };
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("target=auto\n"), true, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, Graphics::HiResIniOverrides(), desktop, w), 3);
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, Graphics::HiResIniOverrides(), dos, w), 2);
		TS_ASSERT_EQUALS(w, "the DOS backend runs hi-res text at 2x only; using 2");
		Graphics::HiResIniOverrides ini;
		ini.scaleSet = true;
		ini.scale = 1;
		w.clear();
		TS_ASSERT_EQUALS(Scumm::ScummHiResText::resolvedScale(map("scale=3\n"), true, ini, desktop, w), 1);
	}

	/**
	 * Task 8 review M2: adoptScreen() must refresh wantsAlpha() alongside
	 * blend()/anyCoverage(), or createCoverage() (gated on wantsAlpha(),
	 * called once after adoptScreen()/setAlphaActive() in
	 * ScummEngine::init()) can be left never allocating a coverage surface
	 * for a corrected target that now wants blending - alphaActive() would
	 * then say blending is on with nothing for gfx.cpp's composite path to
	 * read.
	 *
	 * The map's predicted (clut8) section is `blend=off` with a 1bpp face -
	 * nothing wants blending. Its rgb888 section is `blend=auto` with a 2bpp
	 * (covering) face - once the backend actually hands back an rgb888
	 * screen, blend()/anyCoverage() (and, with the fix, wantsAlpha()) must
	 * all agree that blending is wanted.
	 */
	void test_adopt_screen_refreshes_wants_alpha_and_coverage() {
		const Common::Path onePath = tmpFile("one.svf");
		const Common::Path twoPath = tmpFile("two.svf");
		writeSvf(onePath, 1);
		writeSvf(twoPath, 2);

		const Common::Path mapPath = tmpFile("test.map");
		writeFile(mapPath, Common::String::format(
			"[map]\nversion=2\n"
			"[render]\nblend=off\n"
			"[font.0]\nface=%s\n"
			"[render:rgb888]\nblend=auto\n"
			"[font.0:rgb888]\nface=%s\n",
			onePath.toString().c_str(), twoPath.toString().c_str()));

		Common::Array<Common::String> qualifiers;
		qualifiers.push_back("v5");

		Graphics::HiResMap predicted;
		Graphics::HiResMapLoadOptions opts;
		opts.target = Graphics::kHiResTargetClut8;
		TS_ASSERT(Graphics::HiResFontMap::loadMapFile(mapPath, qualifiers, Graphics::kHiResKeysScumm, predicted, opts));

		Scumm::ScummHiResText hr;
		Scumm::HiResOverlay overlay;
		overlay.create(16, 16, false); // the index plane; createCoverage() adds the coverage plane onto it
		hr.useOverlay(&overlay);
		hr.adoptMapForScreenTest(predicted, Graphics::HiResIniOverrides(), Graphics::kHiResTargetClut8, 5, mapPath, qualifiers);

		TS_ASSERT_EQUALS(hr.blend(), Graphics::kHiResBlendOff);
		TS_ASSERT(!hr.anyCoverage());
		TS_ASSERT(!hr.wantsAlpha());
		hr.createCoverage(16, 16);
		TS_ASSERT(!hr.coverage());

		// The backend actually gave an rgb888 screen.
		hr.adoptScreen(Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0));
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(hr.blend(), Graphics::kHiResBlendAuto);
		TS_ASSERT(hr.anyCoverage());
		// The fix under test: wantsAlpha() follows, rather than staying
		// stale at the predicted (clut8) section's answer.
		TS_ASSERT(hr.wantsAlpha());

		// createCoverage()'s gate now matches: the surface is allocated,
		// consistent with alphaActive() once scumm.cpp sets it the same way
		// scumm.cpp itself would (Graphics::blendActive() off the now-
		// corrected blend()/anyCoverage(), screen not CLUT8).
		hr.createCoverage(16, 16);
		TS_ASSERT(hr.coverage());
		hr.setAlphaActive(Graphics::blendActive(hr.blend(), hr.anyCoverage(), false));
		TS_ASSERT(hr.alphaActive());
	}
	/**
	 * The screen SCUMM asks for. Before v7 the engine only produces
	 * true-colour output on its blending path (palette cache, no backend
	 * palette, the RGB sinks); everywhere else it sets the backend palette
	 * and blits 1-byte pixels. So the request is paletted unless the text will
	 * actually blend, whatever render_target says.
	 */
	void test_screen_is_paletted_unless_the_text_blends() {
		using Scumm::ScummHiResText;
		const Graphics::HiResRenderTarget c = Graphics::kHiResTargetClut8;
		const Graphics::HiResRenderTarget t888 = Graphics::kHiResTargetRgb888;
		const Graphics::HiResRenderTarget t565 = Graphics::kHiResTargetRgb565;
		// hires_text=false, or no map and no faces: the layer is off.
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(false, true, t888, Graphics::kHiResBlendAuto, false), c);
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(false, true, t888, Graphics::kHiResBlendOn, true), c);
		// hires_text_blend=off (the player's checkbox, or [render:rgb888] blend=off).
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, true, t888, Graphics::kHiResBlendOff, true), c);
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, true, t565, Graphics::kHiResBlendOff, true), c);
		// Only 1 bpp faces: nothing to blend, even with blend=on.
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, true, t888, Graphics::kHiResBlendOn, false), c);
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, true, t888, Graphics::kHiResBlendAuto, false), c);
		// A game that cannot blend (v7+).
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, false, t888, Graphics::kHiResBlendOn, true), c);
		// Asked-for clut8 stays clut8.
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, true, c, Graphics::kHiResBlendOn, true), c);
		// The text blends: the predicted RGB family stands.
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, true, t888, Graphics::kHiResBlendAuto, true), t888);
		TS_ASSERT_EQUALS(ScummHiResText::screenTargetFor(true, true, t565, Graphics::kHiResBlendOn, true), t565);
	}

	/// The one section 7.1 warning names what was asked, not what the
	/// prediction already turned it into.
	void test_target_note_names_the_wanted_target() {
		using Scumm::ScummHiResText;
		TS_ASSERT_EQUALS(ScummHiResText::targetNote(Graphics::kHiResTargetRgb888, true, Graphics::kHiResTargetClut8),
						 "render_target=rgb888 is not available here; using clut8");
		TS_ASSERT_EQUALS(ScummHiResText::targetNote(Graphics::kHiResTargetRgb565, true, Graphics::kHiResTargetRgb888),
						 "render_target=rgb565 is not available here; using rgb888");
		// Nothing asked (the auto rule chose it), or what was asked was set.
		TS_ASSERT(ScummHiResText::targetNote(Graphics::kHiResTargetRgb888, false, Graphics::kHiResTargetClut8).empty());
		TS_ASSERT(ScummHiResText::targetNote(Graphics::kHiResTargetRgb888, true, Graphics::kHiResTargetRgb888).empty());
		TS_ASSERT(ScummHiResText::targetNote(Graphics::kHiResTargetClut8, true, Graphics::kHiResTargetClut8).empty());
	}

	void test_hires_text_off_with_rgb888_keeps_a_paletted_screen() {
		writeMap("blend=auto");
		useDomain("hires_text=false\nrender_target=rgb888\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT(!hr.enabled());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(hr.askedTarget(), Graphics::kHiResTargetRgb888);
	}

	void test_no_map_with_rgb888_keeps_a_paletted_screen() {
		tmpDir();
		useDomain("render_target=rgb888\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT(!hr.enabled());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
	}

	void test_blend_off_with_rgb888_takes_the_clut8_sections() {
		writeMap("blend=auto");
		useDomain("render_target=rgb888\nhires_text_blend=off\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT(hr.enabled());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
		// [font.0:clut8] names the 1 bpp face: the sections follow the screen.
		TS_ASSERT(!hr.anyCoverage());
		TS_ASSERT(!hr.wantsAlpha());
	}

	void test_map_blend_off_for_rgb888_takes_the_clut8_sections() {
		writeMap("blend=auto", "[render:rgb888]\nblend=off\n");
		useDomain("render_target=rgb888\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
	}

	void test_blend_on_with_only_1bpp_faces_keeps_a_paletted_screen() {
		const Common::Path one = tmpFile("one.svf");
		writeSvf(one, 1);
		writeFile(tmpFile("HIRESTXT.MAP"), Common::String::format(
			"[map]\nversion=2\n[font.0]\nface=%s\n", one.toString('/').c_str()));
		useDomain("hires_text_blend=on\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT(hr.enabled());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
	}

	void test_blending_map_gets_its_rgb_screen() {
		writeMap("blend=auto");
		useDomain("render_target=rgb888\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetRgb888);
		TS_ASSERT(hr.anyCoverage());
		TS_ASSERT(hr.wantsAlpha());
		const Common::List<Graphics::PixelFormat> req = hr.screenRequest(formats());
		TS_ASSERT(!req.empty());
		TS_ASSERT_EQUALS(Graphics::targetOfFormat(req.front()), Graphics::kHiResTargetRgb888);
		TS_ASSERT(req.back().isCLUT8());
	}

	/// v7+ keeps the paletted screen SMUSH sets its palette on.
	void test_v7_request_is_clut8() {
		writeMap("blend=on");
		useDomain("render_target=rgb888\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "dig", 7, Common::EN_ANY, &formats());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
	}

	/// The backend gave a family whose sections do not blend: the engine
	/// must not keep that screen (it would set a palette on it).
	void test_can_draw_into_follows_the_sections_of_that_family() {
		writeMap("blend=auto", "[render:rgb565]\nblend=off\n");
		useDomain("render_target=rgb888\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetRgb888);
		TS_ASSERT(hr.canDrawInto(rgb888()));
		TS_ASSERT(!hr.canDrawInto(rgb565()));
		TS_ASSERT(hr.canDrawInto(Graphics::PixelFormat::createFormatCLUT8()));
	}

	/// The screen and the text surface are sized from scale() before
	/// adoptScreen() runs: a target correction must not change it.
	void test_adopt_screen_keeps_the_scale_the_screen_was_sized_for() {
		writeMap("blend=auto\nscale=2", "[render:rgb888]\nscale=3\n");
		useDomain("render_target=rgb888\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(hr.scale(), 3);
		hr.adoptScreen(Graphics::PixelFormat::createFormatCLUT8(), true);
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(hr.scale(), 3);
		TS_ASSERT(!hr.anyCoverage()); // the clut8 face is the 1 bpp one
	}

	/// A screen the layer did not choose (FM-Towns 16-bit, Hercules, EGA
	/// dithering) still decides the sections.
	void test_adopt_screen_follows_a_screen_the_layer_did_not_choose() {
		writeMap("blend=auto");
		useDomain("");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetRgb888);
		TS_ASSERT(hr.anyCoverage());
		hr.adoptScreen(Graphics::PixelFormat::createFormatCLUT8(), false);
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetClut8);
		TS_ASSERT(!hr.anyCoverage());
	}

	/// With hi-res text off, adoptScreen() only records the screen.
	void test_adopt_screen_does_nothing_when_hires_text_is_off() {
		writeMap("blend=auto");
		useDomain("hires_text=false\n");
		Scumm::ScummHiResText hr;
		hr.loadConfig(tmpDir(), "monkey2", 5, Common::EN_ANY, &formats());
		hr.adoptScreen(rgb565(), false);
		TS_ASSERT_EQUALS(hr.renderTarget(), Graphics::kHiResTargetRgb565);
		TS_ASSERT(!hr.enabled());
		TS_ASSERT(!hr.anyCoverage());
		TS_ASSERT(!hr.wantsAlpha());
	}

	/// The in-game "Smooth the hi-res text" box shows what the running game
	/// does while the setting is the one it started with, and the
	/// best case of the stored setting otherwise (the launcher, or after
	/// the player changed it without restarting).
	void test_blend_checkbox_state() {
		using Scumm::ScummHiResText;
		// Launcher: nothing running.
		TS_ASSERT(ScummHiResText::blendCheckboxState(Graphics::kHiResBlendAuto, false, Graphics::kHiResBlendAuto, false));
		TS_ASSERT(!ScummHiResText::blendCheckboxState(Graphics::kHiResBlendOff, false, Graphics::kHiResBlendAuto, false));
		// In game, the setting unchanged: the effective state.
		TS_ASSERT(!ScummHiResText::blendCheckboxState(Graphics::kHiResBlendAuto, true, Graphics::kHiResBlendAuto, false));
		TS_ASSERT(ScummHiResText::blendCheckboxState(Graphics::kHiResBlendAuto, true, Graphics::kHiResBlendAuto, true));
		// In game, changed since the start: what was stored.
		TS_ASSERT(ScummHiResText::blendCheckboxState(Graphics::kHiResBlendOn, true, Graphics::kHiResBlendAuto, false));
		TS_ASSERT(!ScummHiResText::blendCheckboxState(Graphics::kHiResBlendOff, true, Graphics::kHiResBlendAuto, true));
	}
};
