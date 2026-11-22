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

#include "common/translation_file.h"
#include "common/textconsole.h"
#include "common/util.h"

namespace Common {

bool isMd5String(const String &s) {
	if (s.size() != 32)
		return false;
	for (uint i = 0; i < s.size(); ++i)
		if (!isXDigit(s[i]))
			return false;
	return true;
}

static bool isIdentifier(const String &s) {
	if (s.empty())
		return false;
	for (uint i = 0; i < s.size(); ++i)
		if (!isAlnum(s[i]) && s[i] != '-' && s[i] != '_')
			return false;
	return true;
}

bool TranslationFileHeader::parse(const String &line, TranslationFileHeader &out, String &error) {
	out = TranslationFileHeader();
	error.clear();
	String text = line;
	while (!text.empty() && (text.lastChar() == '\r' || text.lastChar() == '\n'))
		text.deleteLastChar();
	// A UTF-8 byte order mark is optional at the start of a text file (DESIGN.md section 4).
	if (text.hasPrefix("\xEF\xBB\xBF"))
		text = String(text.c_str() + 3);
	if (!text.hasPrefix("#!"))
		return false;

	TranslationFileHeader h;
	bool seen[5] = { false, false, false, false, false };
	static const char *const kKeys[5] = { "format", "game", "fingerprint", "engine", "vocab" };
	uint i = 2;
	while (i < text.size()) {
		while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
			++i;
		const uint start = i;
		while (i < text.size() && text[i] != ' ' && text[i] != '\t')
			++i;
		if (start == i)
			break;
		const String field(text.c_str() + start, i - start);
		const size_t eq = field.findFirstOf('=');
		if (eq == String::npos || eq == 0) {
			error = "'" + field + "' is not key=value";
			return false;
		}
		const String key(field.c_str(), eq);
		const String value(field.c_str() + eq + 1);
		int k = -1;
		for (int n = 0; n < 5; ++n)
			if (key == kKeys[n])
				k = n;
		if (k < 0)
			continue; // a key of a later format
		if (seen[k]) {
			error = key + " is given twice";
			return false;
		}
		seen[k] = true;
		switch (k) {
		case 0:
			if (value.empty() || value.size() > 4) {
				error = "format=" + value + " is not a number";
				return false;
			}
			for (uint d = 0; d < value.size(); ++d) {
				if (!isDigit(value[d])) {
					error = "format=" + value + " is not a number";
					return false;
				}
			}
			h.format = (int)atoi(value.c_str());
			break;
		case 1:
			h.game = value;
			break;
		case 2:
			h.fingerprint = value;
			break;
		case 3:
			h.engine = value;
			break;
		default:
			h.vocab = value;
			break;
		}
	}

	if (!seen[0]) {
		error = "no format=";
		return false;
	}
	if (h.format != 1) {
		error = String::format("format=%d, this build reads format=1", h.format);
		return false;
	}
	if (!isIdentifier(h.game)) {
		error = "game= is missing or not an id";
		return false;
	}
	if (!isMd5String(h.fingerprint)) {
		error = "fingerprint= is not 32 hex digits";
		return false;
	}
	if (!isIdentifier(h.engine)) {
		error = "engine= is missing or not an id";
		return false;
	}
	if (seen[4] && !isMd5String(h.vocab)) {
		error = "vocab= is not 32 hex digits";
		return false;
	}
	h.fingerprint.toLowercase();
	h.vocab.toLowercase();
	out = h;
	return true;
}

String TranslationFileHeader::toString() const {
	String s = String::format("#! format=%d game=%s fingerprint=%s engine=%s", format, game.c_str(), fingerprint.c_str(),
	                          engine.c_str());
	if (!vocab.empty())
		s += " vocab=" + vocab;
	return s;
}

TranslationCheck checkTranslationHeader(const TranslationFileHeader &found, const TranslationFileHeader &expected,
                                        const String &fileName) {
	if (!found.present()) {
		warning("%s has no fingerprint header (a pack made before headers); loaded unchecked", fileName.c_str());
		return kTranslationUnchecked;
	}
	String differs;
	const struct {
		const char *key;
		const String *want;
		const String *have;
		bool anyCase;
	} fields[] = {
		{ "game", &expected.game, &found.game, false },
		{ "engine", &expected.engine, &found.engine, false },
		{ "fingerprint", &expected.fingerprint, &found.fingerprint, true },
		{ "vocab", &expected.vocab, &found.vocab, true },
	};
	for (int i = 0; i < ARRAYSIZE(fields); ++i) {
		const String &want = *fields[i].want;
		const String &have = *fields[i].have;
		if (want.empty())
			continue;
		if (fields[i].anyCase ? want.equalsIgnoreCase(have) : want == have)
			continue;
		differs += String::format("%s%s=%s, the game's is %s", differs.empty() ? "" : "; ", fields[i].key,
		                          have.empty() ? "(none)" : have.c_str(), want.c_str());
	}
	if (differs.empty())
		return kTranslationMatches;
	warning("%s was made for another game: %s; not loaded", fileName.c_str(), differs.c_str());
	return kTranslationMismatch;
}

} // End of namespace Common
