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

#ifndef GRIM_LOCALIZE_TEXT_H
#define GRIM_LOCALIZE_TEXT_H

#include "common/hash-str.h"
#include "common/str.h"
#include "common/str-array.h"

namespace Graphics {
class TextRun;
struct LineSpan;
}

namespace Grim {

/*
 * The parts of the translation table reader and of the line breaker that
 * need no engine state (no g_grim, no resource loader), so the test runner
 * links them alone.
 */

/**
 * A UTF-8 byte order mark (EF BB BF) at data: step over its 3 bytes and
 * return true. Otherwise data and size are left as they are.
 */
bool stripUtf8Bom(const char *&data, int32 &size);

/** Whether a file of the game directory exists (the resource loader, or a fake in tests). */
class TabFileProbe {
public:
	virtual ~TabFileProbe() {}
	virtual bool exists(const Common::String &name) const = 0;
};

/**
 * The translation table of Grim (retail, not remastered, not a demo), in
 * this order: grim.<langCode>.tab when langCode is neither empty nor "en";
 * grim.ko.tab when legacyKorean (the detected Korean fan patch); grim.tab.
 * The first one present wins; grim.tab when none is (the caller then fails
 * as it always did).
 *
 * warning is cleared, and set to one message when the language's own table
 * is missing and warnMissing is true (the language was forced, not detected).
 */
Common::String selectGrimTab(const Common::String &langCode, bool legacyKorean, bool warnMissing,
                             const TabFileProbe &probe, Common::String &warning);

/**
 * The "key\tvalue\r\n" lines of a table, from data + start. data[size] must
 * be NUL. As the engine always read them: a line without a TAB is appended
 * to the previous entry when a later line has one (each such line adds a
 * message to continuations, when given); stopAtEmptyLine (Grim) ends at the
 * first "\r" line, otherwise it is skipped; skipEof skips 0x1A lines (EMI).
 * The last byte of each value is taken to be the CR before the LF.
 */
void parseTabLines(const char *data, int32 size, int32 start, bool stopAtEmptyLine, bool skipEof,
                   Common::StringMap &entries, Common::StringArray *continuations);

/**
 * Whether Grim ends this line with '-': an emergency split (no break
 * opportunity fitted) between two Latin letters. Japanese, Thai and
 * Chinese always split without a dash.
 */
bool splitWantsDash(const Graphics::TextRun &run, const Graphics::LineSpan &span);

/** The widths the legacy breaker measures with (the engine's Font). */
class LegacyWrapWidths {
public:
	virtual ~LegacyWrapWidths() {}
	virtual int32 charWidth(uint32 c) const = 0;
	virtual int32 wcharWidth(byte hi, byte lo) const = 0;
	virtual bool isKoreanChar(byte hi, byte lo) const = 0;
};

/**
 * Grim's own breaker, for byte, DBCS and official UTF-16 text: a line ends
 * at its last space (which stays on the line), a word that does not fit is
 * cut with '-' (useDash, off for Chinese), CP949 pairs are kept whole
 * (koreanDbcs). Lines are joined by '\n' in message; returns the number of
 * lines. Moved unchanged out of TextObject::setupTextReal().
 */
template<typename S>
int wrapLegacy(const S &msg, int maxWidth, S &message, const LegacyWrapWidths &w, bool koreanDbcs, bool useDash) {
	S currLine;
	int numberLines = 1;
	int lineWidth = 0;
	bool isMultiByte = false;
	for (uint i = 0; i < msg.size(); i++) {
		message += msg[i];
		currLine += msg[i];
		if (i < msg.size() - 1 && koreanDbcs && w.isKoreanChar(msg[i], msg[i + 1])) {
			isMultiByte = true;
			message += msg[i + 1];
			currLine += msg[i + 1];
			lineWidth += w.wcharWidth(msg[i], msg[i + 1]);
			i++;
		} else {
			isMultiByte = false;
			lineWidth += w.charWidth(msg[i]);
		}

		if (currLine.size() > 1 && lineWidth > maxWidth) {
			if (isMultiByte) {
				// Remove 2byte code
				lineWidth -= w.wcharWidth(msg[i - 1], msg[i]);
				message.deleteLastChar();
				message.deleteLastChar();
				currLine.deleteLastChar();
				currLine.deleteLastChar();
				i -= 2;
			} else {
				if (currLine.contains(' ')) {
					while (currLine.lastChar() != ' ' && currLine.size() > 1) {
						lineWidth -= w.charWidth(currLine.lastChar());
						message.deleteLastChar();
						currLine.deleteLastChar();
						--i;
					}
				} else { // if it is a unique word
					int dashWidth = useDash ? w.charWidth('-') : 0;
					while (lineWidth + dashWidth > maxWidth && currLine.size() > 1) {
						lineWidth -= w.charWidth(currLine.lastChar());
						message.deleteLastChar();
						currLine.deleteLastChar();
						--i;
					}
					if (useDash)
						message += '-';
				}
			}
			message += '\n';
			currLine.clear();
			numberLines++;

			lineWidth = 0;
		}
	}
	return numberLines;
}

} // End of namespace Grim

#endif
