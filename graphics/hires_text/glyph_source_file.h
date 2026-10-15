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

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "common/types.h"
#include "graphics/hires_text/bitmap_font.h"

namespace Common {
class SeekableReadStream;
class String;
}

namespace Graphics {

class HiResBitmapFont;
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
 * carries an unsupported version). @p name, if given, names the file in the
 * cache counters and in a warning about its jamo sets.
 */
UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream &stream, Common::String &error,
									 const Common::String &name = Common::String());

/**
 * The same, with the glyphs left in the file (HiResBitmapFont::loadStreamed()):
 * only the tables are read now, a glyph when it is first drawn. The source
 * keeps @p stream, deleting it when @p dispose says so (also on failure);
 * @p name names its cache (Common::FileCacheRegistry).
 */
UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
									 Common::String &error, const Common::String &name);

/// The same, with @p mode choosing whole or streamed loading (HiResLoadMode).
UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
									 Common::String &error, const Common::String &name, HiResLoadMode mode);

/**
 * The source for a font the caller loaded itself (HiResBitmapFont::load() or
 * loadStreamed()): a JamoGlyphSource for a jamo-set font (flags bit 1) whose
 * table holds up, else a plain SvfnGlyphSource, with one warning when the
 * table does not. Takes ownership of @p font; @p name names the cache and the
 * warning, and may be empty.
 */
UnicodeGlyphSource *createSvfnSource(HiResBitmapFont *font, const Common::String &name);

/**
 * The characters of a preload list (.HOT file, README "Preloading"): UTF-8
 * text, any order, any line breaks; a line starting with '#' is a comment.
 * Whitespace, control characters, the byte order mark and bytes that are not
 * UTF-8 are skipped. @p out gets each character once, in first-seen order.
 */
void parsePreloadList(const byte *data, uint32 size, Common::Array<uint32> &out);

/**
 * The map's list for a face: read @p listPath (like a map's data files:
 * from disk or ScummVM's data archives) and hand its characters to
 * @p src->preload(). A list that cannot be read is one warning naming
 * @p faceName and no preload. Returns the glyphs the face read in.
 */
uint32 preloadFaceFromList(UnicodeGlyphSource *src, const Common::Path &listPath, const Common::String &faceName);

/**
 * A stream a cache keeps open and reads in blocks of its own: when it is a
 * C stdio file, the library's buffer is dropped, so that a block read is one
 * read of that block. DJGPP's is 16 KB, filled whole again after every seek:
 * per stream, 16 KB of memory and four times the bytes read per block.
 * Called right after the stream is opened, before any read (setvbuf()
 * after I/O is undefined); null is ignored.
 */
void unbufferCacheStream(Common::SeekableReadStream *stream);

} // End of namespace Graphics

#endif
