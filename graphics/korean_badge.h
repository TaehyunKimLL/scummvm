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

#ifndef GRAPHICS_KOREAN_BADGE_H
#define GRAPHICS_KOREAN_BADGE_H

#include "common/scummsys.h"
#include "graphics/surface.h"

namespace Graphics {

/**
 * The on-screen badge that says which input mode the player is in.
 *
 * Why the badge is a BACKEND overlay and not something drawn into the game's
 * picture, measured rather than assumed (harness/s5probe.sh, s5probe2.sh):
 *
 * - The edit control is entered about 1,550 times in a 40-second session -
 *   it is the game's idle poll - but it REDRAWS only when the text changed:
 *   twice in that same session. A bare Han/Yeong press produced zero redraws.
 *   So a marker inside the control is invisible on a toggle unless the toggle
 *   forces a redraw of its own, and forcing one means issuing a screen update
 *   the driver did not ask for. That is the defect class S4 already paid for
 *   once (de7a0137b0d, where the control's own update erased the glyphs it
 *   had just drawn).
 * - `displayActivityIconOnOSD()` is composited by the graphics manager after
 *   the game surface, so it cannot corrupt what SCI drew, it survives being
 *   ignored (BaseBackend's default does nothing), and unlike the OSD MESSAGE
 *   path it does not fade: measured present at +2s and still present at +14s
 *   with no input, and gone on the toggle back.
 *
 * The label is ASCII. The OSD font is the GUI theme font, which in a build
 * without FreeType has no Hangul: posting 한글 through it drew two empty
 * boxes while the ASCII on the next line rendered fine (s5probe3.sh). A badge
 * that reads as boxes is not an indicator, so it says KO and EN.
 */
class KoreanInputIndicator {
public:
	KoreanInputIndicator() : _shown(false), _shownEnabled(false) {}
	~KoreanInputIndicator() { clear(); }

	/**
	 * Put the badge for @p hangulEnabled on screen.
	 *
	 * Idempotent, and that is load-bearing rather than tidy: the caller is
	 * the event pump, so this runs at poll rate and must not rebuild a
	 * surface or re-upload a texture unless the state actually changed.
	 */
	void show(bool hangulEnabled);

	/** Take the badge off screen. Safe when nothing is shown. */
	void clear();

	/** A badge is on screen; shownEnabled() says which. For tests and the harness. */
	bool isShown() const { return _shown; }
	bool shownEnabled() const { return _shownEnabled; }

	/**
	 * The badge surface for one state, ARGB32 with an opaque body.
	 *
	 * Separate and static so it can be measured without a backend: what a
	 * test can assert is that the two states differ, that they are the same
	 * size (a badge that changes size moves the eye), and that each carries
	 * its documented body colour - which is also what the capture harness
	 * counts pixels of, so the test and the harness agree on one number.
	 *
	 * The caller owns the returned surface and must free() it.
	 */
	static Graphics::Surface renderBadge(bool hangulEnabled);

	/**
	 * The body colours, as (r, g, b). Korean is the deliberately loud one:
	 * the state a player forgets they are in is Korean, because every key
	 * then produces something other than what the keycap says.
	 */
	enum {
		kKoreanR = 0xC8, kKoreanG = 0x28, kKoreanB = 0x28,
		kEnglishR = 0x28, kEnglishG = 0x28, kEnglishB = 0x28
	};

private:
	bool _shown;
	bool _shownEnabled;
};

} // End of namespace Graphics

#endif // GRAPHICS_KOREAN_BADGE_H
