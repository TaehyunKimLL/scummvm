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

#ifndef AGI_TEXT_H
#define AGI_TEXT_H

#include "common/str.h"

#include "agi/hangulline.h"

namespace Agi {

/** stringPrintf()/stringWordWrap() result buffers: 2000 cells of up to 4 bytes (UTF-8 text mode). */
enum { kTextBufferBytes = 2000 * 4 };

struct TextPos_Struct {
	int16 row;
	int16 column;
};

#define TEXTPOSARRAY_MAX 5

struct TextAttrib_Struct {
	byte foreground;
	byte background;
	byte combinedForeground;
	byte combinedBackground;
};

#define TEXTATTRIBARRAY_MAX 5

struct MessageState_Struct {
	uint8 type;
	int16 wanted_Text_Width;
	TextPos_Struct wanted_TextPos;
	bool dialogue_Open;
	uint8 newline_Char;
	bool  window_Active;
	TextPos_Struct textPos;
	TextPos_Struct textPos_Edge;
	int16 textSize_Width;
	int16 textSize_Height;
	uint16 printed_Height;

	int16 backgroundPos_x;
	int16 backgroundPos_y; // original AGI used lowerY here, we use upperY so that upscaling is easier
	int16 backgroundSize_Width;
	int16 backgroundSize_Height;
};

// this defines here are for calculating character-size inside the visual-screen!
#define FONT_VISUAL_WIDTH           4
#define FONT_VISUAL_HEIGHT          8

#define FONT_DISPLAY_WIDTH          8
#define FONT_DISPLAY_HEIGHT         8
#define FONT_ROW_CHARACTERS         25
#define FONT_COLUMN_CHARACTERS      40
#define FONT_BYTES_PER_CHARACTER    8

#define HEIGHT_MAX                  20

#define TEXT_STRING_MAX_SIZE        40

class TextMgr {
private:
	Words *_words;
	GfxMgr *_gfx;
	AgiEngine *_vm;
	SystemUI *_systemUI;

public:
	TextMgr(AgiEngine *vm, Words *words, GfxMgr *gfx);
	~TextMgr();

	void init(SystemUI *systemUI);

	TextPos_Struct _textPos;
	int16          _textPosArrayCount;
	TextPos_Struct _textPosArray[TEXTPOSARRAY_MAX];

	TextAttrib_Struct _textAttrib;
	int16             _textAttribArrayCount;
	TextAttrib_Struct _textAttribArray[TEXTATTRIBARRAY_MAX];

	uint16 _window_Row_Min;
	uint16 _window_Row_Max;
	int16 _reset_Column;

	void configureScreen(uint16 gameRow);
	uint16 getWindowRowMin();

	void dialogueOpen();
	void dialogueClose();

	void charPos_Clip(int16 &row, int16 &column);
	void charPos_Set(int16 row, int16 column);
	void charPos_Set(TextPos_Struct *posPtr);
	void charPos_Get(int16 &row, int16 &column);
	void charPos_Get(TextPos_Struct *posPtr);
	void charPos_Push();
	void charPos_Pop();
	void charPos_SetInsideWindow(int16 windowRow, int16 windowColumn);
	void charAttrib_Set(byte foreground, byte background);
	byte charAttrib_GetForeground();
	byte charAttrib_GetBackground();
	void charAttrib_Push();
	void charAttrib_Pop();
	byte calculateTextBackground(byte background);

	void display(int16 textNr, int16 textRow, int16 textColumn);
	void displayAdjustRTL(int16 textRow, int16 textColumn, char *text, int16 calculatedWidth);
	void displayText(const char *textPtr, bool disabledLook = false);
	void displayCharacter(uint32 character, bool disabledLook = false);

	void displayTextInsideWindow(const char *textPtr, int16 windowRow, int16 windowColumn);

	MessageState_Struct _messageState;

	void printAt(int16 textNr, int16 textPos_Row, int16 textPos_Column, int16 text_Width);
	void print(int16 textNr);

	bool messageBox(const char *textPtr);
	void messageBox_KeyPress(uint16 newKey);

	bool _messageBoxCancelled;

	void drawMessageBox(const char *textPtr, int16 forcedHeight = 0, int16 wantedWidth = 0, bool forcedWidth = false);
	void getMessageBoxInnerDisplayDimensions(int16 &x, int16 &y, int16 &width, int16 &height);
	bool isMouseWithinMessageBox();
	void closeWindow(bool ttsStopSpeech = true);

	void statusRow_Set(int16 row);
	int16 statusRow_Get();

	void statusEnable();
	void statusDisable();
	bool statusEnabled();

