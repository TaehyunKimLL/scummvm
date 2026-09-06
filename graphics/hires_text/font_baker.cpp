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

#include "graphics/hires_text/font_baker.h"

#ifdef USE_FREETYPE2

#include "common/endian.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/textconsole.h"
#include "common/ustr.h"
#include "graphics/font.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"

namespace Graphics {

// The same layout HiResBitmapFont reads. Kept in step by the round-trip test.
static const int kHeaderSizeV2 = 36;
static const int kMetricsEntrySize = 4;
static const int kCmapEntrySize = 8;

static void put16(Common::Array<byte> &b, uint at, uint16 v) {
	WRITE_LE_UINT16(&b[at], v);
}

static void put32(Common::Array<byte> &b, uint at, uint32 v) {
	WRITE_LE_UINT32(&b[at], v);
}

static bool faceHasGlyph(const Font &face, uint32 cp) {
	// A face reports a zero advance and an empty box for a code point it
	// does not cover. Space has an empty box but a real advance.
	return face.getCharWidth(cp) > 0 || !face.getBoundingBox(cp).isEmpty();
}

bool HiResFontBaker::bake(const Font &face, const Common::Array<uint32> &codepoints,
						  int cellW, int cellH, bool proportional,
						  Common::Array<byte> &out) {
	out.clear();

	if (cellH <= 0)
		cellH = face.getFontHeight();
	if (cellW <= 0)
		cellW = face.getMaxCharWidth();
	if (cellW <= 0 || cellH <= 0 || cellW > 255 || cellH > 255) {
		warning("HiResText: cannot bake a %dx%d cell", cellW, cellH);
		return false;
	}

	// Ink above the ascent or below the cell is clipped, as the offline
	// tool does; a cell is a cell.
	const int ascent = MIN(face.getFontAscent(), cellH);

	Common::Array<uint32> kept;
	kept.reserve(codepoints.size());
	for (uint i = 0; i < codepoints.size(); ++i) {
		if (faceHasGlyph(face, codepoints[i]))
			kept.push_back(codepoints[i]);
	}
	if (kept.empty())
		return false;
	if (kept.size() > 0xFFFF) {
		warning("HiResText: a baked font holds at most 65535 glyphs");
		return false;
	}

	const int glyphs = (int)kept.size();
	const uint32 glyphStride = (uint32)cellW * (uint32)cellH;
	const uint32 cmapOff = kHeaderSizeV2;
	const uint32 metricsOff = proportional ? cmapOff + (uint32)glyphs * kCmapEntrySize : 0;
	const uint32 dataOff = (proportional ? metricsOff + (uint32)glyphs * kMetricsEntrySize
										 : cmapOff + (uint32)glyphs * kCmapEntrySize);
	const uint32 dataSize = glyphStride * (uint32)glyphs;

	out.resize(dataOff + dataSize);
	for (uint i = 0; i < out.size(); ++i)
		out[i] = 0;

	out[0] = 'S'; out[1] = 'V'; out[2] = 'F'; out[3] = 'N';
	put16(out, 4, 2);                      // version
	put16(out, 6, proportional ? 1 : 0);   // flags
	out[8] = 8;                            // bpp
	put16(out, 10, 0);                     // code page: none, cmap instead
	put16(out, 12, (uint16)glyphs);
	out[14] = (byte)cellW;
	out[15] = (byte)cellH;
	out[16] = (byte)ascent;
	put32(out, 20, metricsOff);
	put32(out, 24, dataOff);
	put32(out, 28, dataSize);
	put32(out, 32, cmapOff);

	// One scratch, reused: the wrapper writes coverage into an alpha
	// channel, so rasterise in white and read the alpha back.
	const PixelFormat fmt(4, 8, 8, 8, 8, 24, 16, 8, 0);
	Surface raster;
	raster.create(cellW, cellH, fmt);
	const uint32 white = fmt.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF);

