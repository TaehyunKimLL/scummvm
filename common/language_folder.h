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

#ifndef COMMON_LANGUAGE_FOLDER_H
#define COMMON_LANGUAGE_FOLDER_H

#include "common/array.h"
#include "common/fs.h"
#include "common/language.h"

namespace Common {

/**
 * The languages a game folder has a translation in, in the order found: each
 * subfolder named by a language's ISO 639-1 code (getLanguageFolderCode(): ko/,
 * ja/; matched case-insensitively) that holds the translation's table,
 * <@p prefix>-<code>.str (sci-ko.str, agi-ja.str), or a hi-res text map
 * (hires*.map); and each <@p prefix>-<code>.str in the folder itself, where
 * the packs made before the language folders keep it.
 *
 * @p files are the game folder's entries, files and folders.
 */
Array<Language> translationFolderLanguages(const FSList &files, const char *prefix);

} // End of namespace Common

#endif