	void statusDraw(bool ttsVoiceScore = false, bool ttsVoiceSound = false);
	void statusClear();

	bool _statusEnabled;
	int16 _statusRow;

	void clearLine(int16 row, byte color);
	void clearLines(int16 row_Upper, int16 row_Lower, byte color);
	void clearBlock(int16 row_Upper, int16 column_Upper, int16 row_Lower, int16 column_Lower, byte color);

	void clearBlockInsideWindow(int16 windowRow, int16 windowColumn, int16 width, byte color);

	bool  _inputEditEnabled;
	byte  _inputCursorChar;

	bool  _optionCommandPromptWindow;

	bool  _promptEnabled;
	int16 _promptRow;
	int16 _promptCursorPos;
	// One slot is one cell: a code point (a byte without an overlay). The
	// index is the column, so the 1:1 relation is in the type (K2 45516bb90d5).
	uint32 _prompt[42];
	uint32 _promptPrevious[42];

	bool inputGetEditStatus();
	void inputEditOn();
	void inputEditOff();
	void inputSetCursorChar(int16 cursorChar);
	byte inputGetCursorChar();

	void promptReset();
	void promptEnable();
	void promptDisable();
	bool promptIsEnabled();

	void promptRow_Set(int16 row);
	int16 promptRow_Get();
	void promptKeyPress(int newKey);
	void promptCancelLine();
	void promptEchoLine();
	void promptRedraw();
	void promptClear(); // for AGI1
	void promptRememberForAutoComplete(bool entered = false); // for auto-completion

	void promptCommandWindow(bool recallLastCommand, int newKey);

	/** The line as text: UTF-8 in UTF-8 text mode, else the bytes typed. */
	Common::String promptGetText() const;
	Common::String promptPreviousGetText() const;
	void promptSetFromText(const Common::String &text); // cells only; the cursor is left alone
	Common::String stringGetText() const;

	int16 _inputStringRow;
	int16 _inputStringColumn;
	bool  _inputStringEntered;
	int16 _inputStringMaxLen;
	int16 _inputStringCursorPos;
	uint32 _inputString[42];
	int16 _inputStringMaxBytes; // UTF-8 text mode: the line's byte budget (the save name's 30-byte field); 0 = none
	/** @p cells[0..len) within _inputStringMaxBytes as UTF-8. */
	bool stringFitsBudget(const uint32 *cells, int16 len) const;

	// Korean typing (UTF-8 text mode): hangul_input (default: a word table is
	// loaded) and hangul_builtin (default true) switch the built-in composer and
	// Han/Yeong on; hangul_start (default true) is the first mode. No badge.
	int _stringPendingKey; // the key that opened the command window: stringEdit() presses it first
	bool _hangulInput; // composer and Han/Yeong
	bool _hangulMode;  // Korean (true) or English
	HangulLine _hangulLine;
	bool hangulInputAvailable() const { return _hangulInput; }
	bool hangulMode() const { return _hangulMode; }
	uint32 hangulPreedit() const { return (_hangulInput && _hangulMode) ? _hangulLine.preedit() : 0; }
	void hangulToggle();
	/** Korean typing from hangul_input/hangul_builtin/hangul_start (DESIGN.md section 7); @p table: agi-ko.dat loaded. */
	void hangulSetup(bool table);
	/** Redraw cells[from..newLen) after erasing back from oldLen; the preedit cell inverted. */
	void redrawInputTail(const uint32 *cells, int16 oldLen, int16 from, int16 newLen);
	/** End a syllable run on @p cells, redrawing its last cell plainly. */
	void hangulEndRun(const uint32 *cells, int16 len);
	/**
	 * The composer's share of an edit line (the prompt, get.string): a jamo
	 * key edits the syllable run at the end of @p cells, a backspace edits it
	 * first while one is being composed. True when @p newKey was consumed.
	 */
	bool hangulEditKey(int newKey, uint32 *cells, int16 &cursorPos, int16 maxCells);

	bool stringWasEntered();
	void stringPos_Get(int16 &row, int16 &column);
	int16 stringGetMaxLen();
	void stringSet(const char *text);
	void stringEdit(int16 stringMaxLen);
	void stringKeyPress(int newKey);
	void stringRememberForAutoComplete(bool entered = false); // for auto-completion

	char *stringPrintf(const char *originalText);
	char *stringWordWrap(const char *originalText, int16 maxWidth, int16 *calculatedWidthPtr = nullptr, int16 *calculatedHeightPtr = nullptr);
};

Common::String rightAlign(Common::String line, va_list args);

} // End of namespace Agi

#endif /* AGI_TEXT_H */
