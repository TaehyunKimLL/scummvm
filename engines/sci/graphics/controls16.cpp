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

#include "common/config-manager.h"
#include "common/debug-channels.h"
#include "common/util.h"
#include "common/stack.h"
#include "common/system.h"
#include "common/unicode-bidi.h"
#include "graphics/primitives.h"

#include "sci/sci.h"
#include "sci/utf8.h"
#include "sci/console.h"
#include "sci/event.h"
#include "sci/engine/kernel.h"
#include "sci/engine/state.h"
#include "sci/engine/selector.h"
#include "sci/engine/tts.h"
#include "sci/graphics/compare.h"
#include "sci/graphics/dbcs.h"
#include "sci/graphics/drivers/gfxdriver.h"
#include "sci/graphics/ports.h"
#include "sci/graphics/paint16.h"
#include "sci/graphics/scifont.h"
#include "sci/graphics/screen.h"
#include "sci/graphics/text16.h"
#include "sci/graphics/controls16.h"

namespace Sci {

GfxControls16::GfxControls16(SegManager *segMan, GfxPorts *ports, GfxPaint16 *paint16, GfxText16 *text16, GfxScreen *screen)
	: _segMan(segMan), _ports(ports), _paint16(paint16), _text16(text16), _screen(screen) {
	_texteditBlinkTime = 0;
	_texteditCursorVisible = false;
	_koreanRunStart = 0;
	_koreanRunObject = NULL_REG;

	// Printed once, so an empty log is not ambiguous: it could mean the
	// defect did not reproduce, or that --debugflags=hangul never took
	// effect.
	debugC(1, kDebugLevelHangul,
	       "[init] Hangul debug channel active. Korean entry is %s "
	       "(sci_hangul_input).",
	       (ConfMan.hasKey("sci_hangul_input") &&
	        ConfMan.getBool("sci_hangul_input")) ? "ENABLED" : "OFF");
}

GfxControls16::~GfxControls16() {
}

const char controlListUpArrow[2]	= { 0x18, 0 };
const char controlListDownArrow[2]	= { 0x19, 0 };

Common::String GfxControls16::hangulDump(const Common::String &s) {
	// debugC() is a function: its arguments are built even with the channel off.
	if (!DebugMan.isDebugChannelEnabled(kDebugLevelHangul))
		return Common::String();
	const bool utf8 = g_sci->heapStringsAreUtf8();
	Common::String out = Common::String::format("len=%u [", s.size());
	for (uint i = 0; i < s.size(); ++i) {
		const byte b = (byte)s[i];
		if (b >= 0x80 && !utf8 && i + 1 < s.size()) {
			// An EUC-KR pair, shown as one unit so a lone high byte - which
			// is what a truncated write-back leaves behind - is visible as
			// an unpaired value rather than hiding inside a run of hex.
			out += Common::String::format("%s%02X%02X", i ? " " : "",
			                              b, (byte)s[i + 1]);
			i++;
		} else if (b >= 0x80) {
			out += Common::String::format("%s<%02X%s>", i ? " " : "", b, utf8 ? "" : "!ORPHAN");
		} else if (b >= 0x20 && b < 0x7F) {
			out += Common::String::format("%s'%c'", i ? " " : "", (char)b);
		} else {
			out += Common::String::format("%s<%02X>", i ? " " : "", b);
		}
	}
	out += "]";
	return out;
}

void GfxControls16::drawListControl(Common::Rect rect, reg_t obj, int16 maxChars, int16 count, const Common::String *entries, GuiResourceId fontId, int16 upperPos, int16 cursorPos, bool isAlias) {
	Common::Rect workerRect = rect;
	GuiResourceId oldFontId = _text16->GetFontId();
	int16 oldPenColor = _ports->_curPort->penClr;
	uint16 fontSize = 0;
	int16 i;
	int16 lastYpos;

	// draw basic window
	_paint16->eraseRect(workerRect);
	workerRect.grow(1);
	_paint16->frameRect(workerRect);

	// draw UP/DOWN arrows
	//  we draw UP arrow one pixel lower than sierra did, because it looks nicer. Also the DOWN arrow has one pixel
	//  line inbetween as well
	// They "fixed" this in SQ4 by having the arrow character start one pixel line later, we don't adjust there
	if (g_sci->getGameId() != GID_SQ4)
		workerRect.top++;
	_text16->Box(controlListUpArrow, false, workerRect, SCI_TEXT16_ALIGNMENT_CENTER, 0);
	workerRect.top = workerRect.bottom - 10;
	_text16->Box(controlListDownArrow, false, workerRect, SCI_TEXT16_ALIGNMENT_CENTER, 0);

	// Draw inner lines
	workerRect.top = rect.top + 9;
	workerRect.bottom -= 10;
	_paint16->frameRect(workerRect);
	workerRect.grow(-1);

	_text16->SetFont(fontId);
	fontSize = _ports->_curPort->fontHeight;
	_ports->penColor(_ports->_curPort->penClr); _ports->backColor(_ports->_curPort->backClr);
	workerRect.bottom = workerRect.top + fontSize;
	lastYpos = rect.bottom - fontSize;

	// Write actual text
	for (i = upperPos; i < count; i++) {
		_paint16->eraseRect(workerRect);
		const Common::String &listEntry = entries[i];
		if (listEntry[0]) {
			Common::String textString = listEntry;
			if (g_sci->isLanguageRTL())
				textString = Common::convertBiDiString(textString, g_sci->getLanguage());

			if (!g_sci->isLanguageRTL())
				_ports->moveTo(workerRect.left, workerRect.top);
			else {
				// calc width, for right alignment
				const char *textPtr = textString.c_str();
				uint16 textWidth = 0;
				while (*textPtr)
					textWidth += _text16->_font->getCharWidth((byte)*textPtr++);
				_ports->moveTo(workerRect.right - textWidth - 1, workerRect.top);
			}
			_text16->Draw(textString.c_str(), 0, MIN<int16>(maxChars, listEntry.size()), oldFontId, oldPenColor);
			if ((!isAlias) && (i == cursorPos)) {
				_paint16->invertRect(workerRect);
			}
		}
		workerRect.translate(0, fontSize);
		if (workerRect.bottom > lastYpos)
			break;
	}

	_text16->SetFont(oldFontId);
}

void GfxControls16::texteditCursorDraw(Common::Rect rect, const char *text, uint16 curPos) {
	if (!_texteditCursorVisible) {
		// Measure with the font that DRAWS this line, not the one the control
		// nominally carries: Box() switches a Korean line to font 1001, so the
		// control's own narrow font would measure every syllable at half its
		// drawn width and put the cursor inside the first one.
		GuiResourceId cursorFontId = _text16->FontIdForLine(text, 0);
		GuiResourceId oldFontId = _text16->GetFontId();
		_text16->SetFont(cursorFontId);

		// curPos is a byte offset into the string; the walk steps by whole
		// characters (UTF-8 or code-page pairs), so a multi-byte character is
		// charged its one drawn width rather than once per byte.
		int16 textWidth = _text16->EditTextWidth(text, curPos);
		if (!g_sci->isLanguageRTL())
			_texteditCursorRect.left = rect.left + textWidth;
		else
			_texteditCursorRect.right = rect.right - textWidth;
		_texteditCursorRect.top = rect.top;
		_texteditCursorRect.bottom = _texteditCursorRect.top + _text16->_font->getHeight();
		int16 cursorWidth = 1;
		if (text[curPos] != 0)
			cursorWidth = _text16->EditTextWidth(text + curPos, 1);
		if (!g_sci->isLanguageRTL())
			_texteditCursorRect.right = _texteditCursorRect.left + cursorWidth;
		else
			_texteditCursorRect.left = _texteditCursorRect.right - cursorWidth;
		_text16->SetFont(oldFontId);
		_paint16->invertRect(_texteditCursorRect);
		_paint16->bitsShow(_texteditCursorRect);
		_texteditCursorVisible = true;
		texteditSetBlinkTime();
	}
}

int16 GfxControls16::editLineWidth(const Common::String &text) {
	GuiResourceId controlFontId = _text16->GetFontId();
	_text16->SetFont(_text16->FontIdForLine(text.c_str(), 0));
	int16 width = _text16->EditTextWidth(text.c_str(), text.size());
	_text16->SetFont(controlFontId);
	return width;
}

void GfxControls16::texteditCursorErase() {
	if (_texteditCursorVisible) {
		_paint16->invertRect(_texteditCursorRect);
		_paint16->bitsShow(_texteditCursorRect);
		_texteditCursorVisible = false;
	}
	texteditSetBlinkTime();
}

void GfxControls16::texteditSetBlinkTime() {
	_texteditBlinkTime = g_system->getMillis() + (30 * 1000 / 60);
}

void GfxControls16::kernelTexteditChange(reg_t controlObject, reg_t eventObject) {
	uint16 cursorPos = readSelectorValue(_segMan, controlObject, SELECTOR(cursor));
	uint16 maxChars = readSelectorValue(_segMan, controlObject, SELECTOR(max));
	reg_t textReference = readSelector(_segMan, controlObject, SELECTOR(text));
	Common::String text;
	uint16 eventKey = 0, modifiers = 0;
	bool textChanged = false;
	bool textAddChar = false;
	// A composed character has already been written into `text` by the time
	// the width check below runs, so it needs its own flag and its own undo
	// value rather than textAddChar's insertChar() path.
	bool koreanAddChar = false;
	Common::String koreanTextBefore;
	Common::Rect rect;

	if (textReference.isNull())
		error("kEditControl called on object that doesn't have a text reference");
	text = _segMan->getString(textReference);
	if (g_sci->getSciDebugger())
		g_sci->getSciDebugger()->noteInput(text);

	debugC(2, kDebugLevelHangul,
	       "[edit] ENTER obj=%04x:%04x textRef=%04x:%04x max=%d cursor=%d heap=%s",
	       PRINT_REG(controlObject), PRINT_REG(textReference),
	       maxChars, cursorPos, hangulDump(text).c_str());

	// Scripts count the cursor in characters (kStrLen is a code point count
	// in UTF-8 mode); the code below works on byte offsets.
	const bool utf8 = g_sci->heapStringsAreUtf8();
	if (utf8)
		cursorPos = utf8OffsetOf((const byte *)text.c_str(), cursorPos);
	if (cursorPos > text.size())
		cursorPos = text.size();

	// The composer's run is a byte range in THIS control's string. It stops
	// describing anything real when the control changes, but also - and this
	// is what a length test misses - whenever anyone else rewrites the
	// string: a script clearing the line after Enter, restoring a default,
	// or a saved game loading. The prompt case is the common one and is
	// invisible to an offset check, because the run opens at offset 0 of an
	// empty line and 0 is still <= the new size; the composer would then
	// re-emit the PREVIOUS line's syllables over whatever is there now. So
	// the run is validated against the bytes it was left describing, not
	// against their length.
	if (_koreanRunObject != controlObject || _koreanRunStart > text.size() ||
		_koreanRunText != text) {
		debugC(1, kDebugLevelHangul,
		       "[edit]   GUARD FIRES: obj%s start%s bytes%s -> reset, runStart=%u",
		       _koreanRunObject != controlObject ? "=DIFF" : "=same",
		       _koreanRunStart > text.size() ? "=OOR" : "=ok",
		       _koreanRunText != text ? "=DIFF" : "=same",
		       text.size());
		_koreanInput.reset();
		_koreanRunObject = controlObject;
		_koreanRunStart = text.size();
		_koreanRunText = text;
	}

	uint16 oldCursorPos = cursorPos;

	if (!eventObject.isNull()) {
		uint16 textSize = text.size();
		uint16 eventType = readSelectorValue(_segMan, eventObject, SELECTOR(type));

		// In UTF-8 text a cursor step is one code point, so Backspace, Delete
		// and the arrows never land inside a multi-byte character.
		// A Korean code-page game keeps EUC-KR pairs in the string, and
		// half of one left behind makes the renderer walk off the end of it
		// on the next redraw, so the same steps go by pair there. Other
		// code pages keep their byte-wise steps: dbcs.h knows EUC-KR only.
		const bool eucKr = !utf8 && g_sci->usesKoreanText();
		const byte *bytes = (const byte *)text.c_str();
		auto stepBack = [&](uint16 pos) -> uint16 {
			if (utf8)
				return (uint16)utf8PrevBoundary(bytes, textSize, pos);
			return eucKr ? (uint16)stepCharLeft(text, pos) : pos - 1;
		};
		auto stepForward = [&](uint16 pos) -> uint16 {
			if (utf8)
				return (uint16)utf8NextBoundary(bytes, textSize, pos);
			return eucKr ? (uint16)stepCharRight(text, pos) : pos + 1;
		};

		switch (eventType) {
		case kSciEventMousePress:
			// TODO: Implement mouse support for cursor change
			break;
		case kSciEventKeyDown:
			eventKey = readSelectorValue(_segMan, eventObject, SELECTOR(message));
			modifiers = readSelectorValue(_segMan, eventObject, SELECTOR(modifiers));

			if (DebugMan.isDebugChannelEnabled(kDebugLevelHangul))
				debugC(1, kDebugLevelHangul,
				       "[edit] KEY %d (0x%02x)%s mod=0x%04x korean=%d",
				       eventKey, eventKey,
				       (eventKey > 31 && eventKey < 127)
				           ? Common::String::format(" '%c'", (char)eventKey).c_str()
				           : "",
				       modifiers, (int)_koreanInput.isEnabled());

			// The Han/Yeong state is owned by the event manager, which flips
			// it and emits no event at all. Mirroring it into the composer
			// here rather than reacting to a key means no script can swallow
			// the toggle, and the composer cannot be left switched on by a
			// control that never saw the key.
			if (g_sci->getEventManager()->hangulInputEnabled() != _koreanInput.isEnabled()) {
				_koreanInput.setEnabled(!_koreanInput.isEnabled());
				_koreanRunStart = text.size();
				debugC(1, kDebugLevelHangul,
				       "[edit]   TOGGLE mirrored -> korean=%d runStart=%u",
				       (int)_koreanInput.isEnabled(), _koreanRunStart);
			}
			_koreanInput.setEncoding(utf8 ? KoreanComposer::kUtf8 : KoreanComposer::kCp949);

			// Backspace inside the composer's run removes one jamo rather than
			// one character: 한 -> 하 -> ㅎ -> nothing. The composer holds the
			// decomposition for as long as it owns the run. Gated on
			// ownsRun() and not on isComposing(), because a completed
			// syllable is still the composer's - deleting it here would leave
			// the string and the composer disagreeing about what was typed.
			if (_koreanInput.isEnabled() && eventKey == kSciKeyBackspace &&
				_koreanInput.ownsRun()) {
				// The composer works on the head of the line (everything
				// before the cursor); whatever follows the cursor is put
				// back after it.
				const uint16 headEnd = cursorPos;
				const Common::String tail(text.c_str() + headEnd);
				text.erase(headEnd);
				const bool backspaced = _koreanInput.backspace(text, _koreanRunStart);
				cursorPos = backspaced ? text.size() : headEnd;
				text += tail;
				if (backspaced) {
					textChanged = true;
					debugC(1, kDebugLevelHangul,
					       "[edit]   BKSP composer -> %s runStart=%u owns=%d",
					       hangulDump(text).c_str(), _koreanRunStart,
					       (int)_koreanInput.ownsRun());
					break;
				}
			}

			// A printable key while Korean entry is on goes to the composer,
			// which rewrites its run of the string in the game's encoding.
			// Enter is not one: it ends the line, and the game's script
			// handles it.
			// Ctrl and Alt chords are commands, not text (Ctrl+C clears the line
			// further down), so they bypass the composer.
			if (_koreanInput.isEnabled() && eventKey > 31 && eventKey < 256 &&
				eventKey != kSciKeyEnter &&
				!(modifiers & (kSciKeyModCtrl | kSciKeyModAlt))) {
				koreanTextBefore = text;
				// Composed text goes in at the cursor: the composer sees the
				// head of the line and the tail is rejoined after its run.
				const Common::String tail(text.c_str() + cursorPos);
				text.erase(cursorPos);
				if (!_koreanInput.ownsRun())
					_koreanRunStart = text.size();
				const bool fed = _koreanInput.feed((char)eventKey, text, _koreanRunStart);
				const uint16 headSize = text.size();
				text += tail;
				if (fed) {
					debugC(1, kDebugLevelHangul,
					       "[edit]   FEED ok -> %s runStart=%u owns=%d composing=%d (max=%d)",
					       hangulDump(text).c_str(), _koreanRunStart,
					       (int)_koreanInput.ownsRun(),
					       (int)_koreanInput.isComposing(), maxChars);
					if (text.size() <= maxChars) {
						cursorPos = headSize;
						textChanged = true;
						koreanAddChar = true;
						break;
					}
					// Over the control's limit: undo the key whole, composer
					// state included, so the string and the composer cannot
					// disagree about what has been typed.
					debugC(1, kDebugLevelHangul,
					       "[edit]   REJECT over maxChars: %u > %d, undo to %s",
					       text.size(), maxChars,
					       hangulDump(koreanTextBefore).c_str());
					text = koreanTextBefore;
					_koreanInput.reset();
					_koreanRunStart = text.size();
					_koreanRunText = text;
					if (utf8)
						cursorPos = utf8IndexOfOffset((const byte *)text.c_str(), cursorPos);
					writeSelectorValue(_segMan, controlObject, SELECTOR(cursor), cursorPos);
					return;
				}
				text = koreanTextBefore;
				debugC(1, kDebugLevelHangul,
				       "[edit]   FEED refused (not consumed as Korean) -> falls "
				       "through to ASCII; runStart=%u owns=%d",
				       _koreanRunStart, (int)_koreanInput.ownsRun());
				// The composer refused the key - it would have produced a
				// character this font has no glyph for. Fall through and let
				// it be inserted as ASCII, which is what an unclaimed key is.
			}

			// Everything from here on is a plain edit or caret move that
			// knows nothing about the composer: Enter ends the line, Delete
			// and the ordinary backspace splice characters out of the
			// middle, Ctrl+C clears it, the arrow keys move the caret away
			// from the run, and a refused key falls through to an ASCII
			// insert. If the composer still owned its run across any of
			// those it would re-emit the abandoned syllable on the next
			// keypress (Delete on a composing 한 gave back 한 plus the new
			// syllable; Ctrl+C resurrected the cleared line). So reaching
			// this switch ends the run; the characters already in the string
			// stay where they are.
			if (_koreanInput.ownsRun()) {
				_koreanInput.commit(text, _koreanRunStart);
				_koreanRunText = text;
				debugC(1, kDebugLevelHangul,
				       "[edit]   RELEASE run before editing switch -> runStart=%u owns=%d %s",
				       _koreanRunStart, (int)_koreanInput.ownsRun(),
				       hangulDump(text).c_str());
			}

			switch (eventKey) {
			case kSciKeyBackspace:
				if (cursorPos > 0) {
					const uint16 start = stepBack(cursorPos);
					text.erase(start, cursorPos - start);
					cursorPos = start;
					textChanged = true;
				}
				break;
			case kSciKeyDelete:
				if (cursorPos < textSize) {
					text.erase(cursorPos, stepForward(cursorPos) - cursorPos);
					textChanged = true;
				}
				break;
			case kSciKeyHome:
				cursorPos = 0; textChanged = true;
				break;
			case kSciKeyEnd:
				cursorPos = textSize; textChanged = true;
				break;
			case kSciKeyLeft:
				if (!g_sci->isLanguageRTL()) {
					if (cursorPos > 0) {
						cursorPos = stepBack(cursorPos); textChanged = true;
					}
				} else {
					if (cursorPos + 1 <= textSize) {
						cursorPos = stepForward(cursorPos); textChanged = true;
					}
				}
				break;
			case kSciKeyRight:
				if (!g_sci->isLanguageRTL()) {
					if (cursorPos + 1 <= textSize) {
						cursorPos = stepForward(cursorPos); textChanged = true;
					}
				} else {
					if (cursorPos > 0) {
						cursorPos = stepBack(cursorPos); textChanged = true;
					}
				}
				break;
			case kSciKeyEtx:
				if (modifiers & kSciKeyModCtrl) {
					// Control-C erases the whole line
					cursorPos = 0; text.clear();
					textChanged = true;
				}
				break;
			default:
				if ((modifiers & kSciKeyModCtrl) && eventKey == 99) {
					// Control-C in earlier SCI games (SCI0 - SCI1 middle)
					// Control-C erases the whole line
					cursorPos = 0; text.clear();
					textChanged = true;
				} else if (eventKey > 31 && eventKey < 256 && textSize < maxChars) {
					// insert pressed character
					textAddChar = true;
					textChanged = true;
				}
				break;
			}
			break;
		default:
			break;
		}
	}

	if (g_sci->getVocabulary() && !textChanged && oldCursorPos != cursorPos) {
		assert(!textAddChar);
		textChanged = g_sci->getVocabulary()->checkAltInput(text, cursorPos);
	}

	if (textChanged) {
		GuiResourceId oldFontId = _text16->GetFontId();
		GuiResourceId fontId = readSelectorValue(_segMan, controlObject, SELECTOR(font));
		rect = g_sci->_gfxCompare->getNSRect(controlObject);

		_text16->SetFont(fontId);
		if (textAddChar) {

			// We check if we are really able to add the new char. The width is
			// that of the line as it will be drawn, so the typed character is
			// measured in the context of the text (a Korean line is drawn by
			// font 1001 whatever font the control names).
			Common::String grown = text;
			grown.insertChar(eventKey, cursorPos);
			if (editLineWidth(grown) >= rect.width()) {
				// Does not fit.
				_text16->SetFont(oldFontId);
				return;
			}

			text.insertChar(eventKey, cursorPos++);

			// Note: the following checkAltInput call might make the text
			// too wide to fit, but SSCI fails to check that too.
		}
		if (koreanAddChar) {
			// The same fit check for composed text, which is already IN the
			// string rather than waiting to be inserted - so the whole string
			// is measured and the undo restores what the key replaced.
			if (editLineWidth(text) >= rect.width()) {
				text = koreanTextBefore;
				_koreanInput.reset();
				_koreanRunStart = text.size();
				_koreanRunText = text;
				cursorPos = MIN<uint16>(oldCursorPos, text.size());
				_text16->SetFont(oldFontId);
				if (utf8)
					cursorPos = utf8IndexOfOffset((const byte *)text.c_str(), cursorPos);
				writeSelectorValue(_segMan, controlObject, SELECTOR(cursor), cursorPos);
				return;
			}
		}
		if (g_sci->getVocabulary())
			g_sci->getVocabulary()->checkAltInput(text, cursorPos);
		texteditCursorErase();
		_paint16->eraseRect(rect);

		// Where the driver renders the text itself (the Korean and PC-98
		// drivers) the glyphs go into the driver's own hi-res bitmap, not
		// the low-res buffer this rect describes. The cleared background
		// therefore has to reach the screen BEFORE they are drawn, and must
		// not be pushed again afterwards: that copies the empty background
		// back over the glyphs, which is why a Korean edit control drew
		// nothing. kernelDrawButton and kernelDrawText already follow this
		// rule. Limited to Korean text: the PC-98 edit control keeps its
		// original draw order. It is only true of the double-byte glyphs: a single-byte
		// character still goes through the low-res buffer, and Box()'s
		// `show` argument pushes exactly those lines.
		const bool driverDrawsText = g_sci->usesKoreanText() && _screen->gfxDriver()->driverBasedTextRendering();
		if (driverDrawsText && !getPicNotValid())
			_paint16->bitsShow(rect);
		_text16->Box(text.c_str(), driverDrawsText, rect, SCI_TEXT16_ALIGNMENT_LEFT, -1);
		if (!driverDrawsText)
			_paint16->bitsShow(rect);

		texteditCursorDraw(rect, text.c_str(), cursorPos);
		_text16->SetFont(oldFontId);

		// Write back string. SegManager::strncpy()'s raw path has no bounds
		// check, so a string longer than the game's own buffer corrupts the
		// SCI heap here; log the sizes so a later crash can be traced back.
		if (DebugMan.isDebugChannelEnabled(kDebugLevelHangul)) {
			SegmentRef ref = _segMan->dereference(textReference);
			debugC(1, kDebugLevelHangul,
			       "[heap] write-back %u bytes into maxSize=%d raw=%d %s%s",
			       text.size() + 1, ref.maxSize, (int)ref.isRaw,
			       hangulDump(text).c_str(),
			       (ref.isValid() && ref.maxSize >= 0 &&
			        (int)(text.size() + 1) > ref.maxSize)
			           ? "  *** OVERFLOWS THE GAME'S BUFFER ***" : "");
		}
		_segMan->strcpy_(textReference, text.c_str());

		// Remember what this control left in the string, so the next entry
		// can tell "nobody touched it" from "a script rewrote it".
		_koreanRunText = text;
		// An ordinary edit leaves the composer owning nothing, so the next
		// Korean key must start its run at the end of whatever is there now;
		// otherwise a digit typed after a lone jamo vanished on the next
		// vowel.
		if (!_koreanInput.ownsRun())
			_koreanRunStart = cursorPos;
	} else {
		if (eventKey)
			debugC(1, kDebugLevelHangul,
			       "[edit]   NO REDRAW (textChanged=0) after key %d; heap=%s",
			       eventKey, hangulDump(text).c_str());
		if (g_system->getMillis() >= _texteditBlinkTime) {
			_paint16->invertRect(_texteditCursorRect);
			_paint16->bitsShow(_texteditCursorRect);
			_texteditCursorVisible = !_texteditCursorVisible;
			texteditSetBlinkTime();
		}
	}

	if (utf8)
		cursorPos = utf8IndexOfOffset((const byte *)text.c_str(), cursorPos);
	writeSelectorValue(_segMan, controlObject, SELECTOR(cursor), cursorPos);
}

int GfxControls16::getPicNotValid() {
	if (getSciVersion() >= SCI_VERSION_1_1)
		return _screen->_picNotValidSci11;
	return _screen->_picNotValid;
}

void GfxControls16::kernelDrawButton(Common::Rect rect, reg_t obj, const char *text, uint16 languageSplitter, int16 fontId, int16 style, bool hilite) {
	g_sci->_tts->button(text);

	if (!hilite) {
		int16 sci0EarlyPen = 0, sci0EarlyBack = 0;
		if (getSciVersion() == SCI_VERSION_0_EARLY) {
			// SCI0early actually used hardcoded green/black buttons instead of using the port colors
			sci0EarlyPen = _ports->_curPort->penClr;
			sci0EarlyBack = _ports->_curPort->backClr;
			_ports->penColor(0);
			_ports->backColor(2);
		}
		rect.grow(1);
		_paint16->eraseRect(rect);
		_paint16->frameRect(rect);

		// Unlike PC-98, the Korean fan translations have CJK text for some button controls. The original PC-98
		// interpreters which were used to make the necessary code changes to kernelDrawText do not have any
		// modifications for button controls, since it is not necessary (due to the English button labels). I
		// have now tried to adapt the code changes from kernelDrawText for the button controls. It does require
		// some extra attention, like drawing the buttons frames first, but seems to work as intended. Also, the
		// different handling also seems to work fine for the English buttons (which both the Korean and the PC-98
		// versions have).
		if (_screen->gfxDriver()->driverBasedTextRendering() && !getPicNotValid()) {
			if (style & SCI_CONTROLS_STYLE_SELECTED) {
				rect.grow(-1);
				_paint16->frameRect(rect);
				rect.grow(1);
			}
			_paint16->bitsShow(rect);
		}

		rect.grow(-2);
		_ports->textGreyedOutput(!(style & SCI_CONTROLS_STYLE_ENABLED));

		if (!g_sci->hasMacFonts()) {
			_text16->Box(text, languageSplitter, _screen->gfxDriver()->driverBasedTextRendering(), rect, SCI_TEXT16_ALIGNMENT_CENTER, fontId);
		} else {
			_text16->macDraw(text, rect, SCI_TEXT16_ALIGNMENT_CENTER, fontId, _text16->GetFontId(), 0);
		}
		_ports->textGreyedOutput(false);

		// Fix for Korean fan translation, see comment above.
		if (!_screen->gfxDriver()->driverBasedTextRendering()) {
			rect.grow(1);
			if (style & SCI_CONTROLS_STYLE_SELECTED)
				_paint16->frameRect(rect);
			if (!getPicNotValid()) {
				rect.grow(1);
				_paint16->bitsShow(rect);
			}
		}

		if (getSciVersion() == SCI_VERSION_0_EARLY) {
			_ports->penColor(sci0EarlyPen);
			_ports->backColor(sci0EarlyBack);
		}
	} else {
		// SCI0early used xor to invert button rectangles resulting in pink/white buttons
		// All PC-98 targets (both SCI_VERSION_01 and SCI_VERSION_1_LATE) also use the
		// xor method, resulting in a grey color.
		if (getSciVersion() == SCI_VERSION_0_EARLY || g_sci->getPlatform() == Common::kPlatformPC98)
			_paint16->invertRectViaXOR(rect);
		else
			_paint16->invertRect(rect);
		if (g_sci->hasMacFonts()) {
			// Mac scripts set a flag to tell the interpreter to draw white text when inverted.
			// Note that KQ6 does not do this because it includes the PC version of the script,
			// causing button text to disappear when clicked in the original.
			uint16 textColor = (style & SCI_CONTROLS_STYLE_MAC_INVERTED) ? 255 : 0;
			rect.grow(-1);
			_text16->macDraw(text, rect, SCI_TEXT16_ALIGNMENT_CENTER, fontId, _text16->GetFontId(), textColor);
			rect.grow(1);
		}
		_paint16->bitsShow(rect);
	}
}

void GfxControls16::kernelDrawText(Common::Rect rect, reg_t obj, const char *text, uint16 languageSplitter, int16 fontId, TextAlignment alignment, int16 style, bool hilite) {
	g_sci->_tts->text(text);

	if (!hilite) {
		rect.grow(1);
		_paint16->eraseRect(rect);
		rect.grow(-1);
		if (!g_sci->hasMacFonts()) {
			// The PC-98 versions set the 'show` argument here (unlike normal DOS versions).
			_text16->Box(text, languageSplitter, _screen->gfxDriver()->driverBasedTextRendering(), rect, alignment, fontId);
		} else {
			_text16->macDraw(text, rect, alignment, fontId, _text16->GetFontId(), 0);
		}
		if (style & SCI_CONTROLS_STYLE_SELECTED) {
			_paint16->frameRect(rect);
		}

		// I have checked the PC-98 versions of QFG1 and KQ5. These set all rect bounds for the
		// screen update rect to 0 after the text drawing. So nothing gets updated on screen.
		// Otherwise, it would just overdraw the hi-res text. I have looked at the DOS version of
		// QFG1 for comparison. There, it copies the text box rect into the screen update rect.
		// So this specific handling for the PC-98 versions is correct.
		bool allowScreenUpdate = _screen->gfxDriver()->driverBasedTextRendering() ? false : true;

		if (allowScreenUpdate && !getPicNotValid())
			_paint16->bitsShow(rect);
	} else {
		// SCI0early used xor to invert button rectangles resulting in pink/white buttons
		// All PC-98 targets (both SCI_VERSION_01 and SCI_VERSION_1_LATE) also use the
		// xor method, resulting in a grey color.
		if (getSciVersion() == SCI_VERSION_0_EARLY || g_sci->getPlatform() == Common::kPlatformPC98)
			_paint16->invertRectViaXOR(rect);
		else
			_paint16->invertRect(rect);
		_paint16->bitsShow(rect);
	}
}

void GfxControls16::kernelDrawTextEdit(Common::Rect rect, reg_t obj, const char *text, uint16 languageSplitter, int16 fontId, int16 mode, int16 style, int16 cursorPos, int16 maxChars, bool hilite) {
	Common::Rect textRect = rect;
	uint16 oldFontId = _text16->GetFontId();

	rect.grow(1);
	_texteditCursorVisible = false;
	texteditCursorErase();
	_paint16->eraseRect(rect);
	_text16->Box(text, languageSplitter, false, textRect, SCI_TEXT16_ALIGNMENT_LEFT, fontId);
	_paint16->frameRect(rect);
	if (style & SCI_CONTROLS_STYLE_SELECTED) {
		_text16->SetFont(fontId);
		rect.grow(-1);
		texteditCursorDraw(rect, text, cursorPos);
		_text16->SetFont(oldFontId);
		rect.grow(1);

		g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, true);
	} else {
		g_system->setFeatureState(OSystem::kFeatureVirtualKeyboard, false);
	}
	if (!getPicNotValid())
		_paint16->bitsShow(rect);

