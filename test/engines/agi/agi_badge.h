#include <cxxtest/TestSuite.h>

#include "../../system/null_osystem.h"
#include "agi/hangulline.h"

// D10: the badge follows the Han/Yeong mode while Korean typing is on, and an
// English session (no Korean typing) never puts one up.
//
// The decision is tested through koreanBadgeFor(): the test OSystem has no
// graphics manager (NULL_DRIVER_USE_FOR_TEST), so KoreanInputIndicator::show()
// - g_system->displayActivityIconOnOSD() - cannot run here. That show()
// re-posts only on a change is pinned by SciKoreanIndicatorTestSuite, and
// the badge on screen by the harness (agistate "badge").
class AgiBadgeTestSuite : public CxxTest::TestSuite {
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

	void test_english_session_draws_nothing() {
		TS_ASSERT_EQUALS(Agi::koreanBadgeFor(false, true), Agi::kKoreanBadgeNone);
		TS_ASSERT_EQUALS(Agi::koreanBadgeFor(false, false), Agi::kKoreanBadgeNone);
		// Nothing is up before anything is shown, and clearing it then
		// does not reach the backend.
		Graphics::KoreanInputIndicator b;
		TS_ASSERT(!b.isShown());
		Agi::updateKoreanBadge(b, false, true);
		TS_ASSERT(!b.isShown());
	}

	void test_badge_follows_the_toggle() {
		TS_ASSERT_EQUALS(Agi::koreanBadgeFor(true, true), Agi::kKoreanBadgeKorean);
		TS_ASSERT_EQUALS(Agi::koreanBadgeFor(true, false), Agi::kKoreanBadgeEnglish);
	}

	void test_the_two_badges_differ_and_match_in_size() {
		Graphics::Surface ko = Graphics::KoreanInputIndicator::renderBadge(true);
		Graphics::Surface en = Graphics::KoreanInputIndicator::renderBadge(false);
		TS_ASSERT_EQUALS(ko.w, en.w);
		TS_ASSERT_EQUALS(ko.h, en.h);
		TS_ASSERT_DIFFERS(ko.getPixel(ko.w / 2, 2), en.getPixel(en.w / 2, 2));
		ko.free();
		en.free();
	}
};
