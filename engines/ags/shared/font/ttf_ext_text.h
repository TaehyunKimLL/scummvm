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


#ifndef AGS_SHARED_FONT_TTF_EXT_TEXT_H
#define AGS_SHARED_FONT_TTF_EXT_TEXT_H

#include "common/str.h"

namespace AGS3 {

/**
 * The chain "game TTF -> extfntN.wfn" of TTFFontRenderer (a Korean patch's
 * Hangul extension behind a TTF font), free of alfont and the engine's
 * globals so the unit tests run it. Runs of characters the face draws are
 * measured and drawn whole (kerning); a character the extension draws
 * instead goes glyph by glyph. Ops:
 *   int getxc(const char **s)        Allegro's ugetxc
 *   bool useExt(int cp)              the face lacks cp and the extension has it
 *   int faceWidth(const char *run)   alfont_text_length()
 *   int extWidth(int cp)             the extension glyph's advance
 *   void drawFace(const char *run, int x)
 *   void drawExt(int cp, int x)
 */
template<class Ops>
bool ttf_ext_has_chars(const char *text, Ops &ops) {
	for (int cp = ops.getxc(&text); cp; cp = ops.getxc(&text)) {
		if (ops.useExt(cp))
			return true;
	}
	return false;
}

template<class Ops>
int ttf_ext_text_width(const char *text, Ops &ops) {
	int width = 0;
	Common::String run;
	const char *p = text;
	for (;;) {
		const char *at = p;
		const int cp = ops.getxc(&p);
		if (cp == 0 || ops.useExt(cp)) {
			if (!run.empty())
				width += ops.faceWidth(run.c_str());
			run.clear();
			if (cp == 0)
				break;
			width += ops.extWidth(cp);
		} else {
			run += Common::String(at, p - at);
		}
	}
	return width;
}

template<class Ops>
void ttf_ext_render_text(const char *text, int x, Ops &ops) {
	Common::String run;
	const char *p = text;
	for (;;) {
		const char *at = p;
		const int cp = ops.getxc(&p);
		if (cp == 0 || ops.useExt(cp)) {
			if (!run.empty()) {
				ops.drawFace(run.c_str(), x);
				x += ops.faceWidth(run.c_str());
			}
			run.clear();
			if (cp == 0)
				break;
			ops.drawExt(cp, x);
			x += ops.extWidth(cp);
		} else {
			run += Common::String(at, p - at);
		}
	}
}

} // namespace AGS3

#endif
