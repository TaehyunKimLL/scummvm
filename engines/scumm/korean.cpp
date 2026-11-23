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

#include "common/str.h"
#include "scumm/charset.h"
#include "scumm/resource.h"
#include "scumm/korean_text.h"
#include "scumm/trs_bundle.h"

namespace Scumm {

void ScummEngine::transcodeLanguageBundle(uint32 bodySize) {
	Common::Array<uint32> originals, translations;
	for (int i = 0; i < _numTranslatedLines; i++) {
		originals.push_back(_translatedLines[i].originalTextOffset);
		translations.push_back(_translatedLines[i].translatedTextOffset);
	}
	Common::Array<byte> body;
	uint unmapped = 0;
	if (!convertTrsBodyToUtf8(_languageBuffer, bodySize, originals, translations, body, unmapped)) {
		warning("The CP949 translation bundle is malformed (an entry inside a string): not used");
		_existLanguageFile = false;
		return;
	}
	for (int i = 0; i < _numTranslatedLines; i++) {
		_translatedLines[i].originalTextOffset = originals[i];
		_translatedLines[i].translatedTextOffset = translations[i];
	}
	delete[] _languageBuffer;
	_languageBuffer = new byte[body.size()];
	memcpy(_languageBuffer, body.begin(), body.size());
	if (unmapped)
		warning("CP949 translation bundle: %u characters could not be read and show as '?'", unmapped);
	debug(2, "loadLanguageBundle: CP949 body converted to UTF-8, %u -> %u bytes", bodySize, body.size());
}

void ScummEngine::copyTranslatedText(byte *dst, const byte *src, int dstSize) {
	if (_koreanUtf8)
		toEngineText(src, resStrLen(src) + 1, dst, dstSize);
	else
		memcpy(dst, src, MIN<int>(resStrLen(src) + 1, dstSize));
}

} // End of namespace Scumm
