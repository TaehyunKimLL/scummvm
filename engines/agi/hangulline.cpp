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

#include "agi/hangulline.h"

namespace Agi {

void HangulLine::writeRun(uint32 *cells, int16 &len) const {
	const Common::U32String run = _hangul.text();
	for (uint i = 0; i < run.size(); i++)
		cells[_runStart + i] = run[i];
	len = _runStart + (int16)run.size();
	cells[len] = 0;
}

bool HangulLine::feed(char key, uint32 *cells, int16 &len, int16 maxCells) {
	if (!Common::HangulComposer::isJamoKey(key)) {
		commit(len);
		return false;
	}
	if (_hangul.empty())
		_runStart = len;
	const Common::HangulComposer before = _hangul;
	_hangul.feed(key);
	if (_runStart + (int16)_hangul.text().size() > maxCells) {
		_hangul = before;
		return true;
	}
	writeRun(cells, len);
	return true;
}

bool HangulLine::backspace(uint32 *cells, int16 &len) {
	if (_hangul.empty())
		return false;
	_hangul.backspace();
	writeRun(cells, len);
	if (_hangul.empty())
		_runStart = len;
	return true;
}

void HangulLine::commit(int16 len) {
	_hangul.flush();
	_hangul.reset();
	_runStart = len;
}

KoreanBadge koreanBadgeFor(bool hangulInput, bool hangulMode) {
	if (!hangulInput)
		return kKoreanBadgeNone;
	return hangulMode ? kKoreanBadgeKorean : kKoreanBadgeEnglish;
}

void updateKoreanBadge(Graphics::KoreanInputIndicator &badge, bool hangulInput, bool hangulMode) {
	const KoreanBadge b = koreanBadgeFor(hangulInput, hangulMode);
	if (b == kKoreanBadgeNone)
		badge.clear();
	else
		badge.show(b == kKoreanBadgeKorean);
}

} // End of namespace Agi
