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

#ifndef AGI_HANGULLINE_H
#define AGI_HANGULLINE_H

#include "common/hangul.h"
#include "graphics/korean_badge.h"

namespace Agi {

/**
 * Common::HangulComposer driving one of AGI's code-point edit lines (the
 * prompt, get.string). While Korean mode is on, ASCII jamo keys build a
 * "run" at the end of the line: cells[runStart..len) is always the
 * composer's committed syllables plus the syllable in progress, the
 * preedit, which the caller draws inverted in the last cell. A non-jamo key,
 * Enter, a mode switch or an IME character ends the run as it stands.
 * (SCI's KoreanComposer does the same on a byte string; this one needs no
 * encoding: the line holds code points.)
 */
class HangulLine {
public:
	HangulLine() : _runStart(0) {}

	void reset() {
		_hangul.reset();
		_runStart = 0;
	}

	bool composing() const { return !_hangul.empty(); }
	uint32 preedit() const { return _hangul.preedit(); }
	int16 runStart() const { return _runStart; }

	/**
	 * One ASCII key. A jamo key rewrites the run and returns true - also
	 * when it is refused because the run would grow past @p maxCells, in
	 * which case nothing changes. Any other key commits the run and returns
	 * false; the caller handles that key itself.
	 */
	bool feed(char key, uint32 *cells, int16 &len, int16 maxCells);

	/** One jamo, then whole syllables of the run; false when not composing. */
	bool backspace(uint32 *cells, int16 &len);

	/** End the run; the next run starts at @p len. */
	void commit(int16 len);

private:
	void writeRun(uint32 *cells, int16 &len) const;

	Common::HangulComposer _hangul;
	int16 _runStart;
};

/** What the KO/EN badge shows. */
enum KoreanBadge {
	kKoreanBadgeNone,
	kKoreanBadgeEnglish,
	kKoreanBadgeKorean
};

/**
 * D10: the KO/EN badge is up exactly while Korean typing is on for the game
 * (agi_hangul_input in UTF-8 text mode); it shows the mode; otherwise
 * nothing is drawn. (The decision, apart from the backend call below.)
 */
KoreanBadge koreanBadgeFor(bool hangulInput, bool hangulMode);

/** Put koreanBadgeFor() on screen. show() is idempotent, so this may run often. */
void updateKoreanBadge(Graphics::KoreanInputIndicator &badge, bool hangulInput, bool hangulMode);

} // End of namespace Agi

#endif
