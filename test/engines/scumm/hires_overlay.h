#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_overlay.h"

/**
 * The overlay's whole job is that the two planes cannot drift apart.
 *
 * Every test here is about a pair of planes staying in step - allocated
 * together, cleared together, saved and restored together. The bugs this
 * class exists to prevent were all of the form "someone touched one plane
 * and not the other", and none of them would fail a rendering test until
 * much later, somewhere else.
 */
class HiResOverlayTestSuite : public CxxTest::TestSuite {
	static byte at(const Graphics::Surface &s, int x, int y) {
		return *(const byte *)s.getBasePtr(x, y);
	}

public:
	void test_create_allocates_both_planes_at_one_size() {
		Scumm::HiResOverlay ov;
		ov.create(40, 20, true);

		TS_ASSERT(ov.created());
		TS_ASSERT_EQUALS(ov.index().w, 40);
		TS_ASSERT_EQUALS(ov.index().h, 20);
		TS_ASSERT(ov.coverage() != nullptr);
		TS_ASSERT_EQUALS(ov.coverage()->w, 40);
		TS_ASSERT_EQUALS(ov.coverage()->h, 20);

		ov.free();
		TS_ASSERT(!ov.created());
		TS_ASSERT(ov.coverage() == nullptr);
	}

	/// A stencil-only overlay: the platforms that key text in need no alpha.
	void test_create_without_coverage() {
		Scumm::HiResOverlay ov;
		ov.create(8, 8, false);

		TS_ASSERT(ov.created());
		TS_ASSERT(ov.coverage() == nullptr);

		// Clearing must not trip over the plane that is not there.
		ov.clear(0, 8, 0xFD);
		TS_ASSERT_EQUALS(at(ov.index(), 0, 0), 0xFD);

		ov.free();
	}

	/**
	 * The transparent value belongs to the caller.
	 *
	 * FM-Towns clears to 0 and reads 0 back as transparent; everywhere else
	 * uses CHARSET_MASK_TRANSPARENCY. An overlay that assumed one would
	 * silently blank text on the other.
	 */
	void test_clear_uses_the_key_it_is_given() {
		Scumm::HiResOverlay ov;
		ov.create(4, 4, true);

		ov.clear(0, 4, 0xFD);
		TS_ASSERT_EQUALS(at(ov.index(), 1, 1), 0xFD);

		ov.clear(0, 4, 0);
		TS_ASSERT_EQUALS(at(ov.index(), 1, 1), 0);

		ov.free();
	}

	/// Coverage always goes to zero, whatever the index is cleared to.
	void test_clear_zeroes_coverage_with_the_index() {
		Scumm::HiResOverlay ov;
		ov.create(4, 4, true);

		*(byte *)ov.index().getBasePtr(2, 2) = 7;
		*(byte *)ov.coverage()->getBasePtr(2, 2) = 0x80;

		ov.clear(0, 4, 0xFD);

		TS_ASSERT_EQUALS(at(ov.index(), 2, 2), 0xFD);
		TS_ASSERT_EQUALS(at(*ov.coverage(), 2, 2), 0);

		ov.free();
	}

	/// A band clear leaves the rows outside it alone, in both planes.
	void test_clear_is_scoped_to_its_band() {
		Scumm::HiResOverlay ov;
		ov.create(4, 10, true);

		for (int y = 0; y < 10; ++y) {
			*(byte *)ov.index().getBasePtr(0, y) = 9;
			*(byte *)ov.coverage()->getBasePtr(0, y) = 0xFF;
		}

		ov.clear(4, 3, 0xFD);

		TS_ASSERT_EQUALS(at(ov.index(), 0, 3), 9);
		TS_ASSERT_EQUALS(at(ov.index(), 0, 4), 0xFD);
		TS_ASSERT_EQUALS(at(ov.index(), 0, 6), 0xFD);
		TS_ASSERT_EQUALS(at(ov.index(), 0, 7), 9);

		TS_ASSERT_EQUALS(at(*ov.coverage(), 0, 3), 0xFF);
		TS_ASSERT_EQUALS(at(*ov.coverage(), 0, 4), 0);
		TS_ASSERT_EQUALS(at(*ov.coverage(), 0, 7), 0xFF);

		ov.free();
	}

