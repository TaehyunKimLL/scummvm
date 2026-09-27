/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include <cxxtest/TestSuite.h>

#include "graphics/pixelformat.h"
#include "graphics/surface.h"

#include "ags/shared/font/text_twin.h"

namespace {

const Graphics::PixelFormat kTwinArgb(4, 8, 8, 8, 8, 16, 8, 0, 24);
const Graphics::PixelFormat kTwin565(2, 5, 6, 5, 0, 11, 5, 0, 0);

uint16 &at16(Graphics::Surface &s, int x, int y) { return *(uint16 *)s.getBasePtr(x, y); }
uint32 &at32(Graphics::Surface &s, int x, int y) { return *(uint32 *)s.getBasePtr(x, y); }

/** A 16-bit bitmap filled with c. */
void make16(Graphics::Surface &s, int w, int h, uint16 c) {
	s.create(w, h, kTwin565);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			at16(s, x, y) = c;
}

/** "Draw text": fill r with c, the capture around it. */
bool drawCaptured(AGS3::TextCapture &cap, Graphics::Surface &s, const Common::Rect &r, uint16 c, int id) {
	cap.beginDraw(s, Common::Rect(0, 0, s.w, s.h));
	for (int y = r.top; y < r.bottom; y++)
		for (int x = r.left; x < r.right; x++)
			at16(s, x, y) = c;
	AGS3::TextDraw d;
	d.font = id;
	d.x = r.left;
	d.y = r.top;
	return cap.endDraw(s, d);
}

bool sameSurface(const Graphics::Surface &a, const Graphics::Surface &b) {
	if (a.w != b.w || a.h != b.h || a.format != b.format)
		return false;
	for (int y = 0; y < a.h; y++)
		if (memcmp(a.getBasePtr(0, y), b.getBasePtr(0, y), a.w * a.format.bytesPerPixel))
			return false;
	return true;
}

} // End of anonymous namespace

/**
 * C23 T4: which text a bitmap still shows as it was drawn, and the
 * bitmap without it (AGS_HIRES_TEXT_DESIGN.md section 4.1, 4.2).
 */
class AgsTextTwinTestSuite : public CxxTest::TestSuite {
public:
	void test_record_is_the_changed_box_grown_by_one() {
		Graphics::Surface s;
		make16(s, 20, 10, 0x1111);
		AGS3::TextCapture cap;
		TS_ASSERT(drawCaptured(cap, s, Common::Rect(5, 3, 9, 6), 0xFFFF, 0));
		TS_ASSERT_EQUALS(cap.records().size(), 1u);
		TS_ASSERT_EQUALS(cap.records()[0].rect, Common::Rect(4, 2, 10, 7));
		// clipped to the bitmap
		TS_ASSERT(drawCaptured(cap, s, Common::Rect(0, 0, 2, 1), 0xAAAA, 1));
		TS_ASSERT_EQUALS(cap.records()[1].rect, Common::Rect(0, 0, 3, 2));
		s.free();
	}

	void test_nothing_changed_is_no_record() {
		Graphics::Surface s;
		make16(s, 8, 8, 0x1111);
		AGS3::TextCapture cap;
		TS_ASSERT(!drawCaptured(cap, s, Common::Rect(1, 1, 3, 3), 0x1111, 0));
		TS_ASSERT_EQUALS(cap.records().size(), 0u);
		s.free();
	}

	void test_finish_restores_the_exact_text_free_picture() {
		Graphics::Surface s, original, textFree;
		make16(s, 24, 12, 0x1111);
		at16(s, 6, 4) = 0x2222;     // background detail under the first text
		original.copyFrom(s);
		AGS3::TextCapture cap;
		drawCaptured(cap, s, Common::Rect(5, 3, 9, 6), 0xFFFF, 0);
		drawCaptured(cap, s, Common::Rect(7, 5, 12, 8), 0xF800, 1);   // overlaps the first
		textFree.copyFrom(s);
		TS_ASSERT_EQUALS(cap.finish(textFree), 2u);
		TS_ASSERT(cap.records()[0].valid);
		TS_ASSERT(cap.records()[1].valid);
		TS_ASSERT(sameSurface(textFree, original));
		s.free();
		original.free();
		textFree.free();
	}

	void test_overdrawn_entry_and_what_it_covers_are_invalid() {
		// Text 0, then text 1 touching it, then text 2 apart; then a
		// control is drawn over text 1. 1 and 0 (which 1 covered) stay
		// native; 2 is still valid.
		Graphics::Surface s, textFree;
		make16(s, 40, 12, 0x1111);
		AGS3::TextCapture cap;
		drawCaptured(cap, s, Common::Rect(2, 2, 6, 5), 0xFFFF, 0);
		drawCaptured(cap, s, Common::Rect(6, 4, 10, 7), 0xFFFF, 1);
		drawCaptured(cap, s, Common::Rect(25, 2, 30, 5), 0xFFFF, 2);
		at16(s, 8, 5) = 0x07E0;    // not captured: a stipple over text 1
		textFree.copyFrom(s);
		TS_ASSERT_EQUALS(cap.finish(textFree), 1u);
		TS_ASSERT(!cap.records()[0].valid);
		TS_ASSERT(!cap.records()[1].valid);
		TS_ASSERT(cap.records()[2].valid);
		TS_ASSERT_EQUALS(at16(textFree, 3, 3), 0xFFFF);  // native text kept
		TS_ASSERT_EQUALS(at16(textFree, 8, 5), 0x07E0);
		TS_ASSERT_EQUALS(at16(textFree, 26, 3), 0x1111); // text 2 removed
		s.free();
		textFree.free();
	}

