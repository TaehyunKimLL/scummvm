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

#ifndef GRAPHICS_FONTS_SVFFONT_H
#define GRAPHICS_FONTS_SVFFONT_H

#include "common/types.h"

#include "graphics/font.h"

namespace Graphics {

class SvfChain;

/**
 * A Graphics::Font over an SvfChain. Draws on CLUT8 surfaces by threshold (coverage >= 128 writes
 * the colour index) and blends over 1 (non-CLUT8), 2 and 4 byte formats. Non-ASCII text must be
 * passed as Common::U32String: the Common::String overloads of Font draw each byte as a code point.
 */
class SvfFont : public Font {
public:
	SvfFont(SvfChain *chain, DisposeAfterUse::Flag dispose);
	~SvfFont() override;

	int getFontHeight() const override;
	Common::String getFontName() const override;
	int getFontAscent() const override;
	int getFontDescent() const override;
	int getMaxCharWidth() const override;
	int getCharWidth(uint32 chr) const override;
	void drawChar(Surface *dst, uint32 chr, int x, int y, uint32 color) const override;

	SvfChain &getChain() const { return *_chain; }

	/** UTF-8 of @p cp in @p out; a surrogate or a value past U+10FFFF is the invalid byte 0xFF. */
	static uint encodeUtf8(uint32 cp, char out[4]);

private:
	SvfChain *_chain;
	DisposeAfterUse::Flag _dispose;
};

} // End of namespace Graphics

#endif