	/// A band that starts above the surface or runs past it must be clipped.
	void test_clear_clips_rather_than_walking_off() {
		Scumm::HiResOverlay ov;
		ov.create(4, 6, true);

		ov.clear(-3, 5, 0xFD);          // starts above
		TS_ASSERT_EQUALS(at(ov.index(), 0, 0), 0xFD);
		TS_ASSERT_EQUALS(at(ov.index(), 0, 1), 0xFD);

		ov.clear(4, 99, 0xFE);          // runs past the bottom
		TS_ASSERT_EQUALS(at(ov.index(), 0, 5), 0xFE);

		ov.clear(99, 4, 0xFD);          // entirely below: a no-op
		ov.clear(0, -1, 0xFD);          // negative height: a no-op

		ov.free();
	}

	void test_clear_rect_covers_both_planes() {
		Scumm::HiResOverlay ov;
		ov.create(10, 10, true);

		for (int y = 0; y < 10; ++y)
			for (int x = 0; x < 10; ++x) {
				*(byte *)ov.index().getBasePtr(x, y) = 5;
				*(byte *)ov.coverage()->getBasePtr(x, y) = 0xFF;
			}

		ov.clear(Common::Rect(2, 2, 5, 5), 0xFD);

		TS_ASSERT_EQUALS(at(ov.index(), 3, 3), 0xFD);
		TS_ASSERT_EQUALS(at(*ov.coverage(), 3, 3), 0);
		TS_ASSERT_EQUALS(at(ov.index(), 6, 6), 5);
		TS_ASSERT_EQUALS(at(*ov.coverage(), 6, 6), 0xFF);

		ov.free();
	}

	/**
	 * The bug this class was written for.
	 *
	 * The GUI stamps over the overlay and puts it back afterwards. It used to
	 * copy the index plane only, so closing a menu left coverage describing
	 * glyphs that were no longer there - visible as smeared antialiasing
	 * around whatever was drawn next.
	 */
	void test_save_and_restore_carry_both_planes() {
		Scumm::HiResOverlay ov;
		ov.create(4, 4, true);

		*(byte *)ov.index().getBasePtr(1, 1) = 3;
		*(byte *)ov.coverage()->getBasePtr(1, 1) = 0x40;

		ov.saveState();

		// The GUI scribbles over both.
		ov.clear(0, 4, 0xFD);
		TS_ASSERT_EQUALS(at(ov.index(), 1, 1), 0xFD);
		TS_ASSERT_EQUALS(at(*ov.coverage(), 1, 1), 0);

		ov.restoreState();

		TS_ASSERT_EQUALS(at(ov.index(), 1, 1), 3);
		TS_ASSERT_EQUALS(at(*ov.coverage(), 1, 1), 0x40);

		ov.free();
	}

	/// Restoring without a saved state must not corrupt what is there.
	void test_restore_without_save_is_a_no_op() {
		Scumm::HiResOverlay ov;
		ov.create(4, 4, true);

		*(byte *)ov.index().getBasePtr(0, 0) = 11;
		ov.restoreState();
		TS_ASSERT_EQUALS(at(ov.index(), 0, 0), 11);

		ov.free();
	}

	/// A dropped state is gone: a later restore must not resurrect it.
	void test_dropped_state_is_not_restored() {
		Scumm::HiResOverlay ov;
		ov.create(4, 4, true);

		*(byte *)ov.index().getBasePtr(0, 0) = 11;
		ov.saveState();
		*(byte *)ov.index().getBasePtr(0, 0) = 22;
		ov.dropState();
		ov.restoreState();

		TS_ASSERT_EQUALS(at(ov.index(), 0, 0), 22);

		ov.free();
	}

	/// Freeing releases any saved state too, rather than leaking it.
	void test_free_discards_a_saved_state() {
		Scumm::HiResOverlay ov;
		ov.create(4, 4, true);
		ov.saveState();
		ov.free();

		ov.create(4, 4, true);
		*(byte *)ov.index().getBasePtr(0, 0) = 42;
		ov.restoreState();
		TS_ASSERT_EQUALS(at(ov.index(), 0, 0), 42);

		ov.free();
	}

	/// Re-creating at a new size must not leave the old plane behind.
	void test_create_replaces_an_existing_overlay() {
		Scumm::HiResOverlay ov;
		ov.create(4, 4, true);
		ov.create(8, 6, false);

		TS_ASSERT_EQUALS(ov.index().w, 8);
		TS_ASSERT_EQUALS(ov.index().h, 6);
		TS_ASSERT(ov.coverage() == nullptr);

		ov.free();
	}

	/// free() on an untouched overlay must be safe.
	void test_free_without_create() {
		Scumm::HiResOverlay ov;
		ov.free();
		TS_ASSERT(!ov.created());
	}
};
