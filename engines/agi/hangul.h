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

#ifndef AGI_HANGUL_H
#define AGI_HANGUL_H

#include "common/str.h"
#include "common/ustr.h"

namespace Agi {

/**
 * Two-beolsik Hangul input composer.
 *
 * ScummVM's event layer only reports Common::EVENT_KEYDOWN with an ASCII
 * value; there is no EVENT_TEXTINPUT and no IME composition, so Korean text
 * can never arrive from the operating system's input method. This class does
 * the composition itself: it maps QWERTY keys to jamo and runs the
 * choseong/jungseong/jongseong automaton, emitting precomposed U+AC00
 * syllables.
 *
 * The composer keeps the syllable currently being assembled separate from the
 * committed text so the caller can draw it as a live pre-edit character.
 */
class HangulComposer {
public:
	HangulComposer() { reset(); }

	/** Drop all state, committed and pending. */
	void reset();

	/** True if @p ascii is a key that maps to a jamo. */
	static bool isJamoKey(char ascii);

	/**
	 * Feed one ASCII key.
	 * Non-jamo keys flush the pending syllable and are appended verbatim.
	 * @return true if the key was consumed as Hangul.
	 */
	bool feed(char ascii);

	/** Delete one jamo, then one syllable, mirroring native IME behaviour. */
	void backspace();

	/** Commit the pending syllable, if any. */
	void flush();

	/** Committed text plus the syllable in progress. */
	Common::U32String text() const;

	/** The syllable currently being assembled, or 0 when there is none. */
	uint32 preedit() const { return composeSyllable(); }

	/** True when nothing at all has been typed. */
	bool empty() const { return _out.empty() && !_cho && !_jung && !_jong; }

private:
	uint32 composeSyllable() const;

	Common::U32String _out;
	// indices into the standard jamo tables, 0 means "not set"
	int _cho;   // 1..19
	int _jung;  // 1..21
	int _jong;  // 1..27
};

} // End of namespace Agi

#endif
