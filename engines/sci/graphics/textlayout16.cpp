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

#include "sci/graphics/textlayout16.h"

#include "graphics/hires_text/latin_advance.h"

namespace Sci {

bool hiresTextApplies(SciVersion v, Common::CodePage page, bool utf8Translation, Common::String &why) {
	if (v >= SCI_VERSION_2) {
		why = "SCI32 games do not support it yet";
		return false;
	}
	if (utf8Translation)
		return true;
	switch (page) {
	case Common::kWindows949:
	case Common::kWindows932:
	case Common::kWindows936:
	case Common::kWindows950:
		return true;
	default:
		why = "no translation and no CJK code page";
		return false;
	}
}

int16 gameAdvance(const Graphics::GlyphMetrics &m, int gameNarrow, int gameWide, int scale) {
	if (m.combining)
		return 0;
	if (m.wide)
		return (int16)gameWide;
	return (int16)Graphics::latinAdvanceGamePx(Graphics::kHiResMetricsFont, gameNarrow, m.advance, scale);
}

} // End of namespace Sci
