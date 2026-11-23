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

#include "common/hashmap.h"
#include "common/language_folder.h"

#include "scumm/korean_text.h"
#include "scumm/trs_bundle.h"

namespace Scumm {

void trsBundleNames(Common::Language lang, Common::Array<Common::Path> &out) {
	out.clear();
	const Common::String name = Common::translationTableName("%s.trs", lang);
	if (!name.empty())
		out.push_back(Common::Path(name));
	if (lang == Common::KO_KOR)
		out.push_back(Common::Path("korean.trs"));
}

bool trsBodyIsUtf8(const byte *body, uint32 size) {
	return body && size >= 3 && body[0] == 0xEF && body[1] == 0xBB && body[2] == 0xBF;
}

bool convertTrsBodyToUtf8(const byte *body, uint32 size, Common::Array<uint32> &originals,
                          Common::Array<uint32> &translations, Common::Array<byte> &out, uint &unmapped) {
	out.clear();
	unmapped = 0;
	out.push_back(0xEF);
	out.push_back(0xBB);
	out.push_back(0xBF);

	Common::HashMap<uint32, bool> isTranslation;
	for (uint i = 0; i < translations.size(); i++)
		isTranslation.setVal(translations[i], true);
	Common::HashMap<uint32, uint32> moved;
	const byte *end = body + size;
	uint32 pos = 0;
	while (pos < size) {
		const byte *s = body + pos;
		// The bundle's strings are NUL-terminated as the builder wrote them (an escape argument 0 ends one too:
		// MM's original "sound \xff\x04\xc8" is followed by its NUL).
		const byte *e = (const byte *)memchr(s, 0, end - s);
		if (!e)
			e = end;
		moved.setVal(pos, out.size());
		if (isTranslation.contains(pos))
			cp949ToUtf8(s, e, out, unmapped);
		else
			for (const byte *q = s; q < e; q++)
				out.push_back(*q);
		out.push_back(0);
		pos = (uint32)(e - body) + 1;
	}

	Common::Array<uint32> *lists[2] = { &originals, &translations };
	for (int l = 0; l < 2; l++) {
		for (uint i = 0; i < lists[l]->size(); i++) {
			uint32 to;
			if (!moved.tryGetVal((*lists[l])[i], to))
				return false;
			(*lists[l])[i] = to;
		}
	}
	return true;
}

} // End of namespace Scumm
