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

#include "sci/graphics/fontbanked.h"

#include "sci/resource/resource.h"
#include "sci/graphics/screen.h"
#include "sci/utf8.h"

namespace Sci {

GfxFontBanked::GfxFontBanked(ResourceManager *resMan, GfxScreen *screen, GuiResourceId fontId, int bankBase)
	: _resMan(resMan), _screen(screen), _fontId(fontId), _bankBase(bankBase) {
	for (uint i = 0; i < ARRAYSIZE(_banks); i++)
		_banks[i] = nullptr;
}

GfxFontBanked::~GfxFontBanked() {
	for (uint i = 0; i < ARRAYSIZE(_banks); i++)
		delete _banks[i];
}

int GfxFontBanked::bankBaseFor(ResourceManager *resMan, GuiResourceId fontId) {
	static const int bases[2] = { kBankOutlineBase, kBankBase };
	for (int b = 0; b < 2; b++) {
		const int base = bases[b];
		if (base == kBankOutlineBase && fontId != kOutlineFontId)
			continue;
		bool complete = true;
		for (int lead = kLeadFirst; lead <= kLeadLast && complete; lead++) {
			Resource *res = resMan->findResource(ResourceId(kResourceTypeFont, base + lead - kLeadFirst), false);
			// The SCI0/1 font header: u16 lowest char, u16 char count, u16 height.
			if (!res || res->size() < 6 || res->getUint16LEAt(2) < 0xFF)
				complete = false;
		}
		if (complete)
			return base;
	}
	return -1;
}

GfxFont *GfxFontBanked::bank(GuiResourceId id) {
	const int idx = id - _bankBase;
	if (idx < 0 || idx >= (int)ARRAYSIZE(_banks))
		return nullptr;
	if (!_banks[idx])
		_banks[idx] = new GfxFontFromResource(_resMan, _screen, id);
	return _banks[idx];
}

byte GfxFontBanked::getHeight() {
	GfxFont *f = bank(_bankBase);
	return f ? f->getHeight() : 0;
}

bool GfxFontBanked::isDoubleByte(uint32 chr) {
	const byte b = chr & 0xFF;
	return b >= 0x81 && b <= 0xFE;
}

byte GfxFontBanked::getCharWidth(uint32 chr) {
	int id;
	byte slot;
	if (!koreanBankAndSlot(chr, _bankBase, id, slot))
		return 0;
	GfxFont *f = bank(id);
	return f ? f->getCharWidth(slot) : 0;
}

void GfxFontBanked::draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) {
	int id;
	byte slot;
	if (!koreanBankAndSlot(chr, _bankBase, id, slot))
		return;
	if (GfxFont *f = bank(id))
		f->draw(slot, top, left, color, greyedOutput);
}

} // End of namespace Sci
