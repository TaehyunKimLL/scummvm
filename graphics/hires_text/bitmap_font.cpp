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

#include "graphics/hires_text/bitmap_font.h"

#include "common/endian.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/ustr.h"

namespace Graphics {

// The header this format opens with. The engine's own bitmap fonts have no
// signature at all - they start with a length byte that happens to always be 2
// - so a magic is what lets the two be told apart by looking.
static const uint32 kMagic = MKTAG('S', 'V', 'F', 'N');

static const int kHeaderSize = 32;

// Version 1 orders its glyphs by a code page named in the header. Version 2
// adds a table mapping code points to glyphs, for fonts that do not follow any
// code page's order.
static const uint16 kMaxVersion = 2;

enum {
	kFlagProportional = 1 << 0
};

// Bytes per entry in the metrics table: advance, left bearing, ink width, and
// one still unused.
static const int kMetricsEntrySize = 4;

// Bytes per entry in the code point table of a version 2 font.
static const int kCmapEntrySize = 8;

HiResBitmapFont::HiResBitmapFont() {
	_data = nullptr;
	_dataSize = 0;
	_pixels = nullptr;
	_metrics = nullptr;
	_bpp = 0;
	_cellW = 0;
	_cellH = 0;
	_ascent = 0;
	_glyphs = 0;
	_rowPitch = 0;
	_glyphStride = 0;
	_codePage = Common::kCodePageInvalid;
}

HiResBitmapFont::~HiResBitmapFont() {
	free();
}

void HiResBitmapFont::free() {
	delete[] _data;
	_data = nullptr;
	_dataSize = 0;
	_pixels = nullptr;
	_metrics = nullptr;
	_bpp = 0;
	_cellW = 0;
	_cellH = 0;
	_ascent = 0;
	_glyphs = 0;
	_rowPitch = 0;
	_glyphStride = 0;
	_codePage = Common::kCodePageInvalid;
	_cmap.clear();
	_legacyMap.clear();
}

bool HiResBitmapFont::load(Common::SeekableReadStream &stream, uint32 sizeLimit) {
	free();

	const int64 size64 = stream.size();
	if (size64 < kHeaderSize || (uint64)size64 > sizeLimit)
		return false;

	const uint32 size = (uint32)size64;
	byte *raw = new byte[size];
	if (stream.read(raw, size) != size) {
		delete[] raw;
		return false;
	}

	if (READ_BE_UINT32(raw) != kMagic) {
		// Not this format. The caller can still try to read it as one of the
		// engine's own fonts, so this is not worth a warning.
		delete[] raw;
		return false;
	}

	const uint16 version = READ_LE_UINT16(raw + 4);
	if (version < 1 || version > kMaxVersion) {
		warning("HiResText: font version %d is not supported by this build", version);
		delete[] raw;
		return false;
	}

	const uint16 flags = READ_LE_UINT16(raw + 6);
	const int bpp = raw[8];
	const uint16 codePage = READ_LE_UINT16(raw + 10);
	const int glyphs = READ_LE_UINT16(raw + 12);
	const int cellW = raw[14];
	const int cellH = raw[15];
	const int ascent = raw[16];
	const uint32 metricsOff = READ_LE_UINT32(raw + 20);
	const uint32 dataOff = READ_LE_UINT32(raw + 24);
	const uint32 dataSize = READ_LE_UINT32(raw + 28);

	if (bpp != 1 && bpp != 8) {
		warning("HiResText: font has unsupported depth %d", bpp);
		delete[] raw;
		return false;
	}
	if (cellW <= 0 || cellH <= 0 || glyphs <= 0) {
		warning("HiResText: font has an empty glyph box");
		delete[] raw;
		return false;
	}

	const int rowPitch = (bpp == 1) ? (cellW + 7) / 8 : cellW;
	const uint32 glyphStride = (uint32)rowPitch * (uint32)cellH;

	// Everything the header points at has to lie inside the file. These are
	// files a translation ships, so a truncated or hand-edited one must fail
	// the load rather than read past the buffer.
	if (dataOff > size || dataSize > size - dataOff) {
		warning("HiResText: font glyph data runs past the end of the file");
		delete[] raw;
		return false;
	}
	if (glyphStride > dataSize / (uint32)glyphs) {
		warning("HiResText: font declares %d glyphs but holds room for fewer", glyphs);
		delete[] raw;
		return false;
	}

	const byte *metrics = nullptr;
	if (flags & kFlagProportional) {
		const uint32 metricsSize = (uint32)glyphs * kMetricsEntrySize;
		if (metricsOff > size || metricsSize > size - metricsOff) {
			// The glyphs themselves are still usable; only the advances are
			// lost, and the cell width is a sane stand-in for them.
			warning("HiResText: font metrics table runs past the end of the file");
		} else {
			metrics = raw + metricsOff;
		}
	}

	// A version 2 font carries its own code point table, so it does not have
	// to follow the order of any code page.
	Common::HashMap<uint32, int> cmap;
	if (version >= 2) {
		const uint32 cmapOff = READ_LE_UINT32(raw + 16);
		const uint32 cmapSize = (uint32)glyphs * kCmapEntrySize;
		if (cmapOff == 0 || cmapOff > size || cmapSize > size - cmapOff) {
			warning("HiResText: font code point table runs past the end of the file");
			delete[] raw;
			return false;
		}

		for (int i = 0; i < glyphs; ++i) {
			const byte *entry = raw + cmapOff + (uint32)i * kCmapEntrySize;
			const uint32 codepoint = READ_LE_UINT32(entry);
			const uint32 index = READ_LE_UINT32(entry + 4);
			if (index >= (uint32)glyphs) {
				warning("HiResText: font maps U+%04X to a glyph it does not have", codepoint);
				delete[] raw;
				return false;
			}
			cmap[codepoint] = (int)index;
		}
	}

	_data = raw;
	_dataSize = size;
	_pixels = raw + dataOff;
	_metrics = metrics;
	_bpp = bpp;
	_cellW = cellW;
	_cellH = cellH;
	_ascent = ascent;
	_glyphs = glyphs;
	_rowPitch = rowPitch;
	_glyphStride = (int)glyphStride;
	_cmap = cmap;
	_legacyMap.clear();

	if (version >= 2) {
		_codePage = Common::kUtf8;
	} else if (codePage == 0) {
		// Fonts baked for the single byte range say nothing here, and their
		// glyphs sit at the byte value itself. That is Latin-1 for the range
		// they actually cover.
		_codePage = Common::kISO8859_1;
	} else {
		switch (codePage) {
		case 932:
			_codePage = Common::kWindows932;
			break;
		case 936:
			_codePage = Common::kWindows936;
			break;
		case 949:
			_codePage = Common::kWindows949;
			break;
		case 950:
			_codePage = Common::kWindows950;
			break;
		case 1252:
			_codePage = Common::kWindows1252;
			break;
		default:
			warning("HiResText: font names unknown code page %d", codePage);
			_codePage = Common::kCodePageInvalid;
			break;
		}
	}

	return true;
}

/**
 * Where a code point sits in a font ordered by a legacy code page.
 *
 * The glyphs of such a font are laid out in the order that code page's own
 * byte values give, so the way in is to encode the code point back to those
 * bytes. Doing it through the shared encoder means the tables are the ones the
 * rest of ScummVM already uses, rather than a second copy of the arithmetic.
 */
/// The two bytes a glyph index stands for, in the order its code page lays
/// the block out. Returns false for an index outside that block.
static bool legacyIndexToBytes(Common::CodePage page, int index, byte &hi, byte &lo) {
	switch (page) {
	case Common::kWindows932: {
		// Shift-JIS has two lead byte ranges and 188 usable trail bytes, with
		// a gap at 0x7F.
		const int lead = index / 188;
		const int trail = index % 188;
		const int leadByte = (lead < 0x1f) ? 0x81 + lead : 0xc1 + lead;
		if (leadByte > 0xfc)
			return false;
		hi = (byte)leadByte;
		lo = (byte)(trail < 0x3f ? 0x40 + trail : 0x41 + trail);
		return true;
	}
	case Common::kWindows936:
	case Common::kWindows950: {
		const int lead = index / 191;
		if (0x81 + lead > 0xfe)
			return false;
		hi = (byte)(0x81 + lead);
		lo = (byte)(0x40 + index % 191);
		return true;
	}
	case Common::kWindows949: {
		// The 2350 syllables of KS X 1001, 94 to a row from 0xB0A1.
		const int row = index / 94;
		if (0xb0 + row > 0xfe)
			return false;
		hi = (byte)(0xb0 + row);
		lo = (byte)(0xa1 + index % 94);
		return true;
	}
	default:
		return false;
	}
}

/**
 * Where a code point sits in a font ordered by a legacy code page.
 *
 * This asks the shared decoder what each candidate pair of bytes means and
 * keeps the answer, rather than encoding the code point back to bytes. The
 * difference matters: encoding needs a reverse table that is built from
 * encoding.dat, and a build or install without that file would silently lose
 * every CJK glyph. Decoding a known block is table free for the ranges these
 * fonts cover, and the result is cached, so the cost is paid once per font.
 */
int HiResBitmapFont::legacyGlyphIndex(uint32 codepoint) const {
	if (_codePage == Common::kCodePageInvalid)
		return -1;

	// Single byte fonts are indexed by the byte itself.
	if (_codePage == Common::kISO8859_1 || _codePage == Common::kWindows1252)
		return (codepoint < 256) ? (int)codepoint : -1;

	// Fonts of this kind hold only the double byte block of their page: the
	// single byte range lives in a separate font, and index 0 of a Korean
	// font is a syllable, not a space. ASCII therefore has no glyph here and
	// must not be allowed to land on an unrelated one.
	if (codepoint < 0x80)
		return -1;

	if (_legacyMap.empty()) {
		// Build the code point to glyph map once, by decoding the bytes each
		// index stands for.
		for (int i = 0; i < _glyphs; ++i) {
			byte hi, lo;
			if (!legacyIndexToBytes(_codePage, i, hi, lo))
				continue;

			const char bytes[2] = { (char)hi, (char)lo };
			const Common::U32String decoded(Common::String(bytes, 2), _codePage);
			if (decoded.size() != 1)
				continue;

			const uint32 point = decoded[0];
			// A code page maps some byte pairs to a replacement character;
			// those are not glyphs anyone can ask for by code point.
			if (point && point != 0xFFFD && !_legacyMap.contains(point))
				_legacyMap[point] = i;
		}

		if (_legacyMap.empty()) {
			// Nothing decoded - the conversion tables are missing. Mark the
			// map as tried so this does not run again for every character.
			_legacyMap[0] = -1;
		}
	}

	Common::HashMap<uint32, int>::const_iterator it = _legacyMap.find(codepoint);
	return (it != _legacyMap.end()) ? it->_value : -1;
}

int HiResBitmapFont::glyphIndex(uint32 codepoint) const {
	if (!isLoaded())
		return -1;

	if (!_cmap.empty()) {
		Common::HashMap<uint32, int>::const_iterator it = _cmap.find(codepoint);
		return (it != _cmap.end()) ? it->_value : -1;
	}

	const int index = legacyGlyphIndex(codepoint);
	return (index >= 0 && index < _glyphs) ? index : -1;
}

bool HiResBitmapFont::glyphMetrics(int index, GlyphMetrics &out) const {
	if (!isLoaded() || index < 0 || index >= _glyphs)
		return false;

	out.bearingY = _ascent;
	out.height = _cellH;

	if (!_metrics) {
		// A font with no metrics table is drawn on a fixed grid.
		out.advance = _cellW;
		out.bearingX = 0;
		out.width = _cellW;
		return true;
	}

	const byte *entry = _metrics + (uint32)index * kMetricsEntrySize;
	out.advance = entry[0];
	out.bearingX = entry[1];
	out.width = entry[2];
	return true;
}

const byte *HiResBitmapFont::glyphData(int index) const {
	if (!isLoaded() || index < 0 || index >= _glyphs)
		return nullptr;

	return _pixels + (uint32)index * (uint32)_glyphStride;
}

} // End of namespace Graphics
