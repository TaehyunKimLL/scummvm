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

#include "sci/graphics/glyphsource_routed.h"

namespace Sci {

RoutedGlyphSource::RoutedGlyphSource(UnicodeGlyphSource *main, UnicodeGlyphSource *latin, LatinMode mode,
									 DisposeAfterUse::Flag dispose)
	: _main(main), _latin(latin), _mode(mode), _dispose(dispose) {
}

RoutedGlyphSource::~RoutedGlyphSource() {
	if (_dispose == DisposeAfterUse::YES) {
		delete _main;
		delete _latin;
	}
}

bool RoutedGlyphSource::routeToLatin(uint32 cp) const {
	// kLatinProportional routes the same plain ASCII as kLatinHalf.
	if (_mode == kLatinHalf || _mode == kLatinProportional)
		return cp >= 0x0020 && cp <= 0x007E;
	// kLatinFullwidth: the only other mode this class is ever constructed
	// with (see the class comment in glyphsource_routed.h). Latin-1
	// (U+00A0..U+00FF) is deliberately not routed in either mode: it is not
	// remapped (the fullwidth-forms block has no counterpart for it), and it
	// keeps the face it had before hires_text_latin existed.
	return (cp >= 0xFF01 && cp <= 0xFF5E) || cp == 0x3000;
}

} // End of namespace Sci
