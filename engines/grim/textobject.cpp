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

#include "common/unicode-bidi.h"

#include "common/dbcs-str.h"

#include "graphics/hires_text/text_layout.h"

#include "engines/grim/debug.h"
#include "engines/grim/grim.h"
#include "engines/grim/textobject.h"
#include "engines/grim/savegame.h"
#include "engines/grim/lua.h"
#include "engines/grim/font.h"
#include "engines/grim/gfx_base.h"
#include "engines/grim/color.h"
#include "engines/grim/localize_text.h"

namespace Grim {

TextObjectCommon::TextObjectCommon() :
		_x(0), _y(0), _fgColor(0), _justify(0), _width(0), _height(0),
		_font(nullptr), _duration(0), _layer(0), _coords(0) {
	if (g_grim)
		g_grim->invalidateTextObjectsSortOrder();
}

void TextObjectCommon::setLayer(int layer) {
	_layer = layer;
	if (g_grim)
		g_grim->invalidateTextObjectsSortOrder();
}

TextObject::TextObject() :
		TextObjectCommon(), _numberLines(1), _textID(""), _elapsedTime(0),
		_maxLineWidth(0), _lines(nullptr), _userData(nullptr), _created(false),
		_blastDraw(false), _isSpeech(false), _stackLevel(0) {
}

TextObject::~TextObject() {
	delete[] _lines;
	if (_created) {
		g_driver->destroyTextObject(this);
	}
	if (g_grim)
		g_grim->invalidateTextObjectsSortOrder();
}

void TextObject::setText(const Common::String &text, bool delaySetup) {
	destroy();
	_textID = text;
	if (!delaySetup)
		setupText();
}

void TextObject::reset() {
	destroy();
	setupText();
}

void TextObject::saveState(SaveGame *state) const {
	state->writeColor(_fgColor);

	state->writeLESint32(_x);
	state->writeLESint32(_y);
	state->writeLESint32(_width);
	state->writeLESint32(_height);
	state->writeLESint32(_justify);
	state->writeLESint32(_numberLines);
	state->writeLESint32(_duration);

	state->writeBool(_blastDraw);
	state->writeBool(_isSpeech);
	state->writeLESint32(_elapsedTime);

	Font::save(_font, state);

	state->writeString(_textID);

	if (g_grim->getGameType() == GType_MONKEY4) {
		state->writeLESint32(_layer);
		state->writeLESint32(_stackLevel);
	}
}

bool TextObject::restoreState(SaveGame *state) {
	_fgColor = state->readColor();

	_x            = state->readLESint32();
	_y            = state->readLESint32();
	_width        = state->readLESint32();
	_height       = state->readLESint32();
	_justify      = state->readLESint32();
	_numberLines  = state->readLESint32();
	_duration     = state->readLESint32();

	_blastDraw    = state->readBool();
	_isSpeech     = state->readBool();
	_elapsedTime  = state->readLESint32();

	_font = Font::load(state);

	_textID = state->readString();

	if (g_grim->getGameType() == GType_MONKEY4) {
		_layer = state->readLESint32();
		_stackLevel = state->readLESint32();
		g_grim->invalidateTextObjectsSortOrder();
	}

	setupText();
	_created = false;
	_userData = nullptr;

	return true;
}

void TextObject::setDefaults(const TextObjectDefaults *defaults) {
	_x = defaults->getX();
	_y = defaults->getY();
	_font = defaults->getFont();
	_fgColor = defaults->getFGColor();
	_justify = defaults->getJustify();
}

int TextObject::getBitmapWidth() const {
	return _maxLineWidth;
}

int TextObject::getBitmapHeight() const {
	return _numberLines * _font->getKernedHeight();
}

int TextObject::getTextCharPosition(int pos) {
	int width = 0;
	Common::String msg = LuaBase::instance()->parseMsgText(_textID.c_str(), nullptr);
	for (int i = 0; (msg[i] != '\0') && (i < pos); ++i) {
		width += _font->getCharKernedWidth(msg[i]);
	}
	return width;
}

void TextObject::destroy() {
	if (_created) {
		g_driver->destroyTextObject(this);
		_created = false;
	}
}

template <typename S>
void TextObject::setupTextReal(S msg, Common::String (*convert)(const S &s)) {
	S message;

	// remove spaces (NULL_TEXT) from the end of the string,
	// while this helps make the string unique it screws up
	// text justification
	// remove char of id 13 from the end of the string,
	int pos = msg.size() - 1;
	while (pos >= 0 && (msg[pos] == ' ' || msg[pos] == 13)) {
		msg.deleteLastChar();
		pos = msg.size() - 1;
	}
	delete[] _lines;
	if (msg.size() == 0) {
		_lines = nullptr;
		_numberLines = 0;
		return;
	}

	// format the output message to incorporate line wrapping
	// (if necessary) for the text object
	const int SCREEN_WIDTH = _width ? _width : 640;
	const int SCREEN_MARGIN = SCREEN_WIDTH / 10;

	// If the speaker is too close to the edge of the screen we have to make
	// some room for the subtitles.
	if (_isSpeech) {
		if (_x < SCREEN_MARGIN) {
			_x = SCREEN_MARGIN;
		} else if (SCREEN_WIDTH - _x < SCREEN_MARGIN) {
			_x = SCREEN_WIDTH - SCREEN_MARGIN;
		}
	}

	// The maximum width for any line of text is determined by the justification
	// mode. Note that there are no left/right margins -- this is consistent
	// with GrimE.
	int maxWidth = 0;
	if (_justify == CENTER) {
		maxWidth = 2 * MIN(_x, SCREEN_WIDTH - _x);
	} else if (_justify == LJUSTIFY) {
		maxWidth = SCREEN_WIDTH - _x;
	} else if (_justify == RJUSTIFY) {
		maxWidth = _x;
	}

	// We break the message to lines not longer than maxWidth
	wrapMessage(msg, maxWidth, message);

	// If the text object is a speech subtitle, the y parameter is the
	// coordinate of the bottom of the text block (instead of the top). It means
	// that every extra line pushes the previous lines up, instead of being
	// printed further down the screen.
	const int SCREEN_TOP_MARGIN = _font->getKernedHeight();
	if (_isSpeech) {
		_y -= _numberLines * _font->getKernedHeight();
		if (_y < SCREEN_TOP_MARGIN) {
			_y = SCREEN_TOP_MARGIN;
		}
	}

	_lines = new Common::String[_numberLines];


	// Reset the max width so it can be recalculated
	_maxLineWidth = 0;

	for (int j = 0; j < _numberLines; j++) {
		int nextLinePos, cutLen;
		const typename S::value_type *breakPos = message.c_str();
		while (*breakPos && *breakPos != '\n')
			breakPos++;
		if (*breakPos == '\n') {
			nextLinePos = breakPos - message.c_str();
			cutLen = nextLinePos + 1;
		} else {
			nextLinePos = message.size();
			cutLen = nextLinePos;
		}
		S currentLine(message.c_str(), message.c_str() + nextLinePos);
		Common::String currentLineConvert = convert(currentLine);

		// Reverse the line for the Hebrew translation
		if (g_grim->getGameLanguage() == Common::HE_ISR)
			currentLineConvert = Common::convertBiDiString(currentLineConvert, Common::kWindows1255);

		_lines[j] = currentLineConvert;
		int width = _font->getKernedStringLength(currentLineConvert);
		if (width > _maxLineWidth)
			_maxLineWidth = width;
		for (int count = 0; count < cutLen; count++)
			message.deleteChar(0);
	}
}

namespace {

// The legacy breaker measures with the game's font, as it always did.
class FontWrapWidths : public LegacyWrapWidths {
public:
	explicit FontWrapWidths(const Font *font) : _font(font) {}
	int32 charWidth(uint32 c) const override { return _font->getCharKernedWidth(c); }
	int32 wcharWidth(byte hi, byte lo) const override { return _font->getWCharKernedWidth(hi, lo); }
	bool isKoreanChar(byte hi, byte lo) const override { return _font->isKoreanChar(hi, lo); }

private:
	const Font *_font;
};

} // End of anonymous namespace

template <typename S>
void TextObject::wrapMessage(const S &msg, int maxWidth, S &message) {
	const bool koreanDbcs = g_grim->getGameType() == GType_GRIM && g_grim->getGameLanguage() == Common::KO_KOR && !g_grim->_isUtf8;
	const bool useDash = !(g_grim->getGameLanguage() == Common::Language::ZH_CHN || g_grim->getGameLanguage() == Common::Language::ZH_TWN);
	_numberLines = wrapLegacy(msg, maxWidth, message, FontWrapWidths(_font), koreanDbcs, useDash);
}

namespace {

// Grim's widths, in game px. A line is as wide as the font draws it, pair
// kerning included, so width() and extend() measure the whole span with the
// font's string width (not additive; T2's rule for such metrics).
class GrimLayoutMetrics : public Graphics::LayoutMetrics {
public:
	explicit GrimLayoutMetrics(const Font *font) : _font(font) {}
	int advance(uint32 cp) override {
		if (cp == Graphics::kControlUnit)
			return 0;
		return _font->getCharKernedWidth(cp);
	}
	int width(const Graphics::TextRun &run, uint32 from, uint32 to) override {
		Common::U32String s;
		for (uint32 u = from; u < to; u++)
			if (run.cp(u) != Graphics::kControlUnit)
				s += run.cp(u);
		return s.empty() ? 0 : _font->getKernedStringLength(s.encode(Common::kUtf8));
	}
	int extend(const Graphics::TextRun &run, uint32 from, uint32 i, int) override {
		return width(run, from, i + 1);
	}

private:
	const Font *_font;
};

} // End of anonymous namespace

void TextObject::wrapMessage(const Common::U32String &msg, int maxWidth, Common::U32String &message) {
	// Official UTF-16 tables keep the breaker they always had; only a
	// UTF-8 table (grim.<code>.tab, a marked grim.ko.tab) takes the shared
	// stage.
	if (!g_grim->_utf8Tab) {
		wrapMessage<Common::U32String>(msg, maxWidth, message);
		return;
	}

	// One rule set for every language: Latin and Hangul break at spaces,
	// kana and Han between characters with kinsoku, Thai before its bases.
	// A word too long for the line is split where it must be, with '-'
	// only between two Latin letters.
	Graphics::TextRun run;
	run.assign(msg);
	GrimLayoutMetrics metrics(_font);
	Graphics::BreakRules rules;
	rules.hangul = Graphics::kHangulBreakWord;
	rules.kinsoku = true;
	rules.thaiFallback = true;
	const int dashWidth = _font->getCharKernedWidth('-');

	_numberLines = 0;
	uint32 from = 0;
	while (from < run.size()) {
		Graphics::LineSpan span = Graphics::TextLayout::fitLine(run, from, maxWidth, metrics, rules);
		bool dash = false;
		if (splitWantsDash(run, span)) {
			// Leave room for the dash, as the breaker before this one did.
			const Graphics::LineSpan narrower = Graphics::TextLayout::fitLine(run, from, maxWidth - dashWidth, metrics, rules);
			if (splitWantsDash(run, narrower)) {
				span = narrower;
				dash = true;
			}
		}
		if (_numberLines > 0)
			message += '\n';
		for (uint32 u = span.first; u < span.end; u++)
			message += msg[u];
		if (dash)
			message += '-';
		_numberLines++;
		if (span.next <= from)
			break;
		from = span.next;
	}
	if (_numberLines == 0)
		_numberLines = 1;
}

static Common::String sConvert(const Common::String &s) {
	return s;
}

static Common::String usConvert(const Common::U32String &us) {
	return us.encode(Common::CodePage::kUtf8);
}

static Common::String dbcsConvert(const Common::DBCSString &ds) {
	return ds.convertToString();
}

void TextObject::setupText() {
	Common::String msg = LuaBase::instance()->parseMsgText(_textID.c_str(), nullptr);

	if (g_grim->_isUtf8)
		setupTextReal<Common::U32String>(msg.decode(Common::CodePage::kUtf8), usConvert);
	else if (g_grim->getGameLanguage() == Common::Language::ZH_CHN || g_grim->getGameLanguage() == Common::Language::ZH_TWN)
		setupTextReal<Common::DBCSString>(Common::DBCSString(msg), dbcsConvert);
	else
		setupTextReal<Common::String>(msg, sConvert);

	_elapsedTime = 0;
}

int TextObject::getLineX(int line) const {
	int x = _x;
	if (line >= _numberLines)
		return 0;
	if (_justify == CENTER)
		x = _x - (_font->getKernedStringLength(_lines[line]) / 2);
	else if (_justify == RJUSTIFY)
		x = _x - getBitmapWidth();

	if (x < 0)
		x = 0;
	return x;
}

int TextObject::getLineY(int line) const {
	int y = _y;

	if (g_grim->getGameType() == GType_GRIM) {
		if (_blastDraw) { // special case for Grim for menu text draw, issue #1083
			y = _y + 5;
		} else {
/*			if (_font->getKernedHeight() == 21) // talk_font,verb_font
				y = _y - 6;
			else if (_font->getKernedHeight() == 26) // special_font
				y = _y - 12;
			else */if (_font->getKernedHeight() == 13) // computer_font
				y = _y - 6;/*
			else if (_font->getKernedHeight() == 19) // pt_font
				y = _y - 9;*/
			else
				y = _y;
		}
	}

	if (y < 0)
		y = 0;
	y += _font->getKernedHeight() * line;

	return y;
}

void TextObject::draw() {
	if (!_lines)
		return;

	if (!_created) {
		g_driver->createTextObject(this);
		_created = true;
	}

	if (_justify > 3 || _justify < 0)
		warning("TextObject::draw: Unknown justification code (%d)", _justify);

	g_driver->drawTextObject(this);
}

void TextObject::update() {
	if (!_duration || !_created) {
		return;
	}

	_elapsedTime += g_grim->getFrameTime();
	if (_elapsedTime > _duration) {
		delete this;
	}
}

} // end of namespace Grim
