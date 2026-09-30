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

#include "graphics/hires_text/font_value.h"

#include "common/util.h"
#include "graphics/hires_text/font_map.h"

namespace Graphics {

namespace {

const uint32 kMaxCodePoint = 0x10FFFF;

/** 1-6 hex digits, no prefix. */
bool parseHexDigits(const Common::String &s, uint32 &out) {
	if (s.empty() || s.size() > 6)
		return false;
	uint32 n = 0;
	for (uint i = 0; i < s.size(); ++i) {
		const char c = s[i];
		int digit;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			return false;
		n = (n << 4) | (uint32)digit;
	}
	out = n;
	return true;
}

/** `+0xNNNN`, the `[glyphs]` offset grammar (design section 6.7). */
bool parseOffsetValue(const Common::String &text, uint32 &out) {
	if (text.empty() || text[0] != '+')
		return false;
	const Common::String rest(text.c_str() + 1);
	if (!rest.hasPrefixIgnoreCase("0x"))
		return false;
	uint32 n;
	if (!parseHexDigits(Common::String(rest.c_str() + 2), n))
		return false;
	out = n;
	return true;
}

/** A path entry: contains a separator or an extension dot, or names a
 *  ScummVM data file (design section 5.1). */
bool looksLikePath(const Common::String &s) {
	return s.contains('/') || s.contains('\\') || s.contains('.') || HiResFontMap::isDataPath(s);
}

/**
 * Classify one font-value entry (design section 5.1's order: `same`,
 * `original`, a `[fonts]` name, a path). Returns false only for "none of
 * these" (an unknown face name); the caller supplies the warning text, since
 * the two callers word it differently (a chain entry vs. a `[glyphs]` target).
 */
bool classifyFaceEntry(const Common::String &raw, const HiResFaceNames &names,
					   const Common::Path &namesBaseDir, const Common::Path &pathBaseDir,
					   HiResFaceEntry &out) {
	out.written = raw;

	if (raw.equalsIgnoreCase("same")) {
		out.kind = kHiResFaceSame;
		out.path = Common::Path();
		return true;
	}
	if (raw.equalsIgnoreCase("original")) {
		out.kind = kHiResFaceOriginal;
		out.path = Common::Path();
		return true;
	}

	Common::String mapped;
	if (names.tryGetVal(raw, mapped)) {
		out.kind = kHiResFaceFile;
		out.path = HiResFontMap::resolvePath(mapped, namesBaseDir);
		return true;
	}

	if (looksLikePath(raw)) {
		out.kind = kHiResFaceFile;
		out.path = HiResFontMap::resolvePath(raw, pathBaseDir);
		return true;
	}

	return false;
}

} // End of anonymous namespace

bool isValidFaceName(const Common::String &name) {
	if (name.empty())
		return false;
	if (name.equalsIgnoreCase("same") || name.equalsIgnoreCase("original") || name.equalsIgnoreCase("data"))
		return false;
	for (uint i = 0; i < name.size(); ++i) {
		const char c = name[i];
		const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
						(c >= '0' && c <= '9') || c == '_' || c == '-';
		if (!ok)
			return false;
	}
	return true;
}

bool parseCodePointValue(const Common::String &text, uint32 &out) {
	Common::String hex;
	if (text.hasPrefixIgnoreCase("u+"))
		hex = Common::String(text.c_str() + 2);
	else if (text.hasPrefixIgnoreCase("0x"))
		hex = Common::String(text.c_str() + 2);
	else
		return false;

	uint32 n;
	if (!parseHexDigits(hex, n) || n > kMaxCodePoint)
		return false;
	out = n;
	return true;
}

bool parseFontValue(const Common::String &text, const HiResFaceNames &names,
					const Common::Path &namesBaseDir, const Common::Path &pathBaseDir,
					HiResFontValue &out, Common::Array<Common::String> &warnings) {
	out.entries.clear();

	bool endedInOriginal = false;
	Common::Array<Common::String> droppedAfterOriginal;

	const char *p = text.c_str();
	while (true) {
		const char *comma = strchr(p, ',');
		Common::String entry(p, comma ? (uint32)(comma - p) : (uint32)strlen(p));
		entry.trim();

		if (!entry.empty()) {
			if (endedInOriginal) {
				droppedAfterOriginal.push_back(entry);
			} else {
				HiResFaceEntry face;
				if (classifyFaceEntry(entry, names, namesBaseDir, pathBaseDir, face)) {
					out.entries.push_back(face);
					if (face.kind == kHiResFaceOriginal)
						endedInOriginal = true;
				} else {
					warnings.push_back(Common::String::format("hires_text.map: unknown face name '%s'", entry.c_str()));
				}
			}
		}

		if (!comma)
			break;
		p = comma + 1;
	}

	if (!droppedAfterOriginal.empty()) {
		Common::String joined;
		for (uint i = 0; i < droppedAfterOriginal.size(); ++i) {
			if (i)
				joined += ", ";
			joined += "'";
			joined += droppedAfterOriginal[i];
			joined += "'";
		}
		warnings.push_back("hires_text.map: entries after 'original' are ignored: " + joined);
	}

	return !out.entries.empty();
}

bool parseGlyphRule(const Common::String &text, const HiResFaceNames &names, const Common::Path &baseDir,
					HiResGlyphRule &out, Common::String &error) {
	error.clear();

	Common::String t = text;
	t.trim();

	if (t.equalsIgnoreCase("original")) {
		out.kind = kHiResGlyphOriginal;
		out.value = 0;
		out.face = HiResFaceEntry();
		return true;
	}

	// Split at the last ':', but only when what follows looks like a code
	// point or an offset - so "data:x.ttf" and "C:\f\x.ttf#1" survive whole
	// when there is no target suffix.
	const size_t colon = t.findLastOf(':');
	if (colon != Common::String::npos) {
		Common::String tail(t.c_str() + colon + 1);
		tail.trim();
		if (tail.hasPrefixIgnoreCase("u+") || (!tail.empty() && tail[0] == '+')) {
			Common::String head(t.c_str(), colon);
			head.trim();

			uint32 value;
			bool isOffset = (tail[0] == '+');
			const bool parsed = isOffset ? parseOffsetValue(tail, value) : parseCodePointValue(tail, value);
			if (!parsed) {
				error = Common::String::format("'%s' is not a code point or offset", tail.c_str());
				return false;
			}

			if (head.contains(',')) {
				error = "a [glyphs] target names one face, not a chain";
				return false;
			}
			if (head.equalsIgnoreCase("original")) {
				error = "'original' is not a face";
				return false;
			}

			HiResFaceEntry face;
			if (!classifyFaceEntry(head, names, baseDir, baseDir, face)) {
				error = Common::String::format("unknown face name '%s'", head.c_str());
				return false;
			}

			out.kind = isOffset ? kHiResGlyphTargetOffset : kHiResGlyphTarget;
			out.value = value;
			out.face = face;
			return true;
		}
	}

	if (!t.empty() && t[0] == '+') {
		uint32 value;
		if (!parseOffsetValue(t, value)) {
			error = Common::String::format("'%s' is not a valid offset", t.c_str());
			return false;
		}
		out.kind = kHiResGlyphOffset;
		out.value = value;
		out.face = HiResFaceEntry();
		return true;
	}

	uint32 value;
	if (parseCodePointValue(t, value)) {
		out.kind = kHiResGlyphCodePoint;
		out.value = value;
		out.face = HiResFaceEntry();
		return true;
	}

	error = Common::String::format("'%s' is not a valid [glyphs] value", t.c_str());
	return false;
}

} // End of namespace Graphics
