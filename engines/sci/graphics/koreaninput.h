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

#ifndef SCI_GRAPHICS_KOREANINPUT_H
#define SCI_GRAPHICS_KOREANINPUT_H

#include "common/hangul.h"
#include "common/str.h"
#include "graphics/surface.h"

namespace Sci {

/**
 * The SCI side of Korean text entry: a Hangul composer whose output is
 * EUC-KR bytes rather than code points.
 *
 * The composer in common/ is deliberately encoding-free, and SCI cannot use
 * its code points directly. Unlike AGI, which widened its prompt buffer to
 * uint32 and kept the Korean in engine memory, SCI's edit control writes the
 * line straight back into the game's own heap
 * (`_segMan->strcpy_(textReference, text.c_str())`, controls16.cpp) and draws
 * it from there. What draws it is GfxFontKorean, and
 * FontKoreanWansung::getCharData() indexes by EUC-KR byte pairs. So EUC-KR is
 * what has to be in that string; the intermediate representation is not a
 * free choice.
 *
 * Conversion is Common::convertFromU32String(s, kWindows949), which is exact
 * over every syllable this font can draw - all 2350 of them round-trip with
 * zero errors. What it is NOT is a statement about the font's coverage:
 * kWindows949 is UHC and encodes all 11172 modern syllables, of which 8822
 * encode perfectly and then index outside the font's 25x94 grid. The range
 * check therefore has to be against the FONT, and the converter will not do
 * it - see isDrawable() below.
 */
class KoreanComposer {
public:
	KoreanComposer() : _enabled(false) {}

	/** Is the composer switched on? Off by default; nothing is synthesized. */
	bool isEnabled() const { return _enabled; }

	/**
	 * Switch Korean entry on or off.
	 *
	 * Switching off commits whatever syllable was in progress: the bytes are
	 * already in the game's string and abandoning the composer's state must
	 * not leave them owned by nobody.
	 */
	void setEnabled(bool enabled);

	/** Drop all composer state. The caller's string is not touched. */
	void reset() { _hangul.reset(); }

	/** True while a syllable is being assembled. */
	bool isComposing() const { return _hangul.preedit() != 0; }

	/**
	 * True while the composer owns a run of the caller's string.
	 *
	 * Wider than isComposing(): after a syllable is completed the composer
	 * still holds it, so a backspace must go through the composer rather than
	 * delete one byte of it.
	 */
	bool ownsRun() const { return !_hangul.empty(); }

	/**
	 * Can the bundled Korean font draw this code point?
	 *
	 * False for the half-composed states - a lone jamo is U+3131..U+3163,
	 * which encodes to the 0xA4 row and has no glyph in the 0xB0..0xC8 rows
	 * the font indexes - and false for the 8822 syllables that encode but
	 * fall outside those rows.
	 */
	static bool isDrawable(uint32 codePoint);

	/**
	 * Encode one code point as EUC-KR, or return an empty string if this font
	 * cannot draw it.
	 */
	static Common::String encode(uint32 codePoint);

	/**
	 * Feed one key, and rewrite the composer's run of @p text in place.
	 *
	 * The model mirrors K2's in AGI: @p text holds committed bytes, and the
	 * composer owns everything from @p runStart onwards. After each key that
	 * run is replaced by the composer's current output, so backspace
	 * decomposes correctly with no extra state - the jamo decomposition lives
	 * in the composer for as long as it owns the run.
	 *
	 * A key whose result the font cannot draw is refused whole, composer
	 * state included, so the string and the composer cannot disagree.
	 *
	 * @param key       the ASCII key
	 * @param text      in/out: the edit buffer
	 * @param runStart  in/out: byte offset where the composer's run begins
	 * @return true if the key was consumed as Korean
	 */
	bool feed(char key, Common::String &text, uint &runStart);

	/**
	 * Backspace inside the composer's run: remove one jamo, not one byte.
	 *
	 * @return false when the composer does not own a run, in which case the
	 *         caller should do its own byte-wise backspace.
	 */
	bool backspace(Common::String &text, uint &runStart);

	/**
	 * Commit the syllable in progress and end the composer's ownership of the
	 * run. The bytes stay where they are.
	 */
	void commit(const Common::String &text, uint &runStart);

private:
	/**
	 * The composer's current output, encoded.
	 *
	 * @param committedOk out: false when a COMMITTED character cannot be
	 *        drawn by this font, which is what makes a key refusable. An
	 *        undrawable syllable still being composed is not a refusal - it
	 *        is simply held back, see the comment in the implementation.
	 */
	Common::String encodedRun(bool &committedOk) const;

	bool _enabled;
	Common::HangulComposer _hangul;
};

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

} // End of namespace Sci

#endif // SCI_GRAPHICS_KOREANINPUT_H
