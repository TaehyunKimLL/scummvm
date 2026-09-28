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

#ifndef SCI_GRAPHICS_FONTBANKED_H
#define SCI_GRAPHICS_FONTBANKED_H

#include "sci/graphics/scifont.h"

namespace Sci {

class ResourceManager;
class GfxScreen;

/**
 * Double-byte text drawn from banks of ordinary single-byte FONT resources:
 * one resource per lead byte, the trail byte as the glyph slot.
 *
 * This is how the Korean beta of Conquests of Camelot (SCI0) carries its
 * Hangul: a patched DOS interpreter draws an EUC-KR pair from font
 * (500 + lead - 0xB0), slot = trail byte - 25 resources for the KS X 1001
 * Hangul rows 0xB0..0xC8 - and from a second set at 525 for the game's
 * outline font 104 (the intro/ending draw text twice, outline under body).
 * The glyphs are the game's own 8x8 bitmaps, drawn at native resolution.
 *
 * Takes and draws packed pairs (lead byte low, trail byte high), as every
 * legacy double-byte face does (GfxFontSet::toEncodedPair()).
 */
class GfxFontBanked : public GfxFont {
public:
	/** The first bank of the regular set, and of the outline set for font 104. */
	enum {
		kBankBase = 500,
		kBankOutlineBase = 525,
		kOutlineFontId = 104,
		kLeadFirst = 0xB0,
		kLeadLast = 0xC8
	};

	GfxFontBanked(ResourceManager *resMan, GfxScreen *screen, GuiResourceId fontId, int bankBase);
	~GfxFontBanked() override;

	/**
	 * The first bank for @p fontId, or -1 when the game carries no such
	 * banks: every one of the 25 resources must exist and hold a glyph for
	 * every trail byte (at least 0xFF characters).
	 */
	static int bankBaseFor(ResourceManager *resMan, GuiResourceId fontId);

	GuiResourceId getResourceId() override { return _fontId; }
	byte getHeight() override;
	bool isDoubleByte(uint32 chr) override;
	byte getCharWidth(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;

private:
	GfxFont *bank(GuiResourceId id);

	ResourceManager *_resMan;
	GfxScreen *_screen;
	GuiResourceId _fontId;
	int _bankBase;
	GfxFont *_banks[kLeadLast - kLeadFirst + 1];
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_FONTBANKED_H
