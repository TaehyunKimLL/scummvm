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

#ifndef SCUMM_TEXT_UTF8_H
#define SCUMM_TEXT_UTF8_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/language.h"
#include "common/str-enc.h"
#include "graphics/hires_text/text_layout.h"

namespace Scumm {

/**
 * UTF-8 translation text in SCUMM (I18N_TEXT_DESIGN.md sections 3.3, 4.1,
 * 4.3). Nothing here needs a running engine, so the rules are unit-tested
 * directly (test/engines/scumm/text_char_length.h, trs_utf8.h).
 */

/**
 * How many bytes the character at @p p takes. UTF-8 text: 1..4 by the lead
 * byte (a stray continuation byte, or a sequence cut short by @p end, is 1).
 * Anything else: exactly the old expression, is2ByteCharacter(lang, *p) ?
 * 2 : 1, so a legacy bundle is scanned as before.
 */
int textCharLength(bool utf8, Common::Language lang, const byte *p, const byte *end);

/**
 * Argument bytes after an escape code (FF/FE + code): two for every code
 * but 1, 2, 3 and 8 - ScummEngine::resStrLen()'s rule, for text as stored
 * in a script or a .trs bundle. An argument can be 0 (a talkie offset).
 */
int escapeArgBytes(byte code);

/**
 * The length of a SCUMM v1-v7 string in bytes, NUL excluded, read the way
 * resStrLen() reads it: a 0 inside an escape's arguments does not end it.
 * Never more than @p maxLen. Every place that needs the end of a UTF-8
 * translation (wrapping, inserting, transcoding) asks this, not strlen().
 */
int scummTextLength(const byte *s, uint32 maxLen, int version);

/**
 * A byte of a UTF-8 translation that is not UTF-8 - MI1's own glyph 0xFA
 * in the middle of a line, or untranslated game text in the game's own
 * single-byte character set - is the game's character. It is passed to
 * the renderers as U+F700 + byte (Private Use Area): the hi-res layer has
 * no glyph for it, so the game's font draws and measures the byte itself.
 */
static const uint32 kRawGameByteBase = 0xF700;

/** The game byte a code point from readUtf8TextChar() stands for, or -1. */
int rawGameByte(uint32 cp);

/**
 * Rewrite the strings of a UTF-8 bundle body in the code page @p to: the
 * translations are transcoded (escapes kept, see transcodeScummText()),
 * the originals copied as they are, each measured with scummTextLength().
 * The offsets (relative to @p body) are updated to point into @p out;
 * offsets shared by several entries stay shared.
 */
void transcodeTrsStrings(const byte *body, uint32 bodySize, Common::Array<uint32> &originalOffset,
						 Common::Array<uint32> &translatedOffset, Common::CodePage to, int version,
						 Common::Array<byte> &out, Common::HashMap<uint32, bool> *unmapped);

/**
 * Decode the UTF-8 character at @p p for the charset renderers and advance
 * @p p past it. A byte that does not start a valid sequence is a raw game
 * byte (kRawGameByteBase + byte, one byte); a code point above U+FFFF is
 * U+FFFD, since the renderers measure through a 16-bit getCharWidth().
 */
uint32 readUtf8TextChar(const byte *&p, const byte *end);

/**
 * SCUMM v1-v6 text as TextRun units. The escapes are opaque control units
 * spanning exactly their bytes: 0xFF/0xFE + code + escapeArgBytes(code)
 * (0xFE only up to v6); codes 1 and 8,
 * and the release's newline character after an escape, are newlines. '@'
 * is a zero-width control. A raw 0x0D (what addLinebreaks() writes) is a
 * newline; 0x0A is an ordinary character, as it is when SCUMM draws it.
 * Everything else is UTF-8; a byte that is not is a raw game byte
 * (kRawGameByteBase + byte). HE's '@'/0x7F sequences are not decoded: UTF-8
 * bundles are not taken by HE games.
 */
class ScummTextDecoder : public Graphics::TextDecoder {
public:
	ScummTextDecoder(int version, bool he, byte newLineChar)
		: _version(version), _he(he), _newLineChar(newLineChar) {}
	int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const override;

