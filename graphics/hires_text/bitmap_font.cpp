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

#include "common/algorithm.h"
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
// code page's order. The offset of that table does not fit in the 32 byte
// header - byte 16 is the ascent - so a version 2 header is four bytes longer.
static const int kHeaderSizeV2 = 36;
static const int kCmapOffField = 32;
static const uint16 kMaxVersion = 2;

enum {
	kFlagProportional = 1 << 0,
	// Bit 1 (glyphs are jamo, composed at run time) is reserved.
	kFlagMarksAtOrigin = 1 << 2
};

// Bytes per entry in the metrics table: advance, left bearing, ink width, and
// one still unused.
static const int kMetricsEntrySize = 4;

// Bytes per entry in the code point table of a version 2 font.
static const int kCmapEntrySize = 8;

HiResBitmapFont::HiResBitmapFont() {
	_cmapTable = nullptr;
	_cmapEntries = 0;
	clearLookupCache();
	_data = nullptr;
	_loaded = false;
	_stream = nullptr;
	_disposeStream = DisposeAfterUse::NO;
	_dataOff = 0;
	_glyphBufIndex = -1;
	_pixels = nullptr;
	_metrics = nullptr;
	_bpp = 0;
	_cellW = 0;
	_cellH = 0;
	_ascent = 0;
	_marksAtOrigin = false;
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
	_loaded = false;
	if (_disposeStream == DisposeAfterUse::YES)
		delete _stream;
	_stream = nullptr;
	_disposeStream = DisposeAfterUse::NO;
	_dataOff = 0;
	_glyphBuf.clear();
	_glyphBufIndex = -1;
	_pixels = nullptr;
	_metrics = nullptr;
	_bpp = 0;
	_cellW = 0;
	_cellH = 0;
	_ascent = 0;
	_marksAtOrigin = false;
	_glyphs = 0;
	_rowPitch = 0;
	_glyphStride = 0;
	_codePage = Common::kCodePageInvalid;
	_cmapTable = nullptr;
	_cmapEntries = 0;
	_cmapOrder.clear();
	clearLookupCache();
	_legacyMap.clear();
}

HiResBitmapFont::CmapEntryLess::CmapEntryLess(const byte *table) : _table(table) {}

bool HiResBitmapFont::CmapEntryLess::operator()(uint16 a, uint16 b) const {
	const uint32 ca = READ_LE_UINT32(_table + (uint32)a * kCmapEntrySize);
	const uint32 cb = READ_LE_UINT32(_table + (uint32)b * kCmapEntrySize);
	return ca < cb || (ca == cb && a < b);
}

void HiResBitmapFont::clearLookupCache() {
	for (int i = 0; i < kLookupCacheSize; ++i)
		_lookupCache[i].valid = false;
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

	Layout layout;
	if (!readLayout(raw, size, size, layout)) {
		delete[] raw;
		return false;
	}

	const byte *metrics = layout.metricsOk ? raw + layout.metricsOff : nullptr;
	const byte *cmapTable = layout.version >= 2 ? raw + layout.cmapOff : nullptr;
	Common::Array<uint16> cmapOrder;
	if (cmapTable && !checkCodePointTable(cmapTable, layout.glyphs, cmapOrder)) {
		delete[] raw;
		return false;
	}

	_data = raw;
	_pixels = raw + layout.dataOff;
	adopt(layout, metrics, cmapTable, cmapOrder);
	return true;
}

bool HiResBitmapFont::loadStreamed(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose,
								   uint32 sizeLimit) {
	free();
	if (!stream)
		return false;

	const int64 size64 = stream->size();
	byte head[kHeaderSizeV2];
	const uint32 headSize = (size64 >= kHeaderSizeV2) ? kHeaderSizeV2 : kHeaderSize;
	if (size64 < kHeaderSize || (uint64)size64 > sizeLimit || !stream->seek(0) ||
		stream->read(head, headSize) != headSize) {
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return false;
	}

	Layout layout;
	if (!readLayout(head, headSize, (uint32)size64, layout)) {
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return false;
	}

	// Only the tables are kept: the metrics (4 bytes a glyph) and the code
	// point table (8), one block. The glyphs stay in the file.
	const uint32 metricsSize = layout.metricsOk ? (uint32)layout.glyphs * kMetricsEntrySize : 0;
	const uint32 cmapSize = layout.version >= 2 ? (uint32)layout.glyphs * kCmapEntrySize : 0;
	byte *tables = (metricsSize + cmapSize) ? new byte[metricsSize + cmapSize] : nullptr;
	bool ok = true;
	if (metricsSize)
		ok = stream->seek(layout.metricsOff) && stream->read(tables, metricsSize) == metricsSize;
	if (ok && cmapSize)
		ok = stream->seek(layout.cmapOff) && stream->read(tables + metricsSize, cmapSize) == cmapSize;

	Common::Array<uint16> cmapOrder;
	const byte *cmapTable = cmapSize ? tables + metricsSize : nullptr;
	if (ok && cmapTable)
		ok = checkCodePointTable(cmapTable, layout.glyphs, cmapOrder);
	if (!ok) {
		delete[] tables;
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return false;
	}

	_data = tables;
	_stream = stream;
	_disposeStream = dispose;
	_dataOff = layout.dataOff;
	_glyphBuf.resize(layout.glyphStride);
	_glyphBufIndex = -1;
	adopt(layout, metricsSize ? tables : nullptr, cmapTable, cmapOrder);
	return true;
}

