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

#include "common/system.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/korean_badge.h"

namespace Graphics {

Graphics::Surface KoreanInputIndicator::renderBadge(bool hangulEnabled) {
	// Sized so the label fits the smallest GUI font with a margin, and kept
	// the SAME size for both states: a badge that resizes when the mode
	// changes drags the eye to the edge that moved rather than to the label.
	const int16 width = 34;
	const int16 height = 20;

	Graphics::Surface badge;
	badge.create(width, height, Graphics::PixelFormat::createFormatARGB32());

	const uint32 body = hangulEnabled
		? badge.format.ARGBToColor(0xFF, kKoreanR, kKoreanG, kKoreanB)
		: badge.format.ARGBToColor(0xFF, kEnglishR, kEnglishG, kEnglishB);
	const uint32 border = badge.format.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF);

	badge.fillRect(Common::Rect(0, 0, width, height), border);
	badge.fillRect(Common::Rect(1, 1, width - 1, height - 1), body);

	// ASCII, deliberately: the OSD draws with the GUI theme font, and a build
	// without a scalable font has no Hangul in it - a probe posting 한글 to
	// this same path drew two empty boxes while the ASCII beside it rendered
	// (harness/s5probe3.sh). "KO"/"EN" is legible in every build.
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kGUIFont);
	if (font) {
		const Common::String label = hangulEnabled ? "KO" : "EN";
		const int y = (height - font->getFontHeight()) / 2;
		font->drawString(&badge, label, 0, y, width,
		                 badge.format.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF),
		                 Graphics::kTextAlignCenter);
	}

	return badge;
}

void KoreanInputIndicator::show(bool hangulEnabled) {
	// The caller is the event pump, so this is reached at poll rate - about
	// 1,550 times in a 40-second session, measured. Rebuilding the surface
	// each time would re-upload a texture per poll for no visible change.
	if (_shown && _shownEnabled == hangulEnabled)
		return;

	Graphics::Surface badge = renderBadge(hangulEnabled);
	g_system->displayActivityIconOnOSD(&badge);
	badge.free();

	_shown = true;
	_shownEnabled = hangulEnabled;
}

void KoreanInputIndicator::clear() {
	if (!_shown)
		return;

	g_system->displayActivityIconOnOSD(nullptr);
	_shown = false;
}

} // End of namespace Graphics
