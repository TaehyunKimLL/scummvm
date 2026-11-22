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

#ifndef SCI_KOREAN_INPUT_H
#define SCI_KOREAN_INPUT_H

#include "common/hangul.h"
#include "common/str.h"

namespace Sci {

/**
 * The text of a SCI edit control (the parser prompt) while a Korean
 * (TextMode::kUtf8) game runs: UTF-8 bytes, a cursor that is a byte offset
 * on a code point boundary, Hangul composed by the built-in two-set
 * composer (DESIGN.md section 6) and finished syllables of an OS input
 * method. Knows nothing of the engine: GfxControls16 hands it the keys.
 *
 * The syllable being composed is written into the text as it grows (the
 * pre-edit is drawn as text), from runStart() to the cursor.
 */
class KoreanEditLine {
public:
	/** SCI key codes the line handles (engines/sci/event.h kSciKey*). */
	enum {
		kKeyBackspace = 8,
		kKeyEtx = 3,
		kKeyHome = 0x4700,
		kKeyUp = 0x4800,
		kKeyLeft = 0x4B00,
		kKeyRight = 0x4D00,
		kKeyEnd = 0x4F00,
		kKeyDelete = 0x5300
	};
	/** The key the event manager gives a script for an OS-IME syllable (SCI0 opens the prompt only on ASCII). */
	enum { kOsImeSurrogateKey = 'x' };

	enum Result {
		kIgnored, ///< not a key of the line
		kChanged, ///< text or cursor changed
		kRefused  ///< the key would overflow the field: nothing changed
	};

	KoreanEditLine() : _runStart(0), _composing(false) {}

	/**
	 * Before a key: a text the line did not write last (another control,
	 * the script changed it) drops the composition. A prompt the script just
	 * opened with its first key in it is taken over: the surrogate 'x' of an
	 * OS-IME syllable @p osImeCp becomes that syllable, and in Hangul mode a
	 * jamo key becomes the first jamo. True when @p text changed.
	 */
	bool adopt(Common::String &text, uint &cursor, bool hangulMode, uint32 osImeCp, uint maxBytes);

	/**
	 * One key. @p ctrl: Control held. @p osImeCp: the syllable behind a
	 * surrogate key (0 for none). @p maxBytes: the field's room.
	 */
	Result key(uint16 key, bool ctrl, bool hangulMode, uint32 osImeCp, Common::String &text, uint &cursor, uint maxBytes);

	/** Undoes the last key that returned kChanged (the field could not show it). */
	void undo(Common::String &text, uint &cursor);

	/** Ends the composition; the syllable stays in the text. */
	void commit() { _composing = false; _hangul.reset(); }
	/** After the control wrote @p text back. */
	void remember(const Common::String &text) { _last = text; }

	bool isComposing() const { return _composing; }
	uint runStart() const { return _runStart; }

	static uint prevBoundary(const Common::String &text, uint pos);
	static uint nextBoundary(const Common::String &text, uint pos);
	static Common::String encode(uint32 cp);

private:
	/** The composer's text in place of [runStart, cursor). */
	bool writeRun(Common::String &text, uint &cursor, uint maxBytes);
	void snapshot(const Common::String &text, uint cursor);

	Common::HangulComposer _hangul;
	uint _runStart;
	bool _composing;
	Common::String _last;

	Common::HangulComposer _undoHangul;
	uint _undoRunStart;
	bool _undoComposing;
	Common::String _undoText;
	uint _undoCursor;
};

} // End of namespace Sci

#endif