	_ports->setActiveWindowHasEditText();
}

void GfxControls16::kernelDrawIcon(Common::Rect rect, reg_t obj, GuiResourceId viewId, int16 loopNo, int16 celNo, int16 priority, int16 style, bool hilite) {
	if (!hilite) {
		_paint16->drawCelAndShow(viewId, loopNo, celNo, rect.left, rect.top, priority, 0);
		if (style & 0x20) {
			_paint16->frameRect(rect);
		}
		if (!getPicNotValid())
			_paint16->bitsShow(rect);
	} else {
		// SCI0early used xor to invert button rectangles resulting in pink/white buttons
		// All PC-98 targets (both SCI_VERSION_01 and SCI_VERSION_1_LATE) also use the
		// xor method, resulting in a grey color.
		if (getSciVersion() == SCI_VERSION_0_EARLY || g_sci->getPlatform() == Common::kPlatformPC98)
			_paint16->invertRectViaXOR(rect);
		else
			_paint16->invertRect(rect);
		_paint16->bitsShow(rect);
	}
}

void GfxControls16::kernelDrawList(Common::Rect rect, reg_t obj, int16 maxChars, int16 count, const Common::String *entries, GuiResourceId fontId, int16 style, int16 upperPos, int16 cursorPos, bool isAlias, bool hilite) {
	if (!hilite) {
		drawListControl(rect, obj, maxChars, count, entries, fontId, upperPos, cursorPos, isAlias);
		rect.grow(1);
		if (isAlias && (style & SCI_CONTROLS_STYLE_SELECTED)) {
			_paint16->frameRect(rect);
		}
		if (!getPicNotValid())
			_paint16->bitsShow(rect);
	}
}

} // End of namespace Sci
