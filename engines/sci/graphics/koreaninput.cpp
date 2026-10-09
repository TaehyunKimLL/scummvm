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
#include "common/str-enc.h"
#include "common/system.h"
#include "common/ustr.h"

#include "graphics/font.h"
#include "graphics/fontman.h"

#include "sci/graphics/dbcs.h"
#include "sci/graphics/koreaninput.h"
#include "sci/sci.h"

namespace Sci {

bool KoreanComposer::isDrawable(uint32 codePoint) {
	if (codePoint < 0x80)
		return true;

	Common::U32String one;
	one += codePoint;
	Common::String encoded = Common::convertFromU32String(one, Common::kWindows949);

	// Not encodable at all: convertFromU32String substitutes an error
	// character, which is one byte, so anything that did not come back as a
	// pair is not a Korean character this font could hold.
	if (encoded.size() != 2)
		return false;

	// Encodable is not drawable. kWindows949 is UHC and encodes all 11172
	// modern syllables; FontKoreanWansung::getCharData() indexes
	// ((ch % 256) - 0xb0) * 94 + (ch / 256) - 0xa1, i.e. lead 0xB0..0xC8 and
	// trail 0xA1..0xFE - 2350 of them. The other 8822 encode cleanly and then
	// index outside the glyph table. The compatibility jamo a half-composed
	// syllable is shown as (U+3131..U+3163) encode too, into the 0xA4 row,
	// and are likewise undrawable.
	const byte lead = (byte)encoded[0];
	const byte trail = (byte)encoded[1];
	return lead >= 0xB0 && lead <= 0xC8 && trail >= 0xA1 && trail <= 0xFE;
}

Common::String KoreanComposer::encode(uint32 codePoint) {
	if (!isDrawable(codePoint))
		return Common::String();

	if (codePoint < 0x80)
		return Common::String((char)codePoint);

	Common::U32String one;
	one += codePoint;
	return Common::convertFromU32String(one, Common::kWindows949);
}

Common::String KoreanComposer::encodedRun(bool &committedOk) const {
	Common::U32String composed = _hangul.text();
	const uint32 pending = _hangul.preedit();
	Common::String out;

	committedOk = true;

	for (uint i = 0; i < composed.size(); ++i) {
		const bool isPending = (pending != 0 && i + 1 == composed.size());
		Common::String encoded = encode(composed[i]);

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
		!isDrawable(_hangul.preedit())) {
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

// --------------------------------------------------------------------------
// The badge
// --------------------------------------------------------------------------

Graphics::Surface KoreanInputIndicator::renderBadge(bool hangulEnabled) {
	// Sized so the label fits the smallest GUI font with a margin, and kept
	// the SAME size for both states: a badge that resizes when the mode
	// changes drags the eye to the edge that moved rather than to the label.
	const int16 width = 34;
	const int16 height = 20;

	Graphics::Surface badge;
	badge.create(width, height, Graphics::PixelFormat::createFormatARGB32());

	const uint32 body = hangulEnabled
		? badge.format.ARGBToColor(0xFF, kKoreanR, kKoreanG, kKoreanB)
		: badge.format.ARGBToColor(0xFF, kEnglishR, kEnglishG, kEnglishB);
	const uint32 border = badge.format.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF);

	badge.fillRect(Common::Rect(0, 0, width, height), border);
	badge.fillRect(Common::Rect(1, 1, width - 1, height - 1), body);

	// ASCII, deliberately: the OSD draws with the GUI theme font, and a build
	// without a scalable font has no Hangul in it - a probe posting 한글 to
	// this same path drew two empty boxes while the ASCII beside it rendered
	// (harness/s5probe3.sh). "KO"/"EN" is legible in every build.
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (font) {
		const Common::String label = hangulEnabled ? "KO" : "EN";
		const int y = (height - font->getFontHeight()) / 2;
		font->drawString(&badge, label, 0, y, width,
		                 badge.format.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF),
		                 Graphics::kTextAlignCenter);
	}

	return badge;
}

void KoreanInputIndicator::show(bool hangulEnabled) {
	// The caller is the event pump, so this is reached at poll rate - about
	// 1,550 times in a 40-second session, measured. Rebuilding the surface
	// each time would re-upload a texture per poll for no visible change.
	if (_shown && _shownEnabled == hangulEnabled)
		return;

	Graphics::Surface badge = renderBadge(hangulEnabled);
	g_system->displayActivityIconOnOSD(&badge);
	badge.free();

	_shown = true;
	_shownEnabled = hangulEnabled;
}

void KoreanInputIndicator::clear() {
	if (!_shown)
		return;

	g_system->displayActivityIconOnOSD(nullptr);
	_shown = false;
}

} // End of namespace Sci
