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
#include "common/str.h"

namespace Common {

/**
 * The name of a translation's table for a language: @p pattern with its one "%s" replaced by the language's folder
 * code (getLanguageFolderCode()): "sci-%s.str" gives "sci-ko.str", "%s.trs" "ko.trs". Empty for UNK_LANG or a pattern
 * without exactly one "%s".
 */
String translationTableName(const char *pattern, Language lang);

/**
 * The languages a game folder holds a translation in, for a detector that adds a variant per language
 * (DESIGN.md section 4). @p files are the game folder's entries, files and folders. A language counts when
 *
 * - a subfolder named by its folder code (ko/, any case) holds its table (translationTableName(@p pattern), any case)
 *   or a hi-res text map (hires*.map, any case), or
 * - the game folder itself holds a file whose name is @p pattern with a known folder code (a pack made before
 *   language folders: sci-ko.str beside the game).
 *
 * Sorted by folder code, each language once. A folder that is no language's code (patches/, kr/) is not looked into.
 */
Array<Language> translationFolderLanguages(const FSList &files, const char *pattern);

} // End of namespace Common

#endif
