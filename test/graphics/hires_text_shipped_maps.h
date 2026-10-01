#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

#include "../system/null_osystem.h"

class HiResShippedMapsTestSuite : public CxxTest::TestSuite {
	// test/graphics/<this file> -> the source tree
	static Common::FSNode tree() {
		return Common::FSNode(Common::Path(__FILE__, '/').getParent().getParent().getParent());
	}

	static const Graphics::HiResEngineKeys &keysFor(const Common::String &name) {
		// DOS maps: M1*/M2* are SCUMM, the rest SCI. Shared maps: named per engine below.
		if (name.hasPrefixIgnoreCase("M1") || name.hasPrefixIgnoreCase("M2") || name.hasPrefix("mi1-") ||
			name.hasPrefix("scumm-") || name.hasPrefix("ft-") || name == "korean-default.map")
			return Graphics::kHiResKeysScumm;
		return Graphics::kHiResKeysSci;
	}

	void checkDir(const char *rel, const char *suffix) {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode dir = tree().getChild("dists").getChild("engine-data").getChild("hires_text").getChild(rel);
		if (!dir.isDirectory()) {
			TS_WARN(Common::String::format("%s not found; skipped", rel).c_str());
			return;
		}
		Common::FSList files;
		TS_ASSERT(dir.getChildren(files, Common::FSNode::kListFilesOnly));
		int n = 0;
		for (uint i = 0; i < files.size(); ++i) {
			const Common::String name = files[i].getName();
			if (!name.hasSuffixIgnoreCase(suffix))
				continue;
			++n;
			Common::Array<Common::String> q;
			const Graphics::HiResRenderTarget targets[] = { Graphics::kHiResTargetAuto, Graphics::kHiResTargetClut8,
															Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb888 };
			for (uint t = 0; t < ARRAYSIZE(targets); ++t) {
				Graphics::HiResMap m;
				Graphics::HiResMapLoadOptions options;
				options.target = targets[t];
				options.quiet = true;
				TSM_ASSERT(name.c_str(), Graphics::HiResFontMap::loadMapFile(files[i].getPath(), q, keysFor(name), m, options));
				for (uint w = 0; w < m.warnings.size(); ++w)
					TS_FAIL((name + " (" + Graphics::renderTargetName(targets[t]) + "): " + m.warnings[w]).c_str());
			}
		}
		TS_ASSERT(n > 0);
#endif
	}

public:
	// Common::FSNode needs an OSystem for its filesystem factory.
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

	void test_dos_maps_are_version_2_and_clean() { checkDir("dos", ".MAP"); }

	// One DOS map per game: the per-preset files are gone.
	void test_dos_maps_are_one_per_game() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode dir = tree().getChild("dists").getChild("engine-data").getChild("hires_text").getChild("dos");
		Common::FSList files;
		TS_ASSERT(dir.getChildren(files, Common::FSNode::kListFilesOnly));
		for (uint i = 0; i < files.size(); ++i) {
			const Common::String name = files[i].getName();
			TSM_ASSERT(name.c_str(), !name.hasSuffixIgnoreCase("KOU.MAP") && !name.hasSuffixIgnoreCase("KOL.MAP"));
		}
#endif
	}
	void test_shared_maps_are_version_2_and_clean() { checkDir("maps", ".map"); }
};