	/** Whether FF 08 ('verb on next line') is a newline (the default). */
	void setVerbNewline(bool on) { _verbNewline = on; }

	/** Bytes of the escape at p (>= 2), 0 when p is not an escape. */
	int escapeLength(const byte *p, const byte *end) const;

private:
	int _version;
	bool _he;
	byte _newLineChar;
	bool _verbNewline = true;
	Graphics::Utf8TextDecoder _utf8;
};

/**
 * What layoutLinebreaks() measures with. advance() is the renderer's width;
 * escape() is told of each escape in order as a line is measured (charset
 * switches, code 14, change later widths) and reset() rewinds that state to
 * the start of the text.
 */
class ScummLayoutHooks : public Graphics::LayoutMetrics {
public:
	virtual void reset() {}
	virtual void escape(byte code, const byte *args) { (void)code; (void)args; }

	/** Measures in order, telling escape() of each control unit. */
	int extend(const Graphics::TextRun &run, uint32 from, uint32 i, int widthSoFar) override;

	/** The text being measured; set by layoutLinebreaks(). */
	const byte *_text = nullptr;
};

/**
 * addLinebreaks() for UTF-8 text: the shared layout stage decides where
 * lines end and 0x0D is written there, in the same places as the byte path
 * would write it.
 *
 * Wraps from @p pos up to the end of the string, or to the first 'Wait'
 * (FF 03) or 'no newline' (FF 02) escape, which end what is wrapped now as
 * they do in addLinebreaks(). A line fits when its width + 1 (addLinebreaks()
 * starts at 1) is at most @p maxwidth. At a break, the last ASCII space of
 * the dropped run becomes 0x0D; with none, 0x0D is inserted. An insert that
 * would not fit in @p bufSize (the bytes available from @p str, NUL
 * included) first drops the text's last character, on a character boundary.
 *
 * @param a  addLinebreaks()'s argument: 1 makes FF 08 a newline, otherwise
 *           the spaces after FF 08 become '@' as in the byte path
 */
void layoutLinebreaks(byte *str, int bufSize, int pos, int maxwidth, ScummLayoutHooks &hooks,
					  const Graphics::BreakRules &rules, int version, byte newLineChar, int a = 0);

/** The index and body position of an SCVMTRS bundle. Offsets are absolute. */
struct TrsHeader {
	uint32 numLines = 0;
	uint32 bodyPos = 0;
	Common::Array<uint32> originalOffset;    ///< by entry, as stored
	Common::Array<uint32> translatedOffset;  ///< by entry, as stored
};

/**
 * Parse the header of an SCVMTRS bundle held in memory. False when the
 * magic is wrong or the index or room table run past @p size.
 */
bool parseTrsHeader(const byte *data, uint32 size, TrsHeader &h);

/**
 * Whether a bundle's text is UTF-8: its body starts with EF BB BF, or
 * @p iniUtf8 (text_encoding=utf8) says so. @p looksUtf8, when given, is set
 * for an unmarked body that validates as UTF-8 with at least one multi-byte
 * sequence (one hint is logged for it; it is not taken as UTF-8, since
 * CP949 pairs can form valid UTF-8).
 */
bool decideTrsUtf8(const byte *data, uint32 size, const TrsHeader &h, bool iniUtf8, bool *looksUtf8);

/**
 * Convert SCUMM text between code pages, escapes (0xFF/0xFE + code +
 * arguments) and '@' copied as they are. A character with no form in
 * @p to becomes '?' and its code point is added to @p unmapped. A byte that
 * does not decode in @p from is copied unchanged.
 */
void transcodeScummText(const byte *src, uint32 len, Common::CodePage from, Common::CodePage to,
						Common::Array<byte> &out, Common::HashMap<uint32, bool> *unmapped);

/** The legacy code page a UTF-8 bundle of this language falls back to with
 *  hi-res text off; kCodePageInvalid when it has none. */
Common::CodePage legacyTextPage(Common::Language lang);

} // End of namespace Scumm

#endif
