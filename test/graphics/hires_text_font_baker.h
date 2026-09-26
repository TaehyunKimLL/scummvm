#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_baker.h"
#include "graphics/hires_text/font_map.h"

/**
 * Tests for the code list the hi-res font baker is handed.
 *
 * applyGlyphOverrides() decides which codes a baked font carries once the
 * map's [glyphs] table is applied: 'keep' codes leave, remap targets join.
 * The baker needs FreeType; without it the suite is empty.
 */
class HiResFontBakerTestSuite : public CxxTest::TestSuite {
#ifdef USE_FREETYPE2
private:
	typedef Common::HashMap<uint32, Graphics::HiResGlyphOverride> Overrides;

	/**
	 * applyGlyphOverrides() as it was written before remap targets were
	 * looked up by hash: the same walk over the table, with a linear search
	 * of the list for each target. The result must not have changed.
	 */
	static void referenceApply(const Overrides &overrides, Common::Array<uint32> &inOut) {
		if (overrides.empty())
			return;
		Common::Array<uint32> kept;
		for (uint i = 0; i < inOut.size(); ++i) {
			Overrides::const_iterator it = overrides.find(inOut[i]);
			if (it != overrides.end() && it->_value.action == Graphics::kHiResGlyphKeep)
				continue;
			kept.push_back(inOut[i]);
		}
		for (Overrides::const_iterator it = overrides.begin(); it != overrides.end(); ++it) {
			if (it->_value.action != Graphics::kHiResGlyphRemap)
				continue;
			const uint32 target = it->_value.codepoint;
			bool present = false;
			for (uint i = 0; i < kept.size() && !present; ++i)
				present = (kept[i] == target);
			if (!present)
				kept.push_back(target);
		}
		inOut = kept;
	}

	static uint countOf(const Common::Array<uint32> &list, uint32 code) {
		uint n = 0;
		for (uint i = 0; i < list.size(); ++i)
			if (list[i] == code)
				++n;
		return n;
	}

	static bool parse(const char *text, Graphics::HiResTextConfig &out) {
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		return Graphics::HiResFontMap::loadFromStream(
			stream, Common::Path("/games/demo"), qualifiers, out);
	}
#endif

public:
	void test_keep_drops_codes() {
#ifdef USE_FREETYPE2
		Overrides overrides;
		overrides[0x42] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphKeep, 0);
		overrides[0x41] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x3000);

		Common::Array<uint32> list;
		list.push_back(0x41);
		list.push_back(0x42);
		list.push_back(0x43);
		list.push_back(0x61);
		Graphics::HiResFontBaker::applyGlyphOverrides(overrides, list);

		// Hand-written: the list keeps its order, 0x42 leaves, and the one
		// remap target joins at the end.
		TS_ASSERT_EQUALS(list.size(), 4u);
		TS_ASSERT_EQUALS(list[0], 0x41u);
		TS_ASSERT_EQUALS(list[1], 0x43u);
		TS_ASSERT_EQUALS(list[2], 0x61u);
		TS_ASSERT_EQUALS(list[3], 0x3000u);
#endif
	}

	void test_remap_target_appended_once() {
#ifdef USE_FREETYPE2
		Overrides overrides;
		overrides[0x41] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x3000);
		overrides[0x42] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x3000);
		// A target that is already in the list is not added again.
		overrides[0x43] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x61);

		Common::Array<uint32> list;
		list.push_back(0x41);
		list.push_back(0x42);
		list.push_back(0x43);
		list.push_back(0x61);
		Graphics::HiResFontBaker::applyGlyphOverrides(overrides, list);

		TS_ASSERT_EQUALS(list.size(), 5u);
		TS_ASSERT_EQUALS(list[0], 0x41u);
		TS_ASSERT_EQUALS(list[1], 0x42u);
		TS_ASSERT_EQUALS(list[2], 0x43u);
		TS_ASSERT_EQUALS(list[3], 0x61u);
		TS_ASSERT_EQUALS(list[4], 0x3000u);
		TS_ASSERT_EQUALS(countOf(list, 0x3000), 1u);
		TS_ASSERT_EQUALS(countOf(list, 0x61), 1u);
#endif
	}

	void test_empty_table_leaves_list_alone() {
#ifdef USE_FREETYPE2
		Overrides overrides;
		Common::Array<uint32> list;
		Graphics::HiResFontBaker::latin1(list);
		const Common::Array<uint32> before = list;
		Graphics::HiResFontBaker::applyGlyphOverrides(overrides, list);
		TS_ASSERT(list == before);
#endif
	}

	void test_range_expanded_table() {
#ifdef USE_FREETYPE2
		Graphics::HiResTextConfig config;
		TS_ASSERT(parse(
			"[glyphs]\n"
			"0x21-0x7E=+0xFEE0\n"
			"0x5e=keep\n", config));
		TS_ASSERT_EQUALS(config.glyphOverrides.size(), 94u);

		Common::Array<uint32> list;
		Graphics::HiResFontBaker::latin1(list);
		Common::Array<uint32> expected = list;
		referenceApply(config.glyphOverrides, expected);

		Graphics::HiResFontBaker::applyGlyphOverrides(config.glyphOverrides, list);

		TS_ASSERT_EQUALS(countOf(list, 0x5E), 0u);
		for (uint32 code = 0x21; code <= 0x7E; ++code) {
			const uint32 target = code + 0xFEE0;
			TS_ASSERT_EQUALS(countOf(list, target), (code == 0x5E) ? 0u : 1u);
		}
		// latin1() is 191 codes; 0x5E leaves and 93 fullwidth targets join.
		TS_ASSERT_EQUALS(list.size(), 191u - 1u + 93u);
		TS_ASSERT(list == expected);
#endif
	}
};
