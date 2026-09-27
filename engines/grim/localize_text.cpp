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

#include "common/util.h"

#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_props.h"

#include "engines/grim/localize_text.h"

namespace Grim {

bool stripUtf8Bom(const char *&data, int32 &size) {
	if (size < 3 || (byte)data[0] != 0xEF || (byte)data[1] != 0xBB || (byte)data[2] != 0xBF)
		return false;
	data += 3;
	size -= 3;
	return true;
}

Common::String selectGrimTab(const Common::String &langCode, bool legacyKorean, bool warnMissing,
                             const TabFileProbe &probe, Common::String &warning) {
	warning.clear();
	const Common::String fallback = "grim.tab";

	Common::String own;
	if (!langCode.empty() && !langCode.equalsIgnoreCase("en"))
		own = "grim." + langCode + ".tab";
	if (!own.empty() && probe.exists(own))
		return own;

	Common::String chosen = fallback;
	if (legacyKorean && probe.exists("grim.ko.tab"))
		chosen = "grim.ko.tab";

	if (!own.empty() && warnMissing && chosen != own)
		warning = Common::String::format("Grim: %s is missing; using %s", own.c_str(), chosen.c_str());
	return chosen;
}

void parseTabLines(const char *data, int32 size, int32 start, bool stopAtEmptyLine, bool skipEof,
                   Common::StringMap &entries, Common::StringArray *continuations) {
	const char *nextline = data;
	Common::String lastEntry;
	// The loop of engines/grim/localize.cpp, with lengths that cannot go
	// negative on LF-only or unterminated lines.
	for (const char *line = data + start; nextline != nullptr && (line - data <= size);
	     nextline != nullptr && (line = nextline + 1)) {
		nextline = strchr(line, '\n');
		// If there is no next line we arrived at the last one
		if (nextline == nullptr)
			nextline = strchr(line, '\0');

		// In Grim we have to exit on the first empty line, else skip it
		if (*line == '\r') {
			if (stopAtEmptyLine)
				break;
			nextline = (line - data) + 2 <= size ? strchr(line + 2, '\n') : nullptr;
			continue;
		}

		// EMI has a garbage line which should be ignored
		if (skipEof && *line == '\x1A')
			continue;

		const char *tab = strchr(line, '\t');
		// Skip the line if no tab is found
		if (tab == nullptr)
			continue;

		if (tab > nextline) {
			const Common::String cont(line, MAX<int32>(0, (int32)(nextline - line) - 1));
			if (lastEntry.empty())
				continue;
			if (continuations)
				continuations->push_back(Common::String::format("Continuation line: \"%s\" = \"%s\" + \"%s\"",
					lastEntry.c_str(), entries[lastEntry].c_str(), cont.c_str()));
			entries[lastEntry] += cont;
		} else {
			lastEntry = Common::String(line, tab - line);
			entries[lastEntry] = Common::String(tab + 1, MAX<int32>(0, (int32)(nextline - tab) - 2));
		}
	}
}

namespace {

bool isLatinLetter(uint32 cp) {
	if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'))
		return true;
	// Latin-1 letters (not the multiplication and division signs), Latin
	// Extended-A/B, Latin Extended Additional.
	if (cp >= 0xC0 && cp <= 0x24F)
		return cp != 0xD7 && cp != 0xF7;
	return cp >= 0x1E00 && cp <= 0x1EFF;
}

} // End of anonymous namespace

bool splitWantsDash(const Graphics::TextRun &run, const Graphics::LineSpan &span) {
	if (!span.emergency || span.forced || span.end <= span.first || span.next >= run.size())
		return false;
	// The base before the split (past its marks) and the unit after it.
	uint32 before = span.end - 1;
	while (before > span.first && Graphics::Unicode::isCombining(run.cp(before)))
		before--;
	return isLatinLetter(run.cp(before)) && isLatinLetter(run.cp(span.next));
}

} // End of namespace Grim
