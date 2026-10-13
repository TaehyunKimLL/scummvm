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

#include "common/scummsys.h"
#include "common/system.h"
#include "common/ustr.h"

#include "graphics/font.h"
#include "graphics/fontman.h"

#include "sci/graphics/dbcs.h"
#include "sci/graphics/koreaninput.h"
#include "sci/sci.h"
#include "sci/utf8.h"

namespace Sci {

bool KoreanComposer::isDrawable(uint32 codePoint) {
	if (codePoint < 0x80)
		return true;

	// Encodable is not drawable. CP949 (UHC) encodes all 11172 modern
	// syllables, but FontKoreanWansung::getCharData() indexes
	// ((ch % 256) - 0xb0) * 94 + (ch / 256) - 0xa1, i.e. lead 0xB0..0xC8 and
	// trail 0xA1..0xFE - the 2350 KS X 1001 syllables. The other 8822 encode
	// cleanly and then index outside the glyph table. The compatibility jamo
	// a half-composed syllable is shown as (U+3131..U+3163) encode too, into
	// the 0xA4 row, and are likewise undrawable. The pair comes from
	// encodeCodePagePair(), which needs no encoding.dat for these syllables.
	const uint32 packed = encodeCodePagePair(codePoint, Common::kWindows949);
	const byte lead = packed & 0xFF;
	const byte trail = (packed >> 8) & 0xFF;
	return packed != 0 && lead >= 0xB0 && lead <= 0xC8 && trail >= 0xA1 && trail <= 0xFE;
}

Common::String KoreanComposer::encode(uint32 codePoint) {
	if (!isDrawable(codePoint))
		return Common::String();

	if (codePoint < 0x80)
		return Common::String((char)codePoint);

	const uint32 packed = encodeCodePagePair(codePoint, Common::kWindows949);
	Common::String out;
	out += (char)(packed & 0xFF);
	out += (char)((packed >> 8) & 0xFF);
	return out;
}

bool KoreanComposer::canDraw(uint32 codePoint) const {
	return _encoding == kUtf8 ? codePoint != 0 : isDrawable(codePoint);
}

Common::String KoreanComposer::encodeCodePoint(uint32 codePoint) const {
	if (_encoding != kUtf8)
		return encode(codePoint);
	if (!canDraw(codePoint))
		return Common::String();
	byte buf[4];
	const int n = encodeUtf8Char(codePoint, buf);
	return Common::String((const char *)buf, n);
}

Common::String KoreanComposer::encodedRun(bool &committedOk) const {
	Common::U32String composed = _hangul.text();
	const uint32 pending = _hangul.preedit();
	Common::String out;

	committedOk = true;

	for (uint i = 0; i < composed.size(); ++i) {
		const bool isPending = (pending != 0 && i + 1 == composed.size());
		Common::String encoded = encodeCodePoint(composed[i]);

		if (encoded.empty()) {
			if (isPending) {
				// The syllable still being assembled. A half-composed state
				// is a lone compatibility jamo (U+3131..U+3163), which
				// encodes into the 0xA4 row and has no glyph in the
				// 0xB0..0xC8 rows this font indexes - so it is held in the
				// composer and kept OUT of the game's string rather than
				// written there to draw as garbage. Typing the vowel that
				// completes the syllable makes it appear.
				//
				// This is the one visible compromise in this card, and it is
				// deliberate: supplying glyphs for the composing state is
				// what K3 did for AGI and is its own piece of work.
				continue;
			}
			// A COMMITTED character this font cannot draw. Those bytes would
			// stay in the game's string forever, so the key that produced it
			// is refused instead.
			committedOk = false;
			return Common::String();
		}
		out += encoded;
	}
	return out;
}

void KoreanComposer::setEnabled(bool enabled) {
	if (!enabled) {
		// The composed bytes are already in the caller's string. Dropping the
		// composer's state without committing would leave them owned by
		// nobody, and the next backspace would decompose a syllable that is
		// no longer being composed.
		_hangul.flush();
		_hangul.reset();
	}
	_enabled = enabled;
}

bool KoreanComposer::feed(char key, Common::String &text, uint &runStart) {
	if (!_enabled)
		return false;

	// A key that is not a jamo ends the syllable in progress. When that
	// syllable is a lone jamo - which this font cannot draw, so it is being
	// held OUT of the string by encodedRun() - HangulComposer flushes it
	// into its committed buffer, encodedRun() then reports an undrawable
	// COMMITTED character, and the key is refused whole. The refusal
	// restores the composer to still holding the jamo while the caller goes
	// on to insert the ASCII itself, so the next vowel emits the syllable
	// AFTER that ASCII: typing ㄱ 1 ㅏ produced "1가" instead of "가1".
	//
	// The held jamo was never in the string and never on screen, so ending
	// the composition by dropping it changes nothing the player can see,
	// and it keeps what they type in the order they typed it. Everything
	// already committed stays where it is - it is in `text` already, and
	// runStart re-anchors past it.
	if (!Common::HangulComposer::isJamoKey(key) && isComposing() &&
		!canDraw(_hangul.preedit())) {
		debugC(1, kDebugLevelHangul,
		       "[comp] non-jamo '%c' ends an UNDRAWABLE composition "
		       "(preedit=U+%04X) -> jamo dropped, runStart=%u",
		       (key >= 32 && key < 127) ? key : '?',
		       _hangul.preedit(), text.size());
		_hangul.reset();
		runStart = text.size();
		return false;
	}

	// Remember everything, so a refused key changes nothing at all.
	Common::HangulComposer before = _hangul;
	const Common::String textBefore = text;

	_hangul.feed(key);

	bool committedOk = true;
	Common::String run = encodedRun(committedOk);
	if (!committedOk) {
		// The key would have committed a character this font cannot draw.
		debugC(1, kDebugLevelHangul,
		       "[comp] '%c' REFUSED: would commit an undrawable character "
		       "(composer text len=%u) - restoring",
		       (key >= 32 && key < 127) ? key : '?',
		       _hangul.text().size());
		_hangul = before;
		text = textBefore;
		return false;
	}

	if (runStart > text.size()) {
		debugC(1, kDebugLevelHangul,
		       "[comp] runStart %u > text %u - CLAMPED (the run was pointing "
		       "past the end of the string)", runStart, text.size());
		runStart = text.size();
	}
	text = Common::String(text.c_str(), runStart) + run;
	debugC(2, kDebugLevelHangul,
	       "[comp] '%c' -> run %u bytes at %u, preedit=U+%04X, total len=%u",
	       (key >= 32 && key < 127) ? key : '?',
	       run.size(), runStart, _hangul.preedit(), text.size());
	return true;
}

bool KoreanComposer::backspace(Common::String &text, uint &runStart) {
	if (!_enabled || _hangul.empty())
		return false;

	_hangul.backspace();

	if (runStart > text.size())
		runStart = text.size();

	bool committedOk = true;
	text = Common::String(text.c_str(), runStart) + encodedRun(committedOk);

	if (_hangul.empty()) {
		// The composer emptied out; the run is over and the next backspace
		// is an ordinary one.
		runStart = text.size();
	}
	return true;
}

void KoreanComposer::commit(const Common::String &text, uint &runStart) {
	_hangul.flush();
	_hangul.reset();
	runStart = text.size();
}

} // End of namespace Sci
