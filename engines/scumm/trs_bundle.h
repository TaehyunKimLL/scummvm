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

#ifndef SCUMM_TRS_BUNDLE_H
#define SCUMM_TRS_BUNDLE_H

#include "common/language.h"
#include "common/path.h"
#include "common/str.h"

namespace Scumm {

/**
 * The name of a .trs fan-translation bundle, and the language it carries.
 *
 * The format itself is language-neutral - an "SCVMTRS " magic, a line index,
 * room/script ranges and a body of strings. Nothing inside the file says which
 * language it holds, so the file name has to, and detection and the engine
 * must agree on the rule or a bundle is found by one and not read by the
 * other. Both directions live here so there is one rule, not two.
 *
 * The Korean fan translations that established the format all ship
 * "korean.trs". Every other language names its bundle after that language's
 * ScummVM code, e.g. "de.trs", "fr.trs", "br.trs".
 */

/** The bundle a game in this language would carry; empty for UNK_LANG. */
inline Common::Path getTrsBundleName(Common::Language lang) {
	if (lang == Common::KO_KOR)
		return Common::Path("korean.trs");

	const char *code = Common::getLanguageCode(lang);
	if (!code)
		return Common::Path();

	return Common::Path(Common::String::format("%s.trs", code));
}

/**
 * The language a file name announces, or UNK_LANG when the name is not a
 * bundle we recognise. Takes a bare file name, not a path.
 */
inline Common::Language getTrsBundleLanguage(const Common::String &filename) {
	if (!filename.hasSuffixIgnoreCase(".trs"))
		return Common::UNK_LANG;

	Common::String stem(filename);
	stem.erase(stem.size() - 4);

	// The name Korean established, kept for every existing translation.
	if (stem.equalsIgnoreCase("korean"))
		return Common::KO_KOR;

	return Common::parseLanguage(stem);
}

} // End of namespace Scumm

#endif