bool HiResBitmapFont::readLayout(const byte *raw, uint32 have, uint32 size, Layout &out) {
	if (have < (uint32)kHeaderSize)
		return false;

	if (READ_BE_UINT32(raw) != kMagic) {
		// Not this format. The caller can still try to read it as one of the
		// engine's own fonts, so this is not worth a warning.
		return false;
	}

	const uint16 version = READ_LE_UINT16(raw + 4);
	if (version < 1 || version > kMaxVersion) {
		warning("HiResText: font version %d is not supported by this build", version);
		return false;
	}
	if (version >= 2 && (size < (uint32)kHeaderSizeV2 || have < (uint32)kHeaderSizeV2)) {
		warning("HiResText: version 2 font is too short for its header");
		return false;
	}

	const uint16 flags = READ_LE_UINT16(raw + 6);
	const int bpp = raw[8];
	const int glyphs = READ_LE_UINT16(raw + 12);
	const int cellW = raw[14];
	const int cellH = raw[15];
	const uint32 metricsOff = READ_LE_UINT32(raw + 20);
	const uint32 dataOff = READ_LE_UINT32(raw + 24);
	const uint32 dataSize = READ_LE_UINT32(raw + 28);

	if (bpp != 1 && bpp != 2 && bpp != 8) {
		warning("HiResText: font has unsupported depth %d", bpp);
		return false;
	}
	if (cellW <= 0 || cellH <= 0 || glyphs <= 0) {
		warning("HiResText: font has an empty glyph box");
		return false;
	}

	// Rows are packed MSB first and padded to a byte: at 2bpp four pixels a
	// byte, the leftmost in the top two bits (TextCompose::expandCoverage()).
	const int rowPitch = (bpp == 1) ? (cellW + 7) / 8 : (bpp == 2) ? (cellW + 3) / 4 : cellW;
	const uint32 glyphStride = (uint32)rowPitch * (uint32)cellH;

	// Everything the header points at has to lie inside the file. These are
	// files a translation ships, so a truncated or hand-edited one must fail
	// the load rather than read past the buffer.
	if (dataOff > size || dataSize > size - dataOff) {
		warning("HiResText: font glyph data runs past the end of the file");
		return false;
	}
	if (glyphStride > dataSize / (uint32)glyphs) {
		warning("HiResText: font declares %d glyphs but holds room for fewer", glyphs);
		return false;
	}

	bool metricsOk = false;
	if (flags & kFlagProportional) {
		const uint32 metricsSize = (uint32)glyphs * kMetricsEntrySize;
		if (metricsOff > size || metricsSize > size - metricsOff) {
			// The glyphs themselves are still usable; only the advances are
			// lost, and the cell width is a sane stand-in for them.
			warning("HiResText: font metrics table runs past the end of the file");
		} else {
			metricsOk = true;
		}
	}

	uint32 cmapOff = 0;
	if (version >= 2) {
		cmapOff = READ_LE_UINT32(raw + kCmapOffField);
		const uint32 cmapSize = (uint32)glyphs * kCmapEntrySize;
		if (cmapOff == 0 || cmapOff > size || cmapSize > size - cmapOff) {
			warning("HiResText: font code point table runs past the end of the file");
			return false;
		}
	}

	out.version = version;
	out.flags = flags;
	out.bpp = bpp;
	out.codePage = READ_LE_UINT16(raw + 10);
	out.glyphs = glyphs;
	out.cellW = cellW;
	out.cellH = cellH;
	out.ascent = raw[16];
	out.rowPitch = rowPitch;
	out.glyphStride = glyphStride;
	out.metricsOff = metricsOff;
	out.metricsOk = metricsOk;
	out.dataOff = dataOff;
	out.cmapOff = cmapOff;
	return true;
}

