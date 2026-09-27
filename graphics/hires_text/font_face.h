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

#ifndef GRAPHICS_HIRES_TEXT_FONT_FACE_H
#define GRAPHICS_HIRES_TEXT_FONT_FACE_H

#include "common/path.h"
#include "common/str.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

/** The largest face index a font path may name (FreeType's face index
 *  proper is 16 bits; the bits above select named instances). */
enum { kMaxFontFaceIndex = 0xFFFF };

/**
 * Split a font file name "<file>#<N>" into the file and the face N of a
 * TrueType collection (.ttc). True only when the text after the last '#'
 * is one or more ASCII digits and the text before it is not empty; then
 * @p file and @p faceIndex are set, faceIndex to -1 when N is above
 * kMaxFontFaceIndex. Otherwise false, and both are left untouched.
 *
 * Only the name is looked at; resolveFontFace() decides whether a file by
 * the full name exists first.
 */
bool splitFontFaceIndex(const Common::String &name, Common::String &file, int32 &faceIndex);

/** Whether a path names an existing file (not a directory). */
typedef bool (*FontFileExistsFn)(const Common::Path &path);

/** FontFileExistsFn on the real file system (Common::FSNode). */
bool fontFileExists(const Common::Path &path);

/**
 * Resolve a font path from a hi-res text map to a file and face index:
 *  - a file by the full name exists: that file, face 0 (so a literal '#'
 *    in a file name still works);
 *  - otherwise, when the last component ends in "#<N>"
 *    (splitFontFaceIndex()) and the file without it exists: that file,
 *    face N (-1 when N is out of range, which the opener must refuse);
 *  - otherwise false, with @p file the path as given and face 0.
 */
bool resolveFontFace(const Common::Path &path, Common::Path &file, int32 &faceIndex,
                     FontFileExistsFn exists = fontFileExists);

/**
 * Open the font a map path names (resolveFontFace()) for reading, and set
 * @p faceIndex to the face to open in it. Null on failure, with @p error
 * set: "does not exist", "is a directory", "could not open the file", or,
 * for "<file>#<N>" with N too large, "face index out of range (0..65535)".
 * The stream is the caller's. A path without a face suffix opens exactly
 * as Common::FSNode(path).createReadStream() does, at face 0.
 */
Common::SeekableReadStream *openFontFace(const Common::Path &path, int32 &faceIndex, Common::String &error);

} // End of namespace Graphics

#endif
