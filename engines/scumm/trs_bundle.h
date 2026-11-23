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

#include "common/array.h"
#include "common/language.h"
#include "common/path.h"

namespace Scumm {

/**
 * The .trs translation bundle (the "SCVMTRS " file of string.cpp's loadLanguageBundle()) beyond what upstream reads.
 *
 * Names: "<code>.trs" (Common::translationTableName("%s.trs", lang): "ko.trs") first; a Korean game also takes the
 * established "korean.trs".
 *
 * Body (the bytes after the room table): one that starts with the UTF-8 BOM (EF BB BF) holds UTF-8 translations and is
 * used as it is; any other Korean body is CP949 and its translations are converted to UTF-8 once at load
 * (convertTrsBodyToUtf8()): the engine holds Korean text as UTF-8 (korean_text.h). The originals, the lookup keys, are
 * the game's own bytes and are never touched.
 */

void trsBundleNames(Common::Language lang, Common::Array<Common::Path> &out);

/** Whether @p body starts with EF BB BF. */
bool trsBodyIsUtf8(const byte *body, uint32 size);

/**
 * Rebuild a CP949 body into @p out (a BOM first, so the result is marked UTF-8): the strings at @p translations are
 * converted with cp949ToUtf8(), every other string is copied byte for byte. Both offset lists (relative to the body,
 * each at a string start) are moved to the same strings; false when an offset is not at a string start.
 */
bool convertTrsBodyToUtf8(const byte *body, uint32 size, Common::Array<uint32> &originals,
                          Common::Array<uint32> &translations, Common::Array<byte> &out, uint &unmapped);

} // End of namespace Scumm

#endif
