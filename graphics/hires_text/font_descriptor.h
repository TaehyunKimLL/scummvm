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

#ifndef GRAPHICS_HIRES_TEXT_FONT_DESCRIPTOR_H
#define GRAPHICS_HIRES_TEXT_FONT_DESCRIPTOR_H

#include "common/str.h"

namespace Graphics {

/** Largest pixel size a font descriptor may ask for. */
enum { kFontDescriptorMaxPx = 128 };

/**
 * Parse a one-line font descriptor as translations ship them next to a game
 * font, e.g. Grim's "<font>.laf.txt": "<face file> <size>[px]".
 *
 * Blanks around the line (and a CR) are ignored; the face is everything
 * before the last blank, the size is a decimal integer with an optional
 * "px" suffix, in 1..kFontDescriptorMaxPx. On any other input it returns
 * false and leaves @p face and @p px untouched.
 */
bool parseFontDescriptor(const Common::String &line, Common::String &face, int &px);

} // End of namespace Graphics

#endif
