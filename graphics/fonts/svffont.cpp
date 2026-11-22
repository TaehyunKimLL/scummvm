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
#include "graphics/surface.h"

namespace Graphics {

SvfFont::SvfFont(SvfChain *chain, DisposeAfterUse::Flag dispose) : _chain(chain), _dispose(dispose) {
	assert(_chain);
}

SvfFont::~SvfFont() {
	if (_dispose == DisposeAfterUse::YES)
		delete _chain;
}

int SvfFont::getFontHeight() const {
	return _chain->getAscent() + _chain->getDescent();
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
