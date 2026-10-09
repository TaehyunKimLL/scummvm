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

#ifndef SCI_GRAPHICS_CONTROLS16_H
#define SCI_GRAPHICS_CONTROLS16_H

#include "sci/graphics/koreaninput.h"

namespace Sci {

enum controlStateFlags {
	SCI_CONTROLS_STYLE_ENABLED      = 0x0001,  ///< enabled buttons
	SCI_CONTROLS_STYLE_DISABLED     = 0x0004,  ///< grayed out buttons
	SCI_CONTROLS_STYLE_SELECTED     = 0x0008,  ///< widgets surrounded by a frame
	SCI_CONTROLS_STYLE_MAC_INVERTED = 0x0040   ///< control is inverted (mac-only for hi-res fonts)
};

// Control types and flags
enum {
	SCI_CONTROLS_TYPE_BUTTON		= 1,
	SCI_CONTROLS_TYPE_TEXT			= 2,
	SCI_CONTROLS_TYPE_TEXTEDIT		= 3,
	SCI_CONTROLS_TYPE_ICON			= 4,
	SCI_CONTROLS_TYPE_LIST			= 6,
	SCI_CONTROLS_TYPE_LIST_ALIAS	= 7,
	SCI_CONTROLS_TYPE_DUMMY			= 10
};

class GfxPorts;
class GfxPaint16;
class Font;
class GfxText16;
class GfxScreen;
/**
 * Controls class, handles drawing of controls in SCI16 (SCI0-SCI1.1) games
 */
class GfxControls16 {
public:
	GfxControls16(SegManager *segMan, GfxPorts *ports, GfxPaint16 *paint16, GfxText16 *text16, GfxScreen *screen);
	~GfxControls16();

	void kernelDrawButton(Common::Rect rect, reg_t obj, const char *text, uint16 languageSplitter, int16 fontId, int16 style, bool hilite);
	void kernelDrawText(Common::Rect rect, reg_t obj, const char *text, uint16 languageSplitter, int16 fontId, TextAlignment alignment, int16 style, bool hilite);
	void kernelDrawTextEdit(Common::Rect rect, reg_t obj, const char *text, uint16 languageSplitter, int16 fontId, int16 mode, int16 style, int16 cursorPos, int16 maxChars, bool hilite);
	void kernelDrawIcon(Common::Rect rect, reg_t obj, GuiResourceId viewId, int16 loopNo, int16 celNo, int16 priority, int16 style, bool hilite);
	void kernelDrawList(Common::Rect rect, reg_t obj, int16 maxChars, int16 count, const Common::String *entries, GuiResourceId fontId, int16 style, int16 upperPos, int16 cursorPos, bool isAlias, bool hilite);
	void kernelTexteditChange(reg_t controlObject, reg_t eventObject);

private:
	void texteditSetBlinkTime();

	void drawListControl(Common::Rect rect, reg_t obj, int16 maxChars, int16 count, const Common::String *entries, GuiResourceId fontId, int16 upperPos, int16 cursorPos, bool isAlias);
	void texteditCursorDraw(Common::Rect rect, const char *text, uint16 curPos);
	void texteditCursorErase();
	int getPicNotValid();

	/**
	 * Hex + readable rendering of a string, for the Hangul debug channel.
	 *
	 * The whole point of this log is to see bytes the screen cannot show, so
	 * it prints every byte as hex and marks the EUC-KR pairs. Never call it
	 * outside a debugC() - it builds a string.
	 */
	static Common::String hangulDump(const Common::String &s);

	SegManager *_segMan;
	GfxPorts *_ports;
	GfxPaint16 *_paint16;
	GfxText16 *_text16;
	GfxScreen *_screen;

	// Textedit-Control related
	Common::Rect _texteditCursorRect;
	bool _texteditCursorVisible;
	uint32 _texteditBlinkTime;

	// Korean text entry. Inert unless the sci_hangul_input config key is set:
	// without it the Han/Yeong key is never synthesized, so _koreanInput is
	// never enabled and every branch below it is unreachable.
	KoreanComposer _koreanInput;
	/// Byte offset in the edit string where the composer's run begins.
	uint _koreanRunStart;
	/// The object whose string _koreanRunStart refers to; a different control
	/// means the run belongs to someone else and must not be reused.
	reg_t _koreanRunObject;
	/// The string as this control last left it. A run describes bytes in a
	/// SPECIFIC string: if anything else rewrote it - a script clearing the
	/// line after Enter, restoring a default, or loading a save - the run is
	/// stale even when the offset still happens to be in range. Comparing
	/// lengths cannot see that; comparing the bytes can.
	Common::String _koreanRunText;
};

} // End of namespace Sci

#endif // SCI_GRAPHICS_CONTROLS16_H
