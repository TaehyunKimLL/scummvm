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

#ifndef SCI_GRAPHICS_TEXTLATIN_H
#define SCI_GRAPHICS_TEXTLATIN_H

#include "common/scummsys.h"

namespace Sci {

/**
 * hires_text_latin: how ASCII text is drawn once hires_text_font applies
 * (see cache.cpp's hiresTextFontApplies()) and resolves to a live TrueType
 * face. Resolved per font id (resolveFontSettings(), hirestextsettings.h) from
 * the ini keys and hires_text.map when GfxCache builds that font, carried by
 * the font itself, and never looked up per character.
 */
enum LatinMode {
	kLatinOff,			///< default: today's behaviour, unchanged
	kLatinHalf,			///< ASCII keeps its code point but is routed to the
						///< Unicode/TrueType face and drawn at half (narrow)
						///< width instead of by the game's resource font
	kLatinFullwidth,	///< ASCII is remapped to the fullwidth-forms block
						///< and drawn at double (wide) width
	kLatinProportional	///< ASCII is routed to the Unicode/TrueType face as
						///< in kLatinHalf, each advance taken from the game
						///< font (metrics=game) or the face (metrics=font)
};

/**
 * hires_text_log: which face actually drew a glyph, for GfxText16's
 * per-line tally (Box/Draw/DrawStatus). Answered by GfxFontSet::classify()
 * and GfxFontUnicodeAdapter::classify(), which mirror the same choice their
 * own draw() already makes - this adds no new decision, just a name for the
 * existing one, so it costs nothing when hires_text_log is off (GfxText16
 * only calls classify() when the flag resolved true).
 *
 * Named TextFaceKind, not FaceKind: GfxFontSet already has its own nested
 * FaceKind enum (kFaceResource/kFaceLegacyDbcs/kFaceCodePoint) for a
 * different purpose (which face to ask, not what to report), and the two
 * would otherwise collide by name inside that class's scope.
 */
enum TextFaceKind {
	kTextFaceResource,	///< the game's own resource face
	kTextFaceLegacy,	///< korean.fnt / SJIS.FNT, addressed by byte pair
	kTextFaceUnicode,	///< the SCVMUNI/TrueType bundle, a genuine code point
	kTextFaceLatin		///< ASCII (or its hires_text_latin=fullwidth remap)
						///< routed to that bundle by hires_text_latin
};

/** The arithmetic of hi-res text, free of engine state so it is tested alone
 *  (HIRES_COMPOSITOR_DESIGN.md, hires_text_latin). */
namespace TextCompose {

/**
 * The glyph character for cp, per hires_text_latin's mode: what
 * GfxText16::glyphChar() measures and draws. It is never what the text
 * protocol is classified by - GfxText16::readChar() returns the raw
 * character, so '|' codes, '@' (not 0xFF20) and the ' ' word break keep
 * working in fullwidth mode.
 *
 * kLatinOff never changes cp. kLatinHalf also never changes cp: plain ASCII
 * keeps its ordinary code point, and is instead routed to a different face
 * by asciiGoesToUnicodeFace() below, not by remapping the code point itself.
 *
 * kLatinFullwidth remaps U+0021..U+007E to U+FF01..U+FF5E (+0xFEE0), and,
 * when fullwidthSpace, also remaps U+0020 to U+3000 (IDEOGRAPHIC SPACE). Any
 * other code point - including one already outside ASCII, e.g. a Hangul
 * syllable - is returned unchanged in every mode.
 */
uint32 latinFullwidth(uint32 cp, LatinMode mode, bool fullwidthSpace);

/**
 * Whether a code point already known to be ASCII (cp < 0x80) should be
 * routed to the Unicode face instead of the game's own resource face -
 * overriding the "chr < 0x80 always uses the resource face" rule in
 * GfxFontSet::faceFor() (fontset.cpp) and GfxFontUnicodeAdapter
 * (fontunicode.cpp).
 *
 * Only kLatinHalf and kLatinProportional redirect, and only the printable
 * range U+0020..U+007E. The two differ only in the advance: half uses the
 * face's narrow cell, proportional latinAdvanceGamePx() (graphics/hires_text/latin_advance.h).
 * kLatinFullwidth needs no such override: its ASCII is already remapped past
 * U+00FF by latinFullwidth() before any chr < 0x80 check ever sees it.
 */
bool asciiGoesToUnicodeFace(uint32 cp, LatinMode mode);

} // End of namespace TextCompose
} // End of namespace Sci

#endif
