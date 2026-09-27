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

#ifndef SCI_GRAPHICS_TEXTLAYOUT16_H
#define SCI_GRAPHICS_TEXTLAYOUT16_H

#include "common/array.h"
#include "common/str.h"
#include "common/str-enc.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/text_layout.h"
#include "sci/detection.h"

namespace Sci {

/**
 * The language-neutral pieces of SCI16 hi-res text (I18N_TEXT_DESIGN.md
 * sections 4.1-4.3), kept free of engine state (no g_sci, no ConfMan) so
 * they are tested alone. GfxCache, GfxFontUnicode and GfxText16 call them
 * with what they know about the running game.
 */

/**
 * Whether the hi-res text keys (hires_text_font, _latin*, hires_text.map)
 * apply: an SCI16 game whose text is a UTF-8 translation, or one in a legacy
 * CJK code page (949, 932, 936, 950). The language of the game is not asked.
 * On false, @p why says which half failed.
 */
bool hiresTextApplies(SciVersion v, Common::CodePage page, bool utf8Translation, Common::String &why);

/**
 * The advance of a glyph drawn on the hi-res plane, in game px (design
 * section 4.2): a wide glyph keeps the cell rule (@p gameWide, as before, so
 * Hangul and kanji keep their grid); a combining mark does not advance; any
 * other glyph advances by the face's own advance, latinAdvanceGamePx()
 * (metrics=font) at @p scale hi-res px per game px, @p gameNarrow when the
 * face cannot say. GfxFontUnicode::gameAdvance() is this.
 */
int16 gameAdvance(const Graphics::GlyphMetrics &m, int gameNarrow, int gameWide, int scale);

} // End of namespace Sci

#endif // SCI_GRAPHICS_TEXTLAYOUT16_H
