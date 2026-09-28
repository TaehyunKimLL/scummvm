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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_FILE_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_FILE_H

#include "common/types.h"

namespace Common {
class SeekableReadStream;
class String;
}

namespace Graphics {

class UnicodeGlyphSource;

/**
 * True when the first bytes of a font file are the SVFN magic
 * (HiResBitmapFont, bitmap_font.cpp), i.e. this is a baked bitmap font
 * rather than a TrueType face or one of the engine's own bitmap formats.
 *
 * @param head the file's first bytes (a short peek is enough: only offset 0
 *             through 3 are read)
 * @param size how many bytes of head are valid
 */
bool isSvfnFile(const byte *head, uint32 size);

/**
 * Read an SVFN font from stream and hand it back as a UnicodeGlyphSource, so
 * a caller that only wants glyphs need not know the file was a baked bitmap
 * font rather than a live face.
 *
 * On success the returned source owns the underlying HiResBitmapFont. On
 * failure this returns null and fills error with a one-line reason (the
 * stream failed HiResBitmapFont::load(), e.g. because it was truncated or
 * carries an unsupported version).
 */
UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream &stream, Common::String &error);

} // End of namespace Graphics

#endif
