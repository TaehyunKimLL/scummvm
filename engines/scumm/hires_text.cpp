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

#include "scumm/hires_text.h"

#include "common/config-manager.h"
#include "common/fs.h"
#include "common/rect.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/textconsole.h"
#include "common/file.h"
#include "common/memstream.h"
#include "common/ustr.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/hires_text/font_baker.h"

namespace Scumm {

// A map file with this name in the game folder is picked up with no config key
// at all, so a translation can ship one and need no setup.
//
// The older "korean_ttf.map" (and the korean_ttf_map config key) are
// deliberately NOT read any more. Those files are in the TrueType-era format:
// the legacy loader would draw the text while this layer supplied only the
// scale, and the two disagreed on the layout grid - which surfaced as click
// drift in Loom. A stale map must fail loudly, not half-work.
static const char *const kDefaultMapName = "hires_text.map";

/**
 * A readable name for a code page, for logs.
 *
 * Which code page a font is indexed by decides whether a game's bytes reach
 * its glyphs at all, so it is worth saying out loud rather than leaving the
 * reader to infer it from a number.
 */
static const char *codePageName(Common::CodePage cp) {
	switch (cp) {
	case Common::kWindows949:
		return "CP949/Korean";
	case Common::kWindows932:
		return "CP932/Japanese";
	case Common::kWindows936:
		return "CP936/Simplified Chinese";
	case Common::kWindows950:
		return "CP950/Traditional Chinese";
	case Common::kWindows1252:
		return "CP1252/Latin";
	case Common::kUtf8:
		return "UTF-8";
	case Common::kCodePageInvalid:
		return "none (Unicode indices)";
	default:
		return "other";
	}
}

/**
 * Expand a numbered font name, e.g. "korean%02d.fnt" with 3 -> "korean03.fnt".
 *
 * The template comes from a map file, so it must not be handed to printf: a
 * hand-edited map could otherwise name any conversion it liked, including one
 * that reads a pointer off the stack. Only a single integer field is
 * understood, with an optional zero-padded width.
 */
static Common::String expandFontPattern(const Common::String &pattern, int index) {
	const char *percent = strchr(pattern.c_str(), '%');
	if (!percent)
		return Common::String();

	Common::String out(pattern.c_str(), percent);
	const char *p = percent + 1;

	bool zeroPad = false;
	if (*p == '0') {
		zeroPad = true;
		++p;
	}

	int width = 0;
	while (*p >= '0' && *p <= '9') {
		width = width * 10 + (*p - '0');
		if (width > 8)
			return Common::String();
		++p;
	}

	if (*p != 'd')
		return Common::String();
	++p;

	Common::String number = Common::String::format("%d", index);
	while (zeroPad && (int)number.size() < width)
		number = Common::String("0") + number;

	out += number;
	out += p;
	return out;
}

ScummHiResText::ScummHiResText() {
	reset();
}

void ScummHiResText::reset() {
	_enabled = false;
	_simpleFonts = false;
	_simpleCellHeight = 0;
	_scaleFromUser = false;
	_ttfPath.clear();
	for (int i = 0; i < kMaxFonts; ++i)
		_gameFontW[i] = _gameFontH[i] = 0;
	_fontsLoaded = false;
	_alphaActive = false;
	memset(_paletteCache, 0, sizeof(_paletteCache));
	memset(_paletteRGB, 0, sizeof(_paletteRGB));
	_config.clear();
	freeCoverage();

	for (int i = 0; i < kMaxFonts; ++i)
		_fonts[i].free();
	_singleFont.free();
	_latinFont.free();
	for (int i = 0; i < kMaxFonts; ++i)
		_latinFonts[i].free();
}

bool ScummHiResText::loadFonts(const Common::Path &gameDir) {
	_fontsLoaded = false;
	for (int i = 0; i < kMaxFonts; ++i)
		_fonts[i].free();
	_singleFont.free();
	_latinFont.free();
	for (int i = 0; i < kMaxFonts; ++i)
		_latinFonts[i].free();

	if (!_enabled)
		return false;

	// A game changes charset mid-scene - dialogue, the verb line and a title
	// card are different sizes - so a map names a numbered set, one file per
	// charset the game uses.
	if (!_config.bitmapPattern.empty()) {
		for (int i = 0; i < kMaxFonts; ++i) {
			const Common::String name = expandFontPattern(_config.bitmapPattern, i);
			if (name.empty())
				break;

			Common::FSNode node(gameDir.appendComponent(name));
			if (!node.exists())
				continue;

			Common::SeekableReadStream *stream = node.createReadStream();
			if (!stream)
				continue;

			if (_fonts[i].load(*stream)) {
				_fontsLoaded = true;
				debug(1, "SCUMM: hi-res font %d <- %s: %dx%d cell, %d bpp, %d glyphs, "
						 "%s, codepage %s%s",
					  i, name.c_str(),
					  _fonts[i].cellWidth(), _fonts[i].cellHeight(), _fonts[i].bpp(),
					  _fonts[i].glyphCount(),
					  _fonts[i].isProportional() ? "proportional" : "fixed width",
					  codePageName(_fonts[i].codePage()),
					  _fonts[i].bpp() == 8 ? ", anti-aliased" : ", stencil");
			} else {
				warning("SCUMM: %s is not a usable hi-res font", name.c_str());
			}
			delete stream;
		}
	}

	// One file standing in for every charset, which is what a translation with
	// a single font size ships.
	if (!_config.bitmapSingle.empty()) {
		Common::FSNode node(gameDir.appendComponent(_config.bitmapSingle));
		if (node.exists()) {
			Common::SeekableReadStream *stream = node.createReadStream();
			if (stream) {
				if (_singleFont.load(*stream)) {
					_fontsLoaded = true;
					debug(1, "SCUMM: hi-res font (single) <- %s: %dx%d cell, %d bpp, "
							 "%d glyphs, %s, codepage %s",
						  _config.bitmapSingle.c_str(),
						  _singleFont.cellWidth(), _singleFont.cellHeight(),
						  _singleFont.bpp(), _singleFont.glyphCount(),
						  _singleFont.isProportional() ? "proportional" : "fixed width",
						  codePageName(_singleFont.codePage()));
				} else {
					warning("SCUMM: %s is not a usable hi-res font",
							_config.bitmapSingle.c_str());
				}
				delete stream;
			}
		}
	}

	// A face is only baked when no bitmap font came in: a shipped .fnt is the
	// primary form, and a face names only what to draw with when there is
	// nothing baked.
	if (!_fontsLoaded && bakeTtfFonts(gameDir))
		_fontsLoaded = true;

	// Latin companions, for the letters the double-byte sets do not carry.
	// The menu, the location titles and much of the dialogue mix scripts, so
	// without these the two halves of a line are drawn at different qualities.
	//
	// The name may be a pattern, exactly like the CJK one, because a game can
	// use a different cell per charset - MI2 has five - and a Latin face at
	// the wrong cell sits on a different baseline from the Hangul beside it.
	if (!_config.legacy.latinBitmapName.empty()) {
		const Common::String &latinName = _config.legacy.latinBitmapName;
		const bool latinPattern = latinName.contains('%');

		if (latinPattern) {
			for (int i = 0; i < kMaxFonts; ++i) {
				// Through the same guard as the CJK names above: this string
				// comes from a map file, and format() would let it name any
				// conversion it liked - %n writes through a stack pointer.
				const Common::String name = expandFontPattern(latinName, i);
				if (name.empty())
					break;

				Common::FSNode node(gameDir.appendComponent(name));
				if (!node.exists())
					continue;

				Common::SeekableReadStream *stream = node.createReadStream();
				if (!stream)
					continue;

				if (_latinFonts[i].load(*stream)) {
					_fontsLoaded = true;
					debug(1, "SCUMM: hi-res Latin font %d <- %s: %dx%d cell, "
							 "%d bpp, %d glyphs, %s",
						  i, name.c_str(),
						  _latinFonts[i].cellWidth(), _latinFonts[i].cellHeight(),
						  _latinFonts[i].bpp(), _latinFonts[i].glyphCount(),
						  _latinFonts[i].isProportional() ? "proportional" : "fixed width");
				} else {
					warning("SCUMM: %s is not a usable hi-res font", name.c_str());
				}
				delete stream;
			}
		} else {
			Common::FSNode node(gameDir.appendComponent(latinName));
			if (node.exists()) {
				Common::SeekableReadStream *stream = node.createReadStream();
				if (stream) {
					if (_latinFont.load(*stream)) {
						_fontsLoaded = true;
						debug(1, "SCUMM: hi-res Latin font <- %s: %dx%d cell, "
								 "%d bpp, %d glyphs, %s",
							  latinName.c_str(),
							  _latinFont.cellWidth(), _latinFont.cellHeight(),
							  _latinFont.bpp(), _latinFont.glyphCount(),
							  _latinFont.isProportional() ? "proportional" : "fixed width");
					} else {
						warning("SCUMM: %s is not a usable hi-res font",
								latinName.c_str());
					}
					delete stream;
				}
			} else {
				warning("SCUMM: hi-res Latin font not found: %s",
						latinName.c_str());
			}
		}
	}

	if (!_fontsLoaded)
		warning("SCUMM: hi-res text is configured but no replacement font loaded");

	return _fontsLoaded;
}

bool ScummHiResText::hasFonts() const {
	return _fontsLoaded;
}

const Graphics::HiResBitmapFont *ScummHiResText::fontFor(int charsetId, bool latin) const {
	if (!_fontsLoaded)
		return nullptr;

	// A single-byte character has no glyph in a CP949-indexed set, so it must
	// come from a Latin font or not at all. Prefer the one baked for this
	// charset's cell, so it shares a baseline with the Hangul around it.
	if (latin) {
		if (charsetId >= 0 && charsetId < kMaxFonts && _latinFonts[charsetId].isLoaded())
			return &_latinFonts[charsetId];
		return _latinFont.isLoaded() ? &_latinFont : nullptr;
	}

	if (charsetId >= 0 && charsetId < kMaxFonts && _fonts[charsetId].isLoaded())
		return &_fonts[charsetId];

	if (_singleFont.isLoaded())
		return &_singleFont;

	// A charset can have no replacement of its own - MI2 draws its verb line
	// with charset 6, for which the games ship no korean06.fnt, and upstream
	// remaps that to 0 for its own lookup while leaving _curId at 6. The
	// engine additionally falls back to the nearest set by height, so do the
	// same here: otherwise these characters silently keep the original bitmap
	// font while everything around them is replaced.
	const int nearest = nearestFont(charsetId);
	if (nearest >= 0)
		return &_fonts[nearest];

	return nullptr;
}

void ScummHiResText::setCharsetGrid(int charsetId, int width, int height) {
	if (charsetId >= 0 && charsetId < kMaxFonts) {
		_charsetWidths[charsetId] = width;
		_charsetHeights[charsetId] = height;
	}
}

void ScummHiResText::noteGameCharset(int charsetId, int width, int height) {
	if (charsetId < 0 || charsetId >= kMaxFonts || width <= 0 || height <= 0)
		return;

	// The size is already what it was: nothing has changed, so nothing to do.
	if (_gameFontW[charsetId] == width && _gameFontH[charsetId] == height)
		return;

	_gameFontW[charsetId] = width;
	_gameFontH[charsetId] = height;

	// Nothing to do once this charset has a font, and nothing to do at all
	// unless a face was named: a translation shipping baked .fnt files has
	// its sizes decided already.
	//
	// Both arrays have to be consulted. A CJK game fills _fonts, a European
	// one only _latinFonts, so testing _fonts alone would let every charset
	// re-selection rasterise the face again.
	if (_ttfPath.empty() ||
		_fonts[charsetId].glyphCount() > 0 || _latinFonts[charsetId].glyphCount() > 0)
		return;

	if (bakeCharset(charsetId))
		_fontsLoaded = true;
}

int ScummHiResText::nearestFont(int charsetId) const {
	// Match the grid the engine actually gave this charset, not the charset's
	// nominal size. The width is what sets the advance, so that is what has to
	// fit; a font matched on the nominal height would be too wide and the
	// glyphs would run into each other.
	const int want = (charsetId >= 0 && charsetId < kMaxFonts)
					 ? _charsetWidths[charsetId] : 0;
	if (want <= 0)
		return -1;

	const int m = scale() > 0 ? scale() : 1;

	int best = -1;
	int bestDelta = 0;
	for (int i = 0; i < kMaxFonts; ++i) {
		if (!_fonts[i].isLoaded())
			continue;

		// The replacement was baked at the game width times the scale, so undo
		// that to compare like with like.
		const int delta = ABS(_fonts[i].cellWidth() / m - want);
		if (best < 0 || delta < bestDelta) {
			best = i;
			bestDelta = delta;
		}
	}

	return best;
}

/**
 * Which decoration the replacement glyphs get.
 *
 * The engine's own shadow setting describes the shape of the game's bitmap
 * font, which a replacement has no reason to match, so a map may override it.
 * Without an override we follow the game and nothing changes by accident.
 */
static Graphics::HiResShadowMode resolveShadow(Graphics::HiResShadowMode fromMap, int gameShadow) {
	if (fromMap != Graphics::kHiResShadowGame)
		return fromMap;

	// _2byteShadow: 1 = none, 2 = drop, 3 = stroke, anything else outline.
	//
	// Zero is not one of those values: it is the field's initial state, and it
	// keeps that value in any game that never loads a CJK font, since only
	// loadCJKFont() and the charset switch ever assign it. Treating zero as
	// "outline" there wrapped every glyph in shadowColor - which is also zero
	// - so English text was drawn black-on-black and vanished. A game that
	// asked for nothing gets nothing.
	switch (gameShadow) {
	case 0:
	case 1:
		return Graphics::kHiResShadowNone;
	case 2:
		return Graphics::kHiResShadowDrop;
	case 3:
		return Graphics::kHiResShadowStroke;
	default:
		return Graphics::kHiResShadowOutline;
	}
}

bool ScummHiResText::drawChar(Graphics::Surface &dest, int chr, int charsetId,
							  int x, int y, byte color, byte shadowColor,
							  int gameShadow, Common::Rect *dirty) {
	if (!_enabled || !_fontsLoaded)
		return false;

	// A code the game repurposed. Returning false here rather than further
	// down is the whole point: the caller draws the original glyph when this
	// layer declines, so an ellipsis stored at '^' or an arrow at '_' stays
	// the picture the game meant instead of becoming Latin punctuation.
	int lookup = chr;
	Graphics::HiResGlyphOverride override;
	if (_config.glyphOverride((uint32)chr, override, charsetId)) {
		if (override.action == Graphics::kHiResGlyphKeep)
			return false;
		lookup = (int)override.codepoint;
	}

	// Which font can hold this character is decided by how the game encoded
	// it, not by the code point: a CP949-indexed set has no Latin glyphs even
	// for characters that exist in Unicode.
	const bool wantLatin = (chr < 256);
	const Graphics::HiResBitmapFont *font = fontFor(charsetId, wantLatin);
	if (!font)
		return false;

	if (_logText)
		noteDrawn(charsetId, font, chr);

	// A remap names a Unicode code point outright, so it skips the code page
	// step that turns the game's bytes into one.
	const int index = (lookup == chr) ? glyphIndexFor(*font, chr)
									  : font->glyphIndex((uint32)lookup);
	if (index < 0)
		return false;

	Graphics::GlyphStyle style;
	style.color = color;
	style.shadowColor = _config.shadowColorSet ? _config.shadowColor : shadowColor;
	style.shadowMode = resolveShadow(_config.shadowMode, gameShadow);
	style.shadowOffset = (_config.shadowOffset >= 0) ? _config.shadowOffset : 1;

	return Graphics::HiResGlyphRenderer::drawGlyph(dest, coverage(), *font, index,
												   x, y, style, dirty);
}

void ScummHiResText::updatePaletteCache(const Graphics::PixelFormat &format,
										const byte *rgb, uint first, uint num) {
	if (first >= 256)
		return;
	if (first + num > 256)
		num = 256 - first;

	memcpy(_paletteRGB + first * 3, rgb, num * 3);
	for (uint i = 0; i < num; ++i) {
		_paletteCache[first + i] = format.RGBToColor(rgb[i * 3 + 0],
													 rgb[i * 3 + 1],
													 rgb[i * 3 + 2]);
	}
}

int ScummHiResText::glyphIndexFor(const Graphics::HiResBitmapFont &font, int chr) const {
	// How a double byte character is packed is decided by arithmetic in the
	// caller rather than by how bytes sit in memory, so this is endian
	// independent. charset.cpp builds the pair as (first << 8) | second while
	// scanning the string, but printChar() is handed it with the halves the
	// other way round, so the LOW half is the lead byte. Feeding the decoder
	// the other order yields plausible but wrong characters - Hanja in the
	// middle of Korean dialogue - rather than an outright failure, so it is
	// worth stating which way round this is.
	byte bytes[2];
	int len;
	if (chr < 256) {
		bytes[0] = (byte)chr;
		len = 1;
	} else {
		bytes[0] = (byte)(chr & 0xFF);
		bytes[1] = (byte)(chr >> 8);
		len = 2;
	}

	const byte *p = bytes;
	const uint32 codepoint = decodeNext(p, bytes + len);

	// U+FFFD means the conversion table was missing or the pair is not valid
	// in this code page; either way there is nothing to look up.
	if (!codepoint || codepoint == 0xFFFD)
		return -1;

	return font.glyphIndex(codepoint);
}

int ScummHiResText::advanceFor(int chr, int charsetId, int gameWidth,
							   int *carry) const {
	if (!_enabled || !_fontsLoaded)
		return gameWidth;

	// metrics=game keeps the game's own advances, which is what preserves the
	// original line breaks and is the default.
	//
	// It still needs a floor. The game lays text out on its own font's grid -
	// Indy3 reports _2byteWidth = 8 for a Hangul syllable - and the
	// replacement only fits if the cell is no wider than 8 * scale. Picking
	// the scale to match is the map's job, but a mismatched map should show
	// slightly loose text rather than characters drawn on top of each other.
	const bool fontMetrics = (_config.metricsSource == Graphics::kHiResMetricsFont);

	// A code this layer declines to draw is laid out by the game as it always
	// was; asking the replacement font for its advance would space the
	// original glyph by a character it is not.
	int lookup = chr;
	Graphics::HiResGlyphOverride override;
	if (_config.glyphOverride((uint32)chr, override, charsetId)) {
		if (override.action == Graphics::kHiResGlyphKeep)
			return gameWidth;
		lookup = (int)override.codepoint;
	}

	const Graphics::HiResBitmapFont *font = fontFor(charsetId, chr < 256);
	if (!font)
		return gameWidth;

	const int index = (lookup == chr) ? glyphIndexFor(*font, chr)
									  : font->glyphIndex((uint32)lookup);
	if (index < 0)
		return gameWidth;

	int advance = 0;

	Graphics::GlyphMetrics metrics;
	if (font->isProportional() && font->glyphMetrics(index, metrics)) {
		advance = metrics.advance;

		// Some glyphs are baked with ink reaching one pixel past their
		// advance - 38 of 2350 in Indy3's vj00.fnt, 4 of 256 in its Latin
		// companion. Left alone they collide with whatever follows, so widen
		// the step to clear the ink.
		const int reach = metrics.bearingX + metrics.width;
		if (reach > advance)
			advance = reach;
	} else {
		// A fixed-width set has no metrics table, so the cell is the advance.
		advance = font->cellWidth();
	}

	if (advance <= 0)
		return gameWidth;

	// metrics=game keeps the game's own advance and only needs a floor, so it
	// must not disturb the carry: the remainder belongs to the font's own
	// spacing and spending it here would shift a layout we are meant to leave
	// exactly as the game had it.
	if (!fontMetrics) {
		const int m = scale();
		const int fit = (m > 1) ? (advance + m - 1) / m : advance;
		return MAX(fit, gameWidth);
	}

	// Font metrics are in the scaled surface's pixels; the engine lays text
	// out in game pixels and multiplies the position back up.
	const int m = scale();
	if (m > 1) {
		if (carry) {
			// Spend what earlier characters could not, so a run of narrow
			// glyphs keeps the font's spacing instead of gaining a pixel each.
			const int total = advance + *carry;
			int whole = total / m;
			*carry = total - whole * m;

			// Two glyphs at one position is worse than a pixel of slack.
			if (whole < 1) {
				whole = 1;
				*carry = 0;
			}
			advance = whole;
		} else {
			// No remainder to carry, so round up: rounding down loses up to
			// (scale - 1) pixels per character and, since ink can fill the
			// whole advance, puts the next glyph on top of this one - 747 of
			// 2350 glyphs at scale 2.
			advance = (advance + m - 1) / m;
		}
	}

	return advance;
}
Graphics::PixelFormat ScummHiResText::cursorFormat(const Graphics::PixelFormat &screenFormat) const {
	if (_alphaActive)
		return Graphics::PixelFormat::createFormatCLUT8();
	return screenFormat;
}

void ScummHiResText::createCoverage(int w, int h) {
	freeCoverage();

	if (!_enabled || !_config.alpha || w <= 0 || h <= 0)
		return;

	// The overlay allocates both planes together; this only asks for the
	// coverage one to be present.
	if (_overlay)
		_overlay->createCoverage(w, h);
}

void ScummHiResText::freeCoverage() {
	if (_overlay)
		_overlay->freeCoverage();
}

void ScummHiResText::noteDrawn(int charsetId, const Graphics::HiResBitmapFont *font,
							   int chr) const {
	int which = -1;
	for (int i = 0; i < kMaxFonts; ++i) {
		if (font == &_fonts[i] || font == &_latinFonts[i]) {
			which = i;
			break;
		}
	}

	// One line per glyph is unreadable and one line per run is what you want
	// to match against a screenshot, so accumulate until the font changes.
	if (charsetId != _logCharset || which != _logFont) {
		flushTextLog();
		_logCharset = charsetId;
		_logFont = which;
	}

	// chr is the game's own encoding - a CP949 pair for Korean, not a code
	// point - so it has to go through the same decoder the glyph lookup uses.
	// Encoding it directly as UTF-8 produces plausible-looking but wrong
	// syllables, which is worse than failing outright because the log then
	// disagrees with a screen that is perfectly correct.
	byte bytes[2];
	int len;
	if (chr < 256) {
		bytes[0] = (byte)chr;
		len = 1;
	} else {
		bytes[0] = (byte)(chr & 0xFF);
		bytes[1] = (byte)(chr >> 8);
		len = 2;
	}

	const byte *p = bytes;
	const uint32 cp = decodeNext(p, bytes + len);
	if (!cp || cp == 0xFFFD) {
		_logRun += '?';
		return;
	}

	// UTF-8, so the line can be read directly out of the log.
	if (cp < 0x80) {
		_logRun += (char)cp;
	} else if (cp < 0x800) {
		_logRun += (char)(0xC0 | (cp >> 6));
		_logRun += (char)(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		_logRun += (char)(0xE0 | (cp >> 12));
		_logRun += (char)(0x80 | ((cp >> 6) & 0x3F));
		_logRun += (char)(0x80 | (cp & 0x3F));
	} else {
		_logRun += (char)(0xF0 | (cp >> 18));
		_logRun += (char)(0x80 | ((cp >> 12) & 0x3F));
		_logRun += (char)(0x80 | ((cp >> 6) & 0x3F));
		_logRun += (char)(0x80 | (cp & 0x3F));
	}
}

void ScummHiResText::flushTextLog() const {
	if (_logRun.empty())
		return;

	const Graphics::HiResBitmapFont *font = nullptr;
	if (_logFont >= 0 && _logFont < kMaxFonts) {
		if (_fonts[_logFont].isLoaded())
			font = &_fonts[_logFont];
		else if (_latinFonts[_logFont].isLoaded())
			font = &_latinFonts[_logFont];
	} else if (_singleFont.isLoaded()) {
		font = &_singleFont;
	}

	debug("HRTEXT charset=%d font=%d cell=%dx%d \"%s\"",
		  _logCharset, _logFont,
		  font ? font->cellWidth() : 0, font ? font->cellHeight() : 0,
		  _logRun.c_str());

	_logRun.clear();
}


// The conventional file names of the map-less form. A translation that
// ships these and nothing else gets hi-res text with no configuration.
static const char *const kSimpleFontPattern = "hires%02d.fnt";
static const char *const kSimpleFontSingle = "hires.fnt";
static const char *const kSimpleLatinPattern = "hires_latin%02d.fnt";

/**
 * Look for fonts under the conventional names and, if any are there, fill
 * in the configuration a map would have carried.
 *
 * The fonts describe themselves: an 8bpp file was baked for blending, the
 * smallest cell against the game's own font gives the scale (settled later,
 * in resolveScale(), when that height is known), and the code page in the
 * header says which of the two slots the set belongs in. What a map can
 * express and a bare font cannot - a different glyph count, metrics=font,
 * a shadow style - keeps its default.
 */

/// True for a font whose glyphs are indexed by a single byte, which is what
/// decides the slot below.
static bool isSingleByteFont(const Graphics::HiResBitmapFont &font) {
	return font.codePage() == Common::kISO8859_1 ||
		   font.codePage() == Common::kWindows1252;
}

bool ScummHiResText::probeSimpleFonts(const Common::Path &gameDir) {
	int smallestCell = 0;
	int found = 0;
	int singleByte = 0;
	bool anyCoverage = false;

	// Open each file only far enough to read its header; the real load
	// happens in loadFonts(), like the map path.
	Graphics::HiResBitmapFont probe;
	for (int i = 0; i < kMaxFonts; ++i) {
		const Common::String name = expandFontPattern(kSimpleFontPattern, i);
		Common::FSNode node(gameDir.appendComponent(name));
		if (!node.exists())
			continue;
		Common::SeekableReadStream *stream = node.createReadStream();
		if (!stream)
			continue;
		const bool ok = probe.load(*stream);
		if (!ok) {
			delete stream;
			warning("SCUMM: %s is not a usable hi-res font", name.c_str());
			continue;
		}
		++found;
		if (smallestCell == 0 || probe.cellHeight() < smallestCell)
			smallestCell = probe.cellHeight();
		anyCoverage = anyCoverage || probe.bpp() == 8;
		if (isSingleByteFont(probe))
			++singleByte;
		probe.free();
		delete stream;
	}

	bool haveSingle = false;
	bool singleIsLatin = false;
	{
		Common::FSNode node(gameDir.appendComponent(kSimpleFontSingle));
		if (node.exists()) {
			Common::SeekableReadStream *stream = node.createReadStream();
			if (stream) {
				if (probe.load(*stream)) {
					haveSingle = true;
					if (smallestCell == 0 || probe.cellHeight() < smallestCell)
						smallestCell = probe.cellHeight();
					anyCoverage = anyCoverage || probe.bpp() == 8;
					singleIsLatin = isSingleByteFont(probe);
					probe.free();
				} else {
					warning("SCUMM: %s is not a usable hi-res font", kSimpleFontSingle);
				}
				delete stream;
			}
		}
	}

	if (!found && !haveSingle)
		return false;

	// Which slot the set belongs in comes from the fonts, not from the file
	// name. A European game emits only single-byte characters, and fontFor()
	// sends those to the Latin slot - so a Latin set filed under the numbered
	// name would load and then never be consulted, drawing nothing while
	// reporting eight fonts loaded. The header already says which it is.
	const bool numberedAreLatin = (found > 0 && singleByte == found);

	if (found) {
		if (numberedAreLatin)
			_config.legacy.latinBitmapName = kSimpleFontPattern;
		else
			_config.bitmapPattern = kSimpleFontPattern;
	}
	if (haveSingle) {
		// One field carries both forms; loadFonts() tells them apart by the
		// conversion in the name, so a pattern must not be overwritten by
		// the single name.
		if (singleIsLatin) {
			if (_config.legacy.latinBitmapName.empty())
				_config.legacy.latinBitmapName = kSimpleFontSingle;
		} else {
			_config.bitmapSingle = kSimpleFontSingle;
		}
	}
	if (numberedAreLatin || singleIsLatin)
		_config.legacy.latinEnabled = true;
	_config.alpha = anyCoverage;

	// Latin companions are optional and follow the same convention. A game
	// whose single-byte range is not ASCII - DOTT's ellipsis at 0x5e - must
	// simply not ship them.
	bool haveLatin = false;
	for (int i = 0; i < kMaxFonts && !haveLatin; ++i) {
		Common::FSNode node(gameDir.appendComponent(expandFontPattern(kSimpleLatinPattern, i)));
		haveLatin = node.exists();
	}
	if (haveLatin) {
		_config.legacy.latinEnabled = true;
		_config.legacy.latinBitmapName = kSimpleLatinPattern;
	}

	_simpleFonts = true;
	_simpleCellHeight = smallestCell;
	debug(1, "SCUMM: hi-res fonts found by name (no map): %d numbered%s%s%s, "
			 "smallest cell %d",
		  found, haveSingle ? " + single" : "",
		  numberedAreLatin ? " (single-byte, used as Latin)" : "",
		  haveLatin ? ", with Latin" : "", smallestCell);
	return true;
}

void ScummHiResText::resolveScale(int gameFontHeight) {
	if (!_enabled || !_simpleFonts || _scaleFromUser)
		return;

	// A 16px cell over an 8px game font is 2x; anything fractional rounds
	// to the nearest whole factor, since the surface can only be enlarged
	// by an integer. Out of range means the fonts were baked for something
	// else, and the layer stays at 1 rather than guess.
	int scale = 1;
	if (gameFontHeight > 0 && _simpleCellHeight > 0)
		scale = (_simpleCellHeight + gameFontHeight / 2) / gameFontHeight;
	if (scale < 1 || scale > 3) {
		warning("SCUMM: hi-res fonts are %dpx for a %dpx game font; cannot pick a scale, using 1",
				_simpleCellHeight, gameFontHeight);
		scale = 1;
	}
	_config.scale = scale;
	debug(1, "SCUMM: hi-res scale %d from the fonts (%dpx cell over %dpx game font)",
		  scale, _simpleCellHeight, gameFontHeight);
}

void ScummHiResText::setGameFontCell(int charsetId, int width, int height) {
	if (charsetId >= 0 && charsetId < kMaxFonts) {
		_gameFontW[charsetId] = width;
		_gameFontH[charsetId] = height;
	}
}


/**
 * Bake a TrueType face into the same bitmap fonts a translation would ship.
 *
 * One font per charset the game has a CJK font for, each at that charset's
 * cell times the scale, so the result is exactly what the offline tool would
 * have produced for this game - and everything after this point (fallback,
 * metrics, logging, the no-FreeType build) sees only bitmap fonts.
 *
 * A face is a convenience for a translator who has not baked yet; it costs
 * a rasterising pass at start-up, which a shipped .fnt does not.
 */
bool ScummHiResText::bakeCharset(int charsetId) {
#ifdef USE_FREETYPE2
	if (_ttfPath.empty() || charsetId < 0 || charsetId >= kMaxFonts)
		return false;
	if (_gameFontW[charsetId] <= 0 || _gameFontH[charsetId] <= 0)
		return false;

	Common::FSNode node(_ttfPath);
	if (!node.exists()) {
		warning("SCUMM: hi-res TrueType font not found: '%s'", _ttfPath.toString().c_str());
		return false;
	}

	// The code points this game needs. A European game names no CJK block,
	// which is not a failure - the Latin set is the whole of what it draws.
	Common::Array<uint32> cjk;
	switch (_config.encoding) {
	case Common::kWindows949:
		Graphics::HiResFontBaker::hangulSyllables(cjk);
		break;
	case Common::kWindows932:
		Graphics::HiResFontBaker::jisX0208(cjk);
		break;
	case Common::kWindows936:
		Graphics::HiResFontBaker::chineseCodePage(936, cjk);
		break;
	case Common::kWindows950:
		Graphics::HiResFontBaker::chineseCodePage(950, cjk);
		break;
	default:
		debug(1, "SCUMM: hi-res TrueType font: no CJK block for this language, "
				 "baking Latin only");
		break;
	}

	Common::Array<uint32> latin;
	Graphics::HiResFontBaker::latin1(latin);

	// The map decides which codes this font is responsible for, and it
	// decides per charset: a code kept by the game in one charset is an
	// ordinary character in another.
	if (!_config.glyphOverrides.empty() || !_config.scopedGlyphOverrides.empty()) {
		Common::HashMap<uint32, Graphics::HiResGlyphOverride> merged =
			_config.glyphOverrides;
		if (charsetId < (int)_config.scopedGlyphOverrides.size()) {
			const Common::HashMap<uint32, Graphics::HiResGlyphOverride> &scoped =
				_config.scopedGlyphOverrides[charsetId];
			for (Common::HashMap<uint32, Graphics::HiResGlyphOverride>::const_iterator it =
					 scoped.begin(); it != scoped.end(); ++it)
				merged[it->_key] = it->_value;
		}
		Graphics::HiResFontBaker::applyGlyphOverrides(merged, cjk);
		Graphics::HiResFontBaker::applyGlyphOverrides(merged, latin);
	}

	const int m = _config.scale;
	const int cellW = _gameFontW[charsetId] * m;
	const int cellH = _gameFontH[charsetId] * m;

	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream)
		return false;
	Graphics::Font *face = Graphics::loadTTFFont(stream, DisposeAfterUse::YES, cellH,
												 Graphics::kTTFSizeModeCell);
	if (!face) {
		warning("SCUMM: cannot load '%s' at %dpx", _ttfPath.toString().c_str(), cellH);
		return false;
	}

	bool any = false;

	Common::Array<byte> baked;
	if (!cjk.empty() &&
		Graphics::HiResFontBaker::bake(*face, cjk, cellW, cellH, true, baked)) {
		// Debug aid: write the baked file out so it can be inspected with
		// the same tools as a shipped one.
		if (ConfMan.hasKey("hires_text_dump_baked") && ConfMan.getBool("hires_text_dump_baked")) {
			Common::DumpFile df;
			if (df.open(Common::Path(Common::String::format("baked%02d.fnt", charsetId))))
				df.write(baked.data(), baked.size());
		}
		Common::MemoryReadStream ms(baked.data(), baked.size());
		if (_fonts[charsetId].load(ms)) {
			any = true;
			debug(1, "SCUMM: hi-res font %d <- %s baked at %dx%d, %d glyphs",
				  charsetId, _ttfPath.baseName().c_str(), cellW, cellH,
				  _fonts[charsetId].glyphCount());
		}
	}

	Common::Array<byte> bakedLatin;
	if (Graphics::HiResFontBaker::bake(*face, latin, cellW, cellH, true, bakedLatin)) {
		Common::MemoryReadStream ms(bakedLatin.data(), bakedLatin.size());
		if (_latinFonts[charsetId].load(ms)) {
			// A European game bakes nothing else, so this is what makes the
			// difference between a face that draws and one that is loaded
			// and then silently unused.
			any = true;
			debug(1, "SCUMM: hi-res Latin font %d <- %s baked at %dx%d",
				  charsetId, _ttfPath.baseName().c_str(), cellW, cellH);
		}
	}

	delete face;

	if (any)
		_config.legacy.latinEnabled = true;
	return any;
#else
	(void)charsetId;
	// Named a face but cannot rasterise one: say so, or the only symptom is
	// the generic "no replacement font loaded" from loadFonts.
	if (!_ttfPath.empty())
		warning("SCUMM: hi-res TrueType fonts need a build with FreeType; "
				"bake the font to .fnt instead");
	return false;
#endif
}

bool ScummHiResText::bakeTtfFonts(const Common::Path &gameDir) {
#ifdef USE_FREETYPE2
	(void)gameDir;
	if (_ttfPath.empty())
		return false;

	// Only the charsets whose cell is already known, which for a CJK game is
	// every charset with a double-byte font. A game without one measures its
	// charsets as they are selected, and bakes then - see noteGameCharset.
	bool any = false;
	for (int i = 0; i < kMaxFonts; ++i) {
		if (_gameFontW[i] > 0 && _gameFontH[i] > 0 && bakeCharset(i))
			any = true;
	}
	return any;
#else
	(void)gameDir;
	return false;
#endif
}

/// The code page a language's text is in, when the map does not say.
static Common::CodePage defaultEncodingFor(Common::Language language) {
	switch (language) {
	case Common::JA_JPN:
		return Common::kWindows932;
	case Common::ZH_CHN:
		return Common::kWindows936;
	case Common::ZH_TWN:
		return Common::kWindows950;
	case Common::KO_KOR:
		return Common::kWindows949;
	default:
		// Everything else is a single byte page the game itself defines. The
		// adapter has to be told explicitly before it may assume otherwise.
		return Common::kCodePageInvalid;
	}
}

void ScummHiResText::loadConfig(const Common::Path &gameDir, const Common::String &gameId,
								int version, Common::Language language) {
	reset();

	// The player's switch, before anything is read - so turning it off skips
	// the map, the fonts named beside it, and any TrueType face, not merely
	// the drawing. A disabled feature that still parses a map still complains
	// about it, and someone who turned this off to stop it complaining should
	// stop hearing from it.
	//
	// Only an explicit false turns the layer off, so an ini without the key
	// behaves exactly as before.
	if (ConfMan.hasKey("hires_text") && !ConfMan.getBool("hires_text")) {
		debug(1, "SCUMM: hi-res text off (hires_text=false): the map, the fonts "
				 "in the game folder and any TrueType face are all ignored");
		return;
	}

	_config.encoding = defaultEncodingFor(language);

	// Sections may be narrowed by game or by SCUMM version, most specific
	// first. These strings are the engine's business; the parser treats them
	// as opaque.
	Common::Array<Common::String> qualifiers;
	if (!gameId.empty())
		qualifiers.push_back(gameId);
	qualifiers.push_back(Common::String::format("v%d", version));

	// An explicit key wins; otherwise a conventionally named file in the game
	// folder is used if one is there.
	Common::Path mapPath;
	bool explicitMap = false;
	if (ConfMan.hasKey("hires_text_map")) {
		mapPath = Graphics::HiResFontMap::resolvePath(ConfMan.get("hires_text_map"), gameDir);
		explicitMap = true;
	} else if (ConfMan.hasKey("korean_ttf_map")) {
		// Say so rather than silently ignoring it: the user thinks a map is
		// configured, and the symptom otherwise is "hi-res text does nothing".
		warning("SCUMM: 'korean_ttf_map' is no longer read; use 'hires_text_map' "
				"with a hires_text.map generated for this build");
	}

	if (!explicitMap) {
		const Common::Path candidate = gameDir.appendComponent(kDefaultMapName);
		Common::FSNode probe(candidate);
		if (probe.exists())
			mapPath = candidate;
	}

	bool haveMap = false;
	if (!mapPath.empty()) {
		Common::FSNode probe(mapPath);
		if (probe.exists()) {
			// Name each charset as a scope, so a map can say that 0x5F is an
			// arrow in the dialogue font and a real underscore in the rest.
			Common::Array<Common::String> scopes;
			for (int i = 0; i < kMaxFonts; ++i)
				scopes.push_back(Common::String::format("cs%d", i));

			haveMap = Graphics::HiResFontMap::load(mapPath, qualifiers, _config, &scopes);
			debug(1, "SCUMM: hi-res map %s: '%s'%s",
				  haveMap ? "read" : "REJECTED", mapPath.toString().c_str(),
				  explicitMap ? " (from the config)" : " (found in the game folder)");

			// A map naming no bitmap fonts is almost always one written for
			// the older TrueType loader, which understands a different set of
			// sections. Left unsaid, the symptom is that the legacy system
			// draws the text while this one supplies only the scale - two
			// systems laying out one screen.
			if (haveMap && _config.bitmapPattern.empty() && _config.bitmapSingle.empty())
				warning("SCUMM: '%s' names no [bitmap] fonts; if this is an older "
						"TrueType map, the hi-res text layer will not use it",
						mapPath.toString().c_str());
		} else if (explicitMap) {
			warning("SCUMM: hi-res text map not found: '%s'", mapPath.toString().c_str());
		}
	}

	// A user setting outranks the map, which is why logical font sizes are
	// only resolved once this is settled.
	// A running log of what is drawn and with which font, so a scene can be
	// matched against the fonts it exercises without guessing.
	_logText = ConfMan.hasKey("hires_text_log") && ConfMan.getBool("hires_text_log");

	// No map, but fonts under the conventional names: the simple form of a
	// translation, which ships files and nothing else. The fonts describe
	// themselves well enough to stand in for a map.
	if (!haveMap && mapPath.empty())
		haveMap = probeSimpleFonts(gameDir);

	// A TrueType face, from the config or the map, baked at start-up. The
	// config key wins, since it is the one a user reaches for.
	if (ConfMan.hasKey("hires_text_font"))
		_ttfPath = Graphics::HiResFontMap::resolvePath(ConfMan.get("hires_text_font"), gameDir);
	else if (!_config.ttfPath[Graphics::kHiResRoleDefault].empty())
		_ttfPath = _config.ttfPath[Graphics::kHiResRoleDefault];

	bool haveTtf = false;
	if (!_ttfPath.empty()) {
		haveTtf = true;
		if (!haveMap) {
			// Nothing else says what this is for; a face on its own means
			// "draw the text twice as large", the common case.
			_config.scale = 2;
			_config.alpha = true;
		}
	}

	_scaleFromUser = false;
	if (ConfMan.hasKey("hires_text_scale")) {
		_config.scale = ConfMan.getInt("hires_text_scale");
		_scaleFromUser = true;
	} else if (ConfMan.hasKey("korean_hires_scale")) {
		_config.scale = ConfMan.getInt("korean_hires_scale");
		_scaleFromUser = true;
	}

	if (_config.scale < 1 || _config.scale > 3) {
		warning("SCUMM: hi-res text scale %d is out of range, ignoring", _config.scale);
		_config.scale = 1;
	}

	if (ConfMan.hasKey("hires_text_alpha"))
		_config.alpha = ConfMan.getBool("hires_text_alpha");
	else if (ConfMan.hasKey("korean_alpha_text"))
		_config.alpha = ConfMan.getBool("korean_alpha_text");

	// Whose advances to use. The default follows the game, because scripts
	// size speech bubbles and choose line breaks from the original widths; a
	// proportional replacement only gets to space itself when asked.
	if (ConfMan.hasKey("hires_text_metrics")) {
		const Common::String value = ConfMan.get("hires_text_metrics");
		if (value.equalsIgnoreCase("font"))
			_config.metricsSource = Graphics::kHiResMetricsFont;
		else if (value.equalsIgnoreCase("game"))
			_config.metricsSource = Graphics::kHiResMetricsGame;
		else
			warning("SCUMM: hires_text_metrics should be 'game' or 'font', not '%s'",
					value.c_str());
	}

	// Being asked for is not the same as being usable: without a map there is
	// nothing naming the fonts, so the engine stays on its original path.
	_enabled = (haveMap || haveTtf) &&
			   (_config.scale > 1 || !_config.bitmapPattern.empty() ||
				!_config.bitmapSingle.empty() || haveTtf);

	if (_enabled)
		debug(1, "SCUMM: hi-res text enabled: scale %d, alpha %s, metrics %s, "
				 "source encoding %s, fonts %s",
			  _config.scale,
			  _config.alpha ? "on" : "off",
			  _config.metricsSource == Graphics::kHiResMetricsFont ? "font" : "game",
			  codePageName(_config.encoding),
			  !_config.bitmapPattern.empty() ? _config.bitmapPattern.c_str()
					: (!_config.bitmapSingle.empty() ? _config.bitmapSingle.c_str()
													 : "(none named)"));
}

/**
 * How many bytes the character starting at @p lead takes, in @p page.
 *
 * Returns 1 for anything that is not a lead byte, so a caller always makes
 * progress and never splits a string mid-character.
 */
static int charLength(Common::CodePage page, const byte *p, const byte *end) {
	const byte lead = *p;

	switch (page) {
	case Common::kUtf8:
		if (lead < 0x80)
			return 1;
		if ((lead & 0xE0) == 0xC0)
			return 2;
		if ((lead & 0xF0) == 0xE0)
			return 3;
		if ((lead & 0xF8) == 0xF0)
			return 4;
		return 1;   // a stray continuation byte

	case Common::kWindows932:
		// Shift-JIS: two lead byte ranges. Everything between them, including
		// half-width katakana at 0xA1..0xDF, is a single byte character - a
		// reminder that byte width says nothing about which script it is.
		return ((lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xFC)) ? 2 : 1;

	case Common::kWindows936:
	case Common::kWindows950:
		return (lead >= 0x81 && lead <= 0xFE) ? 2 : 1;

	case Common::kWindows949:
		return (lead >= 0x81 && lead <= 0xFE) ? 2 : 1;

	case Common::kJohab:
		return (lead >= 0x84 && lead <= 0xF9) ? 2 : 1;

	default:
		// A single byte page, or none named at all.
		return 1;
	}

	(void)end;
}

uint32 ScummHiResText::decodeNext(const byte *&p, const byte *end) const {
	if (!p || p >= end)
		return 0;

	const Common::CodePage page = _config.encoding;

	// With no encoding named, the game's text is single byte in whatever the
	// game itself defines. Passing it through unchanged keeps the code point
	// equal to the byte, which is what the original bitmap path assumed.
	if (page == Common::kCodePageInvalid) {
		return *p++;
	}

	int len = charLength(page, p, end);
	if (p + len > end) {
		// A truncated character at the end of the string: consume one byte so
		// the caller still terminates.
		len = 1;
	}

	// ASCII is ASCII in every page here, and asking the shared decoder for it
	// would need the CJK conversion tables loaded.
	if (len == 1 && *p < 0x80)
		return *p++;

	const Common::String bytes((const char *)p, len);
	const Common::U32String decoded(bytes, page);
	p += len;

	if (decoded.empty())
		return 0;

	return decoded[0];
}

} // End of namespace Scumm