bool HiResBitmapFont::checkCodePointTable(const byte *table, int glyphs, Common::Array<uint16> &order) {
	// A version 2 font carries its own code point table, so it does not have
	// to follow the order of any code page. It is searched where it is, by
	// binary search; a table not listed in code point order gets an order of
	// two bytes an entry, instead of a hash map of its entries (some 30 bytes
	// each on a 32-bit machine).
	bool sorted = true;
	for (int i = 0; i < glyphs; ++i) {
		const byte *entry = table + (uint32)i * kCmapEntrySize;
		const uint32 codepoint = READ_LE_UINT32(entry);
		const uint32 index = READ_LE_UINT32(entry + 4);
		if (index >= (uint32)glyphs) {
			warning("HiResText: font maps U+%04X to a glyph it does not have", codepoint);
			return false;
		}
		if (i > 0 && codepoint <= READ_LE_UINT32(entry - kCmapEntrySize))
			sorted = false;
	}

	order.clear();
	if (!sorted) {
		order.resize(glyphs);
		for (int i = 0; i < glyphs; ++i)
			order[i] = (uint16)i;
		Common::sort(order.begin(), order.end(), CmapEntryLess(table));
	}
	return true;
}

void HiResBitmapFont::adopt(const Layout &layout, const byte *metrics, const byte *cmapTable,
							Common::Array<uint16> &cmapOrder) {
	_loaded = true;
	_metrics = metrics;
	_bpp = layout.bpp;
	_cellW = layout.cellW;
	_cellH = layout.cellH;
	_ascent = layout.ascent;
	_marksAtOrigin = (layout.flags & kFlagMarksAtOrigin) != 0;
	_glyphs = layout.glyphs;
	_rowPitch = layout.rowPitch;
	_glyphStride = (int)layout.glyphStride;
	_cmapTable = cmapTable;
	_cmapEntries = cmapTable ? layout.glyphs : 0;
	_cmapOrder.swap(cmapOrder);
	clearLookupCache();
	_legacyMap.clear();

	const uint16 codePage = layout.codePage;
	if (layout.version >= 2) {
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

int HiResBitmapFont::searchCodePointTable(uint32 codepoint) const {
	// Text repeats its letters, so most lookups are answered here.
	LookupHit &hit = _lookupCache[codepoint & (kLookupCacheSize - 1)];
	if (hit.valid && hit.codepoint == codepoint)
		return hit.index;

	// The first entry at or above the code point, in code point order.
	const bool ordered = !_cmapOrder.empty();
	int lo = 0, hi = _cmapEntries;
	while (lo < hi) {
		const int mid = lo + (hi - lo) / 2;
		const uint32 at = ordered ? _cmapOrder[mid] : (uint32)mid;
		if (READ_LE_UINT32(_cmapTable + at * kCmapEntrySize) < codepoint)
			lo = mid + 1;
		else
			hi = mid;
	}

	// A code point listed more than once maps to its last entry in the file
	// (ties keep file order), as the map built from the table used to.
	int index = -1;
	for (int k = lo; k < _cmapEntries; ++k) {
		const uint32 at = ordered ? _cmapOrder[k] : (uint32)k;
		const byte *entry = _cmapTable + at * kCmapEntrySize;
		if (READ_LE_UINT32(entry) != codepoint)
			break;
		index = (int)READ_LE_UINT32(entry + 4);
	}

	hit.codepoint = codepoint;
	hit.index = index;
	hit.valid = true;
	return index;
}

int HiResBitmapFont::glyphIndex(uint32 codepoint) const {
	if (!isLoaded())
		return -1;

	if (_cmapTable)
		return searchCodePointTable(codepoint);

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

	if (_pixels)
		return _pixels + (uint32)index * (uint32)_glyphStride;

	// Streamed: the glyph is read into the one buffer the font keeps.
	if (index == _glyphBufIndex)
		return _glyphBuf.begin();
	_glyphBufIndex = -1;
	if (!_stream->seek(_dataOff + (uint32)index * (uint32)_glyphStride) ||
		_stream->read(_glyphBuf.begin(), _glyphBuf.size()) != _glyphBuf.size()) {
		_stream->clearErr();
		return nullptr;
	}
	_glyphBufIndex = index;
	return _glyphBuf.begin();
}

} // End of namespace Graphics
