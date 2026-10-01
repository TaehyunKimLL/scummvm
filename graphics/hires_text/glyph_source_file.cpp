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

#include "graphics/hires_text/glyph_source_file.h"

#include "common/str.h"
#include "common/stream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_source_svfn.h"

namespace Graphics {

bool isSvfnFile(const byte *head, uint32 size) {
	return size >= 4 && head[0] == 'S' && head[1] == 'V' && head[2] == 'F' && head[3] == 'N';
}

UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream &stream, Common::String &error) {
	HiResBitmapFont *font = new HiResBitmapFont();
	if (!font->load(stream)) {
		error = "not a valid SVFN bitmap font";
		delete font;
		return nullptr;
	}
	return new SvfnGlyphSource(font, DisposeAfterUse::YES);
}

UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
									 Common::String &error, const Common::String &name) {
	HiResBitmapFont *font = new HiResBitmapFont();
	if (!font->loadStreamed(stream, dispose)) {
		error = "not a valid SVFN bitmap font";
		delete font;
		return nullptr;
	}
	SvfnGlyphSource *src = new SvfnGlyphSource(font, DisposeAfterUse::YES);
	src->setName(name);
	return src;
}

} // End of namespace Graphics
