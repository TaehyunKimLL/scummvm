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
}

bool ScummHiResText::loadFonts(const Common::Path &gameDir) {
	_fontsLoaded = false;
	for (int i = 0; i < kMaxFonts; ++i)
		_fonts[i].free();
	_singleFont.free();

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
				debug(1, "SCUMM: hi-res font %d: %dx%d %dbpp, %d glyphs%s",
					  i, _fonts[i].cellWidth(), _fonts[i].cellHeight(), _fonts[i].bpp(),
					  _fonts[i].glyphCount(), _fonts[i].isProportional() ? ", proportional" : "");
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
					debug(1, "SCUMM: hi-res font (single): %dx%d %dbpp, %d glyphs",
						  _singleFont.cellWidth(), _singleFont.cellHeight(),
						  _singleFont.bpp(), _singleFont.glyphCount());
				}
				delete stream;
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

const Graphics::HiResBitmapFont *ScummHiResText::fontFor(int charsetId) const {
	if (!_fontsLoaded)
		return nullptr;

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

	const Graphics::HiResBitmapFont *font = fontFor(charsetId);
	if (!font)
		return false;

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
		return false;

	const int index = font->glyphIndex(codepoint);
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
		if (probe.exists())
			haveMap = Graphics::HiResFontMap::load(mapPath, qualifiers, _config);
		else if (explicitMap)
			warning("SCUMM: hi-res text map not found: '%s'", mapPath.toString().c_str());
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

	// Being asked for is not the same as being usable: without a map there is
	// nothing naming the fonts, so the engine stays on its original path.
	_enabled = haveMap && (_config.scale > 1 || !_config.bitmapPattern.empty() ||
						   !_config.bitmapSingle.empty());

	if (_enabled)
		debug(1, "SCUMM: hi-res text enabled, scale %d, alpha %d", _config.scale, _config.alpha);
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