	void test_overdraw_in_the_margin_invalidates() {
		// The grown pixel counts: N x ink may reach it
		Graphics::Surface s, textFree;
		make16(s, 20, 10, 0x1111);
		AGS3::TextCapture cap;
		drawCaptured(cap, s, Common::Rect(5, 3, 9, 6), 0xFFFF, 0);
		at16(s, 9, 6) = 0x07E0;    // diagonal neighbour of the ink
		textFree.copyFrom(s);
		TS_ASSERT_EQUALS(cap.finish(textFree), 0u);
		s.free();
		textFree.free();
	}

	void test_band_limits_what_is_seen() {
		// Only the band is compared: a change outside it is not this record's
		Graphics::Surface s;
		make16(s, 20, 20, 0x1111);
		AGS3::TextCapture cap;
		cap.beginDraw(s, Common::Rect(0, 5, 20, 10));
		at16(s, 3, 7) = 0xFFFF;
		at16(s, 3, 15) = 0xFFFF;   // outside the band
		AGS3::TextDraw d;
		TS_ASSERT(cap.endDraw(s, d));
		TS_ASSERT_EQUALS(cap.records()[0].rect, Common::Rect(2, 6, 5, 9));
		s.free();
	}

	void test_derive_region_moves_records_inside_it() {
		Graphics::Surface s, sub, textFree;
		make16(s, 40, 20, 0x1111);
		AGS3::TextCapture cap, out;
		drawCaptured(cap, s, Common::Rect(12, 6, 16, 9), 0xFFFF, 0);   // inside the area
		drawCaptured(cap, s, Common::Rect(2, 2, 5, 4), 0xFFFF, 1);     // outside
		drawCaptured(cap, s, Common::Rect(28, 12, 32, 14), 0xFFFF, 2);  // crosses its edge
		cap.deriveRegion(Common::Rect(10, 4, 30, 16), out);
		TS_ASSERT_EQUALS(out.records().size(), 1u);
		TS_ASSERT_EQUALS(out.records()[0].draw.font, 0);
		TS_ASSERT_EQUALS(out.records()[0].rect, Common::Rect(1, 1, 7, 6));
		TS_ASSERT_EQUALS(out.records()[0].draw.x, 2);
		TS_ASSERT_EQUALS(out.records()[0].draw.y, 2);
		// a copy of the area validates the moved record
		sub.create(20, 12, kTwin565);
		sub.copyRectToSurface(s, 0, 0, Common::Rect(10, 4, 30, 16));
		textFree.copyFrom(sub);
		TS_ASSERT_EQUALS(out.finish(textFree), 1u);
		TS_ASSERT_EQUALS(at16(textFree, 3, 3), 0x1111);
		s.free();
		sub.free();
		textFree.free();
	}

	void test_size_mismatch_is_all_invalid() {
		Graphics::Surface s, other;
		make16(s, 20, 10, 0x1111);
		AGS3::TextCapture cap;
		drawCaptured(cap, s, Common::Rect(5, 3, 9, 6), 0xFFFF, 0);
		make16(other, 21, 10, 0x1111);
		TS_ASSERT_EQUALS(cap.finish(other), 0u);
		s.free();
		other.free();
	}

	void test_upscale_to_argb_from_565() {
		Graphics::Surface s, t;
		make16(s, 2, 1, 0xF81F);   // mask colour
		at16(s, 1, 0) = kTwin565.RGBToColor(255, 0, 0);
		t.create(4, 2, kTwinArgb);
		AGS3::TextTwin::upscaleToArgb(s, false, t, 2);
		TS_ASSERT_EQUALS(at32(t, 0, 0), AGS3::TextTwin::kTransparent);
		TS_ASSERT_EQUALS(at32(t, 1, 1), AGS3::TextTwin::kTransparent);
		TS_ASSERT_EQUALS(at32(t, 2, 0), 0xFFFF0000u);
		TS_ASSERT_EQUALS(at32(t, 3, 1), 0xFFFF0000u);
		s.free();
		t.free();
	}

	void test_upscale_to_argb_alpha_rules() {
		Graphics::Surface s, t;
		s.create(3, 1, kTwinArgb);
		at32(s, 0, 0) = 0x00123456;   // keyed bitmap: junk alpha, an opaque pixel
		at32(s, 1, 0) = 0x80FFFFFF;
		at32(s, 2, 0) = 0xFFFF00FF;   // mask RGB, any alpha
		t.create(3, 1, kTwinArgb);
		AGS3::TextTwin::upscaleToArgb(s, false, t, 1);
		TS_ASSERT_EQUALS(at32(t, 0, 0), 0xFF123456u);
		TS_ASSERT_EQUALS(at32(t, 1, 0), 0xFFFFFFFFu);
		TS_ASSERT_EQUALS(at32(t, 2, 0), AGS3::TextTwin::kTransparent);
		// an alpha bitmap keeps its alpha; alpha 0 is transparent
		AGS3::TextTwin::upscaleToArgb(s, true, t, 1);
		TS_ASSERT_EQUALS(at32(t, 0, 0), AGS3::TextTwin::kTransparent);
		TS_ASSERT_EQUALS(at32(t, 1, 0), 0x80FFFFFFu);
		s.free();
		t.free();
	}

	void test_colour_to_argb() {
		TS_ASSERT_EQUALS(AGS3::TextTwin::toArgb(kTwin565.RGBToColor(255, 255, 255), kTwin565), 0xFFFFFFFFu);
		TS_ASSERT_EQUALS(AGS3::TextTwin::toArgb(0x00102030, kTwinArgb), 0xFF102030u);
	}
};