	for (int g = 0; g < glyphs; ++g) {
		const uint32 cp = kept[g];
		put32(out, cmapOff + (uint32)g * kCmapEntrySize, cp);
		put32(out, cmapOff + (uint32)g * kCmapEntrySize + 4, (uint32)g);

		const int advance = face.getCharWidth(cp);
		const Common::Rect box = face.getBoundingBox(cp);

		// Centre a glyph narrower than the cell when it has to sit on a fixed
		// grid; otherwise draw it at its own bearing.
		int penX = 0;
		if (!proportional && advance < cellW)
			penX = (cellW - advance) / 2;

		raster.fillRect(Common::Rect(0, 0, cellW, cellH), 0);
		// drawAlphaChar places the glyph's top at y for this wrapper, with the
		// baseline at y + ascent; ScummVM's TTF font draws from the line top.
		face.drawAlphaChar(&raster, cp, penX, 0, white);

		byte *dst = &out[dataOff + (uint32)g * glyphStride];
		for (int y = 0; y < cellH; ++y) {
			const uint32 *src = (const uint32 *)raster.getBasePtr(0, y);
			for (int x = 0; x < cellW; ++x) {
				byte a, r, gg, b;
				fmt.colorToARGB(src[x], a, r, gg, b);
				dst[y * cellW + x] = a;
			}
		}

		if (proportional) {
			byte *m = &out[metricsOff + (uint32)g * kMetricsEntrySize];
			m[0] = (byte)CLIP(advance, 0, 255);
			const int bearing = CLIP((int)box.left + penX, -128, 127);
			m[1] = (byte)(int8)bearing;
			m[2] = (byte)CLIP((int)box.width(), 0, 255);
			m[3] = 0;
		}
	}

	raster.free();
	return true;
}

void HiResFontBaker::hangulSyllables(Common::Array<uint32> &out) {
	// KS X 1001 rows 0xB0..0xC8, 94 syllables to a row from 0xA1.
	for (int row = 0; row < 25; ++row) {
		for (int col = 0; col < 94; ++col) {
			const char bytes[2] = { (char)(0xB0 + row), (char)(0xA1 + col) };
			const Common::U32String u(Common::String(bytes, 2), Common::kWindows949);
			if (u.size() == 1 && u[0] != 0xFFFD)
				out.push_back(u[0]);
		}
	}
}

void HiResFontBaker::jisX0208(Common::Array<uint32> &out) {
	for (int lead = 0x81; lead <= 0xFC; ++lead) {
		if (lead > 0x9F && lead < 0xE0)
			continue;
		for (int trail = 0x40; trail <= 0xFC; ++trail) {
			if (trail == 0x7F)
				continue;
			const char bytes[2] = { (char)lead, (char)trail };
			const Common::U32String u(Common::String(bytes, 2), Common::kWindows932);
			if (u.size() == 1 && u[0] != 0xFFFD && u[0] > 0x7F)
				out.push_back(u[0]);
		}
	}
}

void HiResFontBaker::chineseCodePage(uint codePage, Common::Array<uint32> &out) {
	const Common::CodePage page = (codePage == 950) ? Common::kWindows950 : Common::kWindows936;
	for (int lead = 0x81; lead <= 0xFE; ++lead) {
		for (int trail = 0x40; trail <= 0xFE; ++trail) {
			if (trail == 0x7F)
				continue;
			const char bytes[2] = { (char)lead, (char)trail };
			const Common::U32String u(Common::String(bytes, 2), page);
			if (u.size() == 1 && u[0] != 0xFFFD && u[0] > 0x7F)
				out.push_back(u[0]);
		}
	}
}

void HiResFontBaker::latin1(Common::Array<uint32> &out) {
	for (uint32 cp = 0x20; cp < 0x7F; ++cp)
		out.push_back(cp);
	for (uint32 cp = 0xA0; cp <= 0xFF; ++cp)
		out.push_back(cp);
}

} // End of namespace Graphics

#endif // USE_FREETYPE2
