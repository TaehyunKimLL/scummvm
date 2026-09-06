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
#include "common/ustr.h"

namespace Scumm {

// A map file with this name in the game folder is picked up with no config key
// at all, so a translation can ship one and need no setup.
static const char *const kDefaultMapName = "hires_text.map";

// The name the Korean translations have been shipping. Still honoured so that
// an existing install keeps working.
static const char *const kLegacyMapName = "korean_ttf.map";

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
				Common::String name = Common::String::format(latinName.c_str(), i);
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

	return nullptr;
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
	switch (gameShadow) {
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

	// Which font can hold this character is decided by how the game encoded
	// it, not by the code point: a CP949-indexed set has no Latin glyphs even
	// for characters that exist in Unicode.
	const Graphics::HiResBitmapFont *font = fontFor(charsetId, chr < 256);
	if (!font)
		return false;

	const int index = glyphIndexFor(*font, chr);
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

	const Graphics::HiResBitmapFont *font = fontFor(charsetId, chr < 256);
	if (!font)
		return gameWidth;

	const int index = glyphIndexFor(*font, chr);
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

	_coverage.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
	_coverage.fillRect(Common::Rect(0, 0, w, h), 0);
}

void ScummHiResText::freeCoverage() {
	_coverage.free();
}

void ScummHiResText::clearCoverage() {
	if (_coverage.getPixels())
		_coverage.fillRect(Common::Rect(0, 0, _coverage.w, _coverage.h), 0);
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
		mapPath = Graphics::HiResFontMap::resolvePath(ConfMan.get("korean_ttf_map"), gameDir);
		explicitMap = true;
	}

	if (!explicitMap) {
		const Common::Path candidates[] = {
			gameDir.appendComponent(kDefaultMapName),
			gameDir.appendComponent(kLegacyMapName)
		};
		for (int i = 0; i < ARRAYSIZE(candidates); ++i) {
			Common::FSNode probe(candidates[i]);
			if (probe.exists()) {
				mapPath = candidates[i];
				break;
			}
		}
	}

	bool haveMap = false;
	if (!mapPath.empty()) {
		Common::FSNode probe(mapPath);
		if (probe.exists()) {
			haveMap = Graphics::HiResFontMap::load(mapPath, qualifiers, _config);
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
	if (ConfMan.hasKey("hires_text_scale"))
		_config.scale = ConfMan.getInt("hires_text_scale");
	else if (ConfMan.hasKey("korean_hires_scale"))
		_config.scale = ConfMan.getInt("korean_hires_scale");

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
	_enabled = haveMap && (_config.scale > 1 || !_config.bitmapPattern.empty() ||
						   !_config.bitmapSingle.empty());

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

int ScummHiResText::resolvedFontSize(int role) const {
	if (role < 0 || role >= Graphics::kHiResRoleCount)
		return 0;

	const int size = _config.ttfSize[role];
	if (size <= 0)
		return 0;

	// A logical size follows whatever scale ended up in force, so one map
	// looks the same at 2x and at 3x.
	return _config.ttfSizeRelative[role] ? size * _config.scale : size;
}

int ScummHiResText::supersample(int role) const {
	if (role < 0 || role >= Graphics::kHiResRoleCount)
		return 1;
	return _config.ttfSupersample[role];
}

} // End of namespace Scumm
