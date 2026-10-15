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
#include "common/textconsole.h"
#include "common/hashmap.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/glyph_source_jamo.h"
#include "graphics/hires_text/glyph_source_svfn.h"

namespace Graphics {

bool isSvfnFile(const byte *head, uint32 size) {
	return size >= 4 && head[0] == 'S' && head[1] == 'V' && head[2] == 'F' && head[3] == 'N';
}

// Composing syllables from jamo pieces when the file says so (flags bit 1) and
// its table holds up, else the file's own glyphs. A font whose table does not
// is used as a plain font, with one warning naming it (@p name, which may be
// empty: a stream source has none).
void parsePreloadList(const byte *data, uint32 size, Common::Array<uint32> &out) {
	Common::HashMap<uint32, bool> seen;
	bool lineStart = true, comment = false;
	uint32 i = 0;
	if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
		i = 3;	// a byte order mark is not a character
	while (i < size) {
		const byte b = data[i];
		if (b == '\n' || b == '\r') {
			lineStart = true;
			comment = false;
			++i;
			continue;
		}
		if (comment) {
			++i;
			continue;
		}
		if (lineStart && b == '#') {
			comment = true;
			++i;
			continue;
		}
		lineStart = false;
		uint32 cp = 0;
		int len = 1;
		if (b < 0x80) {
			cp = b;
		} else if ((b & 0xE0) == 0xC0) {
			cp = b & 0x1F;
			len = 2;
		} else if ((b & 0xF0) == 0xE0) {
			cp = b & 0x0F;
			len = 3;
		} else if ((b & 0xF8) == 0xF0) {
			cp = b & 0x07;
			len = 4;
		} else {
			++i;
			continue;
		}
		bool ok = i + len <= size;
		for (int k = 1; ok && k < len; ++k) {
			ok = (data[i + k] & 0xC0) == 0x80;
			cp = (cp << 6) | (data[i + k] & 0x3F);
		}
		if (!ok) {
			++i;
			continue;
		}
		i += len;
		if (cp <= 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || cp == 0xFEFF || cp > 0x10FFFF)
			continue;
		if (!seen.contains(cp)) {
			seen[cp] = true;
			out.push_back(cp);
		}
	}
}

uint32 preloadFaceFromList(UnicodeGlyphSource *src, const Common::Path &listPath, const Common::String &faceName) {
	if (!src || listPath.empty())
		return 0;
	Common::SeekableReadStream *stream = openDataFile(listPath);
	// A list is a few KB (2,350 syllables are 7 KB); a megabyte is not one.
	if (!stream || stream->size() <= 0 || stream->size() > 1024 * 1024) {
		warning("%s: preload list %s cannot be read; no preload", faceName.c_str(), listPath.toString('/').c_str());
		delete stream;
		return 0;
	}
	Common::Array<byte> bytes;
	bytes.resize((uint)stream->size());
	const bool ok = stream->read(bytes.begin(), bytes.size()) == bytes.size();
	delete stream;
	if (!ok) {
		warning("%s: preload list %s cannot be read; no preload", faceName.c_str(), listPath.toString('/').c_str());
		return 0;
	}
	Common::Array<uint32> cps;
	parsePreloadList(bytes.begin(), bytes.size(), cps);
	return src->preload(cps);
}

UnicodeGlyphSource *createSvfnSource(HiResBitmapFont *font, const Common::String &name) {
	if (font->jamoSets() || !font->jamoSetError().empty()) {
		Common::String why;
		if (JamoGlyphSource::usable(*font, why)) {
			JamoGlyphSource *src = new JamoGlyphSource(font, DisposeAfterUse::YES);
			if (!name.empty())
				src->setName(name);
			return src;
		}
		warning("%s: jamo sets ignored: %s", name.empty() ? "SVF" : name.c_str(), why.c_str());
	}
	SvfnGlyphSource *src = new SvfnGlyphSource(font, DisposeAfterUse::YES);
	if (!name.empty())
		src->setName(name);
	return src;
}

UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream &stream, Common::String &error,
									 const Common::String &name) {
	HiResBitmapFont *font = new HiResBitmapFont();
	if (!font->load(stream)) {
		error = "not a valid SVFN bitmap font";
		delete font;
		return nullptr;
	}
	return createSvfnSource(font, name);
}

UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
									 Common::String &error, const Common::String &name) {
	return createSvfnSource(stream, dispose, error, name, kHiResLoadAuto);
}

UnicodeGlyphSource *createSvfnSource(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
									 Common::String &error, const Common::String &name, HiResLoadMode mode) {
	HiResBitmapFont *font = new HiResBitmapFont();
	if (!font->loadStreamed(stream, dispose, mode)) {
		error = "not a valid SVFN bitmap font";
		delete font;
		return nullptr;
	}
	return createSvfnSource(font, name);
}

void unbufferCacheStream(Common::SeekableReadStream *stream) {
	Common::unbufferStream(stream);
}

} // End of namespace Graphics
