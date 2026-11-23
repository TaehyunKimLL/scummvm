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

#include "graphics/fonts/svffont.h"

#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/surface.h"

namespace Graphics {

namespace {

struct SvfPlot {
	Surface *dst;
	uint32 color;
	bool threshold;
	uint8 a, r, g, b;
};

void plotPixel(void *ctx, int16_t x, int16_t y, uint8_t coverage) {
	const SvfPlot *p = (const SvfPlot *)ctx;
	void *pixel = p->dst->getBasePtr(x, y);
	if (p->threshold) {
		if (coverage >= 128)
			*(byte *)pixel = (byte)p->color;
		return;
	}

	const PixelFormat &format = p->dst->format;
	uint32 bg;
	if (format.bytesPerPixel == 1)
		bg = *(const byte *)pixel;
	else if (format.bytesPerPixel == 2)
		bg = *(const uint16 *)pixel;
	else
		bg = *(const uint32 *)pixel;
	uint32 out;
	if (format.aBits() > 1) {
		// A format with a real alpha: Porter-Duff over, as TTFFont::renderGlyph. The source alpha is the coverage times the
		// colour's alpha. Over a transparent pixel the text keeps its colour and gets the source alpha (white AA text stays
		// white); over an opaque one the alpha stays 255 and the colour is the blend by the source alpha.
		uint8 dA, dR, dG, dB;
		format.colorToARGB(bg, dA, dR, dG, dB);
		const uint sA = ((uint)coverage * p->a + 127) / 255;
		const uint oA = sA + ((uint)dA * (255 - sA) + 127) / 255;
		if (!oA)
			return;
		const uint den = oA * 255;
		const uint ch[3] = { p->r, p->g, p->b }, dc[3] = { dR, dG, dB };
		uint o[3];
		for (int i = 0; i < 3; ++i)
			o[i] = (ch[i] * sA * 255 + dc[i] * dA * (255 - sA) + den / 2) / den;
		out = format.ARGBToColor((uint8)oA, (uint8)o[0], (uint8)o[1], (uint8)o[2]);
	} else {
		// The same blend as the hi-res text sinks and the C renderer's RGB565/RGB555 targets (TextCompose::blendPixel); a 1-bit
		// alpha (ARGB1555) comes out opaque where there is text, also over a transparent background.
		out = TextCompose::blendPixel(format, bg, p->r, p->g, p->b, coverage);
	}
	if (format.bytesPerPixel == 1)
		*(byte *)pixel = (byte)out;
	else if (format.bytesPerPixel == 2)
		*(uint16 *)pixel = (uint16)out;
	else
		*(uint32 *)pixel = out;
}

} // End of anonymous namespace

SvfFont::SvfFont(SvfChain *chain, DisposeAfterUse::Flag dispose) : _chain(chain), _dispose(dispose) {
	assert(_chain);
}

SvfFont::~SvfFont() {
	if (_dispose == DisposeAfterUse::YES)
		delete _chain;
}

int SvfFont::getFontHeight() const {
	return _chain->getCellHeight();
}

Common::String SvfFont::getFontName() const {
	return _chain->getFaceCount() ? _chain->getFace(0)->getName() : Common::String();
}

int SvfFont::getFontAscent() const {
	return _chain->getAscent();
}

int SvfFont::getFontDescent() const {
	return _chain->getDescent();
}

int SvfFont::getMaxCharWidth() const {
	return _chain->getMaxCellWidth();
}

int SvfFont::getCharWidth(uint32 chr) const {
	char text[4];
	const uint len = encodeUtf8(chr, text);
	return _chain->getWidth(text, len);
}

void SvfFont::drawChar(Surface *dst, uint32 chr, int x, int y, uint32 color) const {
	if (!dst || !dst->getPixels() || dst->w <= 0 || dst->h <= 0)
		return;
	const PixelFormat &format = dst->format;
	if (format.bytesPerPixel != 1 && format.bytesPerPixel != 2 && format.bytesPerPixel != 4)
		return;

	SvfPlot p;
	p.dst = dst;
	p.color = color;
	p.threshold = format.isCLUT8();
	format.colorToARGB(color, p.a, p.r, p.g, p.b);

	char text[4];
	const uint len = encodeUtf8(chr, text);
	_chain->drawCoverage(text, len, x, y + _chain->getAscent(), dst->w, dst->h, plotPixel, &p);
}

uint SvfFont::encodeUtf8(uint32 cp, char out[4]) {
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
		out[0] = (char)0xFF;
		return 1;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

} // End of namespace Graphics
