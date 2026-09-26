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
#include "common/system.h"
#include "common/textconsole.h"
#include "common/ustr.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/hires_text/text_compose.h"

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

ScummHiResText::~ScummHiResText() {
	freeFaces();
}

void ScummHiResText::reset() {
	_enabled = false;
	_simpleFonts = false;
	_simpleCellHeight = 0;
	_simpleCellCount = 0;
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
	freeFaces();
}

void ScummHiResText::freeFaces() {
	for (Common::HashMap<Common::String, Face *>::iterator it = _sources.begin();
		 it != _sources.end(); ++it) {
		if (it->_value)
			delete it->_value->source;
		delete it->_value;
	}
	_sources.clear();

	for (int i = 0; i < kMaxFonts; ++i) {
		_cjkFaces[i] = nullptr;
		_latinFaces[i] = nullptr;
		_ttfFaces[i] = nullptr;
		_ttfFacePx[i] = 0;
	}
	_singleFace = nullptr;
	_latinSingleFace = nullptr;
	_logFace = nullptr;
}

void ScummHiResText::adoptConfig(const Graphics::HiResTextConfig &config) {
	freeFaces();
	_config = config;
	_enabled = true;
	_fontsLoaded = false;
}

bool ScummHiResText::addBitmapFont(int charsetId, bool latin, Common::SeekableReadStream &stream,
								   const Common::String &name) {
	if (charsetId >= kMaxFonts)
		return false;

	Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
	if (!font->load(stream)) {
		delete font;
		return false;
	}

	// The same file named twice - a single font doubling as the Latin one -
	// is one source.
	const Common::String key = Common::String::format("%s@%d", name.c_str(), font->cellHeight());
	Face *face = nullptr;
	Common::HashMap<Common::String, Face *>::iterator it = _sources.find(key);
	if (it != _sources.end() && it->_value && it->_value->bitmap) {
		delete font;
		face = it->_value;
	} else {
		face = new Face();
		face->bitmap = font;
		face->source = new Graphics::SvfnGlyphSource(font, DisposeAfterUse::YES);
		face->slot = charsetId;
		if (it != _sources.end()) {
			if (it->_value)
				delete it->_value->source;
			delete it->_value;
		}
		_sources[key] = face;
	}

	if (charsetId < 0) {
		if (latin)
			_latinSingleFace = face;
		else
			_singleFace = face;
	} else if (latin) {
		_latinFaces[charsetId] = face;
	} else {
		_cjkFaces[charsetId] = face;
	}

	_fontsLoaded = true;
	return true;
}

bool ScummHiResText::loadBitmapFile(const Common::Path &gameDir, const Common::String &name,
									int charsetId, bool latin) {
	Common::FSNode node(gameDir.appendComponent(name));
	if (!node.exists())
		return false;

	Common::SeekableReadStream *stream = node.createReadStream();
	if (!stream)
		return false;

	const bool ok = addBitmapFont(charsetId, latin, *stream, name);
	delete stream;

	if (!ok) {
		warning("SCUMM: %s is not a usable hi-res font", name.c_str());
		return false;
	}

	const Face *face = (charsetId < 0) ? (latin ? _latinSingleFace : _singleFace)
									   : (latin ? _latinFaces[charsetId] : _cjkFaces[charsetId]);
	const Graphics::HiResBitmapFont &font = *face->bitmap;
	if (latin) {
		debug(1, "SCUMM: hi-res Latin font %d <- %s: %dx%d cell, %d bpp, %d glyphs, %s",
			  charsetId, name.c_str(), font.cellWidth(), font.cellHeight(), font.bpp(),
			  font.glyphCount(), font.isProportional() ? "proportional" : "fixed width");
	} else {
		debug(1, "SCUMM: hi-res font %d <- %s: %dx%d cell, %d bpp, %d glyphs, "
				 "%s, codepage %s%s",
			  charsetId, name.c_str(), font.cellWidth(), font.cellHeight(), font.bpp(),
			  font.glyphCount(), font.isProportional() ? "proportional" : "fixed width",
			  codePageName(font.codePage()),
			  font.bpp() == 8 ? ", anti-aliased" : ", stencil");
	}
	return true;
}

bool ScummHiResText::loadFonts(const Common::Path &gameDir) {
	_fontsLoaded = false;
	freeFaces();

	if (!_enabled)
		return false;

	const uint32 startMs = g_system ? g_system->getMillis() : 0;

	// A game changes charset mid-scene - dialogue, the verb line and a title
	// card are different sizes - so a map names a numbered set, one file per
	// charset the game uses.
	if (!_config.bitmapPattern.empty()) {
		for (int i = 0; i < kMaxFonts; ++i) {
			const Common::String name = expandFontPattern(_config.bitmapPattern, i);
			if (name.empty())
				break;
			loadBitmapFile(gameDir, name, i, false);
		}
	}

	// One file standing in for every charset, which is what a translation with
	// a single font size ships.
	if (!_config.bitmapSingle.empty())
		loadBitmapFile(gameDir, _config.bitmapSingle, -1, false);

	// A TrueType face is opened, not baked: once per pixel size a charset
	// needs, rasterising nothing but its probe set until a glyph is drawn.
	// The sizes known now - a CJK game's double-byte cells - are opened here,
	// so a face that cannot be used is reported at start-up; a charset whose
	// cell is learnt later opens its size when it first draws.
	if (!_ttfPath.empty()) {
#ifdef USE_FREETYPE2
		Common::FSNode node(_ttfPath);
		if (!node.exists()) {
			warning("SCUMM: hi-res TrueType font not found: '%s'", _ttfPath.toString().c_str());
			_ttfPath.clear();
		} else {
			bool opened = false, anyKnown = false;
			for (int i = 0; i < kMaxFonts; ++i) {
				if (_gameFontH[i] <= 0)
					continue;
				anyKnown = true;
				if (ttfFaceFor(i))
					opened = true;
			}
			// A game with no CJK font learns its cells as charsets are
			// selected, so nothing can be opened yet; the face is still the
			// font this layer draws with.
			if (opened || !anyKnown)
				_fontsLoaded = true;
		}
#else
		// Named a face but cannot rasterise one: say so, or the only symptom
		// is the generic "no replacement font loaded" below.
		warning("SCUMM: hi-res TrueType fonts need a build with FreeType; "
				"bake the font to .fnt instead");
		_ttfPath.clear();
#endif
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

		if (latinName.contains('%')) {
			for (int i = 0; i < kMaxFonts; ++i) {
				// Through the same guard as the CJK names above: this string
				// comes from a map file, and format() would let it name any
				// conversion it liked - %n writes through a stack pointer.
				const Common::String name = expandFontPattern(latinName, i);
				if (name.empty())
					break;
				loadBitmapFile(gameDir, name, i, true);
			}
		} else if (!Common::FSNode(gameDir.appendComponent(latinName)).exists()) {
			warning("SCUMM: hi-res Latin font not found: %s", latinName.c_str());
		} else {
			loadBitmapFile(gameDir, latinName, -1, true);
		}
	}

	if (!_fontsLoaded)
		warning("SCUMM: hi-res text is configured but no replacement font loaded");

	if (g_system)
		debug(1, "SCUMM: hi-res fonts loaded in %u ms (%d glyph sources open)",
			  g_system->getMillis() - startMs, sourceCount());

	return _fontsLoaded;
}

bool ScummHiResText::hasFonts() const {
	return _fontsLoaded;
}

int ScummHiResText::sourceCount() const {
	int n = 0;
	for (Common::HashMap<Common::String, Face *>::const_iterator it = _sources.begin();
		 it != _sources.end(); ++it) {
		if (it->_value)
			++n;
	}
	return n;
}

/**
 * Whether a glyph carries any ink.
 *
 * The layer's contract with every renderer is "false means I drew nothing, so
 * draw it yourself". A glyph that exists in the file but is empty breaks that
 * promise the expensive way: the caller is told the character was handled, so
 * the game's own picture is never drawn and the pixels simply go missing.
 *
 * That is not hypothetical. SCUMM games store small pictures in the control
 * code range - The Dig's option sliders are a run of 0x0B with one 0x0C for
 * the handle - and a Latin face baked from a TrueType font has 68 blank cells
 * in exactly that range. Accepting them erased the slider tracks from the
 * options menu while the labels around them rendered correctly.
 *
 * A space is blank too and is declined here as well, which costs nothing: the
 * original draws nothing for it either, and it then advances by the game's own
 * width like every other character the layer passes on.
 */
static bool glyphHasInk(const Graphics::HiResBitmapFont &font, int index) {
	// A proportional font records the ink extent, so no scan is needed.
	Graphics::GlyphMetrics metrics;
	if (font.isProportional() && font.glyphMetrics(index, metrics))
		return metrics.width > 0 && metrics.height > 0;

	const byte *pixels = font.glyphData(index);
	if (!pixels)
		return false;

	// A fixed-width set has no metrics table, so the cell has to be looked at.
	// It is at most a few hundred bytes and only walked until the first ink.
	const int pitch = font.glyphPitch();
	const int bytes = (font.bpp() == 8) ? font.cellWidth() : (font.cellWidth() + 7) / 8;
	for (int y = 0; y < font.cellHeight(); ++y) {
		const byte *row = pixels + y * pitch;
		for (int x = 0; x < bytes; ++x) {
			if (row[x])
				return true;
		}
	}
	return false;
}

ScummHiResText::Face *ScummHiResText::faceFor(int charsetId, bool latin) const {
	if (!_fontsLoaded)
		return nullptr;

	const bool inRange = charsetId >= 0 && charsetId < kMaxFonts;

	// A single-byte character has no glyph in a CP949-indexed set, so it must
	// come from a Latin font or not at all. Prefer the one made for this
	// charset's cell, so it shares a baseline with the Hangul around it.
	if (latin) {
		if (inRange && _latinFaces[charsetId])
			return _latinFaces[charsetId];
		if (_latinSingleFace)
			return _latinSingleFace;
		return ttfFaceFor(charsetId);
	}

	if (inRange && _cjkFaces[charsetId])
		return _cjkFaces[charsetId];

	if (_singleFace)
		return _singleFace;

	// A face opened at this charset's own size fits it better than a bitmap
	// font made for another charset.
	if (Face *ttf = ttfFaceFor(charsetId))
		return ttf;

	// A charset can have no replacement of its own - MI2 draws its verb line
	// with charset 6, for which the games ship no korean06.fnt, and upstream
	// remaps that to 0 for its own lookup while leaving _curId at 6. The
	// engine additionally falls back to the nearest set by height, so do the
	// same here: otherwise these characters silently keep the original bitmap
	// font while everything around them is replaced.
	const int nearest = nearestFont(charsetId);
	if (nearest >= 0)
		return _cjkFaces[nearest];

	return nullptr;
}

Graphics::UnicodeGlyphSource *ScummHiResText::sourceFor(int charsetId, bool latin) const {
	const Face *face = faceFor(charsetId, latin);
	return face ? face->source : nullptr;
}

ScummHiResText::Face *ScummHiResText::ttfFaceFor(int charsetId) const {
	if (_ttfPath.empty() || charsetId < 0 || charsetId >= kMaxFonts)
		return nullptr;

	// The face is drawn at the size of the game's own cell times the scale,
	// the cell the text is laid out on. A charset whose cell is not known
	// yet borrows the nearest one that is.
	int height = _gameFontH[charsetId];
	if (height <= 0) {
		const int nearest = nearestTtfCharset(charsetId);
		if (nearest < 0)
			return nullptr;
		height = _gameFontH[nearest];
	}
	const int pixelSize = height * MAX(1, _config.scale);

	if (_ttfFacePx[charsetId] == pixelSize)
		return _ttfFaces[charsetId];

	Face *face = openTtfFace(pixelSize);
	_ttfFaces[charsetId] = face;
	_ttfFacePx[charsetId] = pixelSize;
	return face;
}

ScummHiResText::Face *ScummHiResText::openTtfFace(int pixelSize) const {
	const Common::String key = Common::String::format("%s@%d", _ttfPath.toString('/').c_str(), pixelSize);
	Common::HashMap<Common::String, Face *>::iterator it = _sources.find(key);
	if (it != _sources.end())
		return it->_value;

	// Recorded before anything can fail, so a face that cannot be used is
	// tried - and warned about - once, not once per character.
	_sources[key] = nullptr;

	Common::FSNode node(_ttfPath);
	Common::SeekableReadStream *stream = node.exists() ? node.createReadStream() : nullptr;
	if (!stream) {
		warning("SCUMM: cannot open hi-res TrueType font '%s'", _ttfPath.toString().c_str());
		return nullptr;
	}

	// A Korean game's face has to draw Hangul: a Latin-only face would
	// quietly replace every syllable with nothing.
	const bool requireHangul = (_config.encoding == Common::kWindows949 ||
								_config.encoding == Common::kJohab);
	Common::String error;
	Graphics::TtfGlyphSource *ttf = Graphics::TtfGlyphSource::create(stream, DisposeAfterUse::YES,
																	 pixelSize, error, requireHangul);
	if (!ttf) {
		warning("SCUMM: cannot use hi-res TrueType font '%s' at %dpx: %s",
				_ttfPath.toString().c_str(), pixelSize, error.c_str());
		return nullptr;
	}

	Face *face = new Face();
	face->source = ttf;
	face->ttf = ttf;
	face->pixelSize = pixelSize;
	_sources[key] = face;

	debug(1, "SCUMM: hi-res TrueType font %s opened at %dpx: %u probe glyphs rasterised in %u ms",
		  _ttfPath.baseName().c_str(), pixelSize, ttf->rasterCount(), ttf->totalRenderMs());
	return face;
}

bool ScummHiResText::glyphInk(Face &face, uint32 cp, int *inkRight) const {
	if (face.bitmap) {
		const int index = face.bitmap->glyphIndex(cp);
		if (index < 0 || !glyphHasInk(*face.bitmap, index) || face.source->cells(cp) <= 0)
			return false;
		if (inkRight)
			*inkRight = face.bitmap->cellWidth();
		return true;
	}

	if (face.source->cells(cp) <= 0)
		return false;

	Common::HashMap<uint32, int16>::const_iterator it = face.inkRight.find(cp);
	int right = 0;
	if (it != face.inkRight.end()) {
		right = it->_value;
	} else {
		// The face draws its glyph from column 0 of a two-cell row, bearing
		// included; what matters to layout and to the dirty area is how far
		// the ink reaches. A blank glyph (a space) is declined, as the baked
		// fonts' empty cells were: the game draws nothing for it either.
		const int w = face.source->cellWidth() * 2;
		const int bpp = face.source->bitsPerPixel();
		for (int y = 0; y < face.source->cellHeight(); ++y) {
			const byte *row = face.source->row(cp, y);
			if (!row)
				break;
			for (int x = w - 1; x >= right; --x) {
				if (Graphics::TextCompose::expandCoverage(row, x, bpp)) {
					right = x + 1;
					break;
				}
			}
		}
		face.inkRight[cp] = (int16)right;
	}

	if (right <= 0)
		return false;
	if (inkRight)
		*inkRight = right;
	return true;
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

	// The first cell recorded stands. For a CJK game that is its double-byte
	// font's, set before the fonts load, and the text is laid out on it; a
	// game with none learns each charset's cell here, when it is selected.
	// A TrueType face is opened at that size when the charset first draws.
	if (_gameFontW[charsetId] > 0 && _gameFontH[charsetId] > 0)
		return;

	_gameFontW[charsetId] = width;
	_gameFontH[charsetId] = height;
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
		if (!_cjkFaces[i])
			continue;

		// The replacement was made at the game width times the scale, so undo
		// that to compare like with like.
		const int delta = ABS(_cjkFaces[i]->bitmap->cellWidth() / m - want);
		if (best < 0 || delta < bestDelta) {
			best = i;
			bestDelta = delta;
		}
	}

	return best;
}

int ScummHiResText::nearestTtfCharset(int charsetId) const {
	// The same rule as nearestFont(), over the charsets whose cell is known.
	const int want = (charsetId >= 0 && charsetId < kMaxFonts)
					 ? _charsetWidths[charsetId] : 0;
	if (want <= 0)
		return -1;

	int best = -1;
	int bestDelta = 0;
	for (int i = 0; i < kMaxFonts; ++i) {
		if (_gameFontH[i] <= 0)
			continue;
		const int delta = ABS(_gameFontW[i] - want);
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
							  int gameShadow, Common::Rect *dirty,
							  bool withCoverage) {
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
	Face *face = faceFor(charsetId, wantLatin);
	if (!face)
		return false;

	if (_logText)
		noteDrawn(charsetId, face, chr);

	// A remap names a Unicode code point outright, so it skips the code page
	// step that turns the game's bytes into one.
	const uint32 cp = (lookup == chr) ? codePointFor(chr) : (uint32)lookup;
	if (!cp)
		return false;

	// A glyph that is absent or empty must be declined, not drawn: see
	// glyphHasInk(). Returning true for it would tell the caller the character
	// was handled and suppress the game's own picture.
	int width = 0;
	if (!glyphInk(*face, cp, &width))
		return false;

	// Baseline alignment. The two slots can carry fonts of different ascents
	// - a Latin face leaves room above its capitals and below for descenders
	// while Hangul fills its cell - and drawing both at one y would put them
	// on two different baselines. Measured on the shipped MI2 set: Hangul
	// occupies rows 0..21 of a 24 row cell, 'H' rows 2..19.
	//
	// The reference is the CJK font of this charset, because that is what the
	// game's line spacing was laid out against. A font that records no ascent
	// asks for no shift rather than for a wild one; a TrueType face places
	// every glyph on its own baseline already.
	int baselineShift = 0;
	if (wantLatin && face->bitmap && face->bitmap->ascent() > 0) {
		const Face *ref = faceFor(charsetId, false);
		if (ref && ref != face && ref->bitmap && ref->bitmap->ascent() > 0)
			baselineShift = ref->bitmap->ascent() - face->bitmap->ascent();
	}

	// The rows, expanded to one coverage byte per pixel. A 1bpp stencil
	// becomes 0/255 and records no coverage, as it never did: the glyph
	// renderer only writes the coverage plane for a glyph that has some.
	Graphics::UnicodeGlyphSource &src = *face->source;
	const int height = src.cellHeight();
	const int bpp = src.bitsPerPixel();
	_glyphBuf.resize(width * height);
	for (int gy = 0; gy < height; ++gy) {
		const byte *row = src.row(cp, gy);
		if (!row)
			return false;
		Graphics::TextCompose::expandGlyphRow(&_glyphBuf[gy * width], row, width, bpp, false, 0, 0);
	}

	Graphics::GlyphBitmap glyph;
	glyph.pixels = _glyphBuf.begin();
	glyph.pitch = width;
	glyph.width = width;
	glyph.height = height;
	glyph.bpp = 8;

	Graphics::GlyphStyle style;
	style.color = color;
	style.shadowColor = _config.shadowColorSet ? _config.shadowColor : shadowColor;
	style.shadowMode = resolveShadow(_config.shadowMode, gameShadow);
	style.shadowOffset = (_config.shadowOffset >= 0) ? _config.shadowOffset : 1;

	return Graphics::HiResGlyphRenderer::drawGlyph(dest,
												   (withCoverage && bpp == 8) ? coverage() : nullptr,
												   glyph, x, y + baselineShift, style, dirty);
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

uint32 ScummHiResText::codePointFor(int chr) const {
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
	if (codepoint == 0xFFFD)
		return 0;
	return codepoint;
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

	Face *face = faceFor(charsetId, chr < 256);
	if (!face)
		return gameWidth;

	const uint32 cp = (lookup == chr) ? codePointFor(chr) : (uint32)lookup;
	if (!cp)
		return gameWidth;

	// drawChar() declines an empty glyph so the game draws its own picture,
	// so the advance has to be the game's too. Measuring by the replacement
	// font here while the original is what lands on screen is exactly the
	// measure/draw disagreement that shows up as text drifting out of its box.
	int inkRight = 0;
	if (!glyphInk(*face, cp, &inkRight))
		return gameWidth;

	int advance = 0;

	if (face->bitmap) {
		const Graphics::HiResBitmapFont *font = face->bitmap;
		const int index = font->glyphIndex(cp);
		Graphics::GlyphMetrics metrics;
		if (font->isProportional() && font->glyphMetrics(index, metrics)) {
			advance = metrics.advance;

			// Some glyphs are baked with ink reaching one pixel past their
			// advance - 38 of 2350 in Indy3's vj00.fnt, 4 of 256 in its Latin
			// companion. Left alone they collide with whatever follows, so
			// widen the step to clear the ink.
			const int reach = metrics.bearingX + metrics.width;
			if (reach > advance)
				advance = reach;
		} else {
			// A fixed-width set has no metrics table, so the cell is the advance.
			advance = font->cellWidth();
		}
	} else {
		// A face advances by its own metrics, widened, like the bitmap fonts
		// above, to clear ink that reaches past it.
		advance = MAX(face->source->advance(cp), inkRight);
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

void ScummHiResText::noteDrawn(int charsetId, const Face *face, int chr) const {
	// One line per glyph is unreadable and one line per run is what you want
	// to match against a screenshot, so accumulate until the font changes.
	if (charsetId != _logCharset || face != _logFace) {
		flushTextLog();
		_logCharset = charsetId;
		_logFace = face;
	}

	// chr is the game's own encoding - a CP949 pair for Korean, not a code
	// point - so it has to go through the same decoder the glyph lookup uses.
	// Encoding it directly as UTF-8 produces plausible-looking but wrong
	// syllables, which is worse than failing outright because the log then
	// disagrees with a screen that is perfectly correct.
	const uint32 cp = codePointFor(chr);
	if (!cp) {
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

	// The face may have been freed since the run started (a reload); only
	// trust it while it is still one of the open sources.
	const Face *face = nullptr;
	for (Common::HashMap<Common::String, Face *>::const_iterator it = _sources.begin();
		 it != _sources.end(); ++it) {
		if (it->_value && it->_value == _logFace) {
			face = _logFace;
			break;
		}
	}

	const Graphics::UnicodeGlyphSource *src = face ? face->source : nullptr;
	if (face && face->ttf) {
		// A face is not one of the numbered fonts: say which size it is and
		// what it has cost so far.
		debug("HRTEXT charset=%d font=-1 cell=%dx%d \"%s\" ttf=%dpx rasterised=%u",
			  _logCharset, src->cellWidth(), src->cellHeight(), _logRun.c_str(),
			  face->pixelSize, face->ttf->rasterCount());
	} else {
		debug("HRTEXT charset=%d font=%d cell=%dx%d \"%s\"",
			  _logCharset, face ? face->slot : -1,
			  src ? src->cellWidth() : 0, src ? src->cellHeight() : 0,
			  _logRun.c_str());
	}

	_logRun.clear();
}


// The conventional file names of the map-less form. A translation that
// ships these and nothing else gets hi-res text with no configuration.
//
// The double-byte set is named for the language it holds - hrkor, hrjpn,
// hrchs, hrcht - so a folder can carry more than one and the game's own
// language setting picks. hrlat holds the single-byte half and is shared
// by all of them, because that half is the same Latin alphabet whichever
// CJK set sits beside it.
//
// hires%02d.fnt is the older, language-neutral name for the double-byte
// set and is still read: it is what the first builds and the first version
// of the setup document told people to ship.
//
// All of them fit 8.3. These files travel with game data that is often on
// a FAT volume or inside an archive built by a DOS-era tool, and a name the
// filesystem truncates is a font that silently does not load.
//
// hrlat%02d.fnt is also the name the existing maps already use for their
// [latin] bitmap sets, so a translator moving to the map-less form keeps
// the files they have.
static const char *const kSimpleFontLegacy = "hires%02d.fnt";
static const char *const kSimpleFontSingle = "hires.fnt";
static const char *const kSimpleLatinPattern = "hrlat%02d.fnt";

/// The double-byte font name for a language, or null when it has none.
static const char *simpleFontPatternFor(Common::Language language) {
	switch (language) {
	case Common::KO_KOR:
		return "hrkor%02d.fnt";
	case Common::JA_JPN:
		return "hrjpn%02d.fnt";
	case Common::ZH_CHN:
		return "hrchs%02d.fnt";
	case Common::ZH_TWN:
		return "hrcht%02d.fnt";
	default:
		return nullptr;
	}
}

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

bool ScummHiResText::probeSimpleFonts(const Common::Path &gameDir,
									  Common::Language language) {
	int smallestCell = 0;
	int found = 0;
	int singleByte = 0;
	bool anyCoverage = false;

	// The name for this language, then the language-neutral one. A folder
	// holding sets for several languages is the point of the first; the
	// second is what was shipped before the names carried a language.
	const char *cjkPattern = simpleFontPatternFor(language);
	Common::String usedPattern;

	// Open each file only far enough to read its header; the real load
	// happens in loadFonts(), like the map path.
	Graphics::HiResBitmapFont probe;
	for (int pass = 0; pass < 2 && found == 0; ++pass) {
		const char *pattern = (pass == 0) ? cjkPattern : kSimpleFontLegacy;
		if (!pattern)
			continue;

		for (int i = 0; i < kMaxFonts; ++i) {
			const Common::String name = expandFontPattern(pattern, i);
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
			if (_simpleCellCount < kMaxFonts)
				_simpleCells[_simpleCellCount++] = probe.cellHeight();
			if (isSingleByteFont(probe))
				++singleByte;
			probe.free();
			delete stream;
		}

		if (found > 0)
			usedPattern = pattern;
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

	// Latin companions, under their own name. This is searched BEFORE the
	// early return below, so a translation that ships only Latin fonts and
	// names them hrlat%02d.fnt works on its own. Filing them under
	// that name is the natural thing to do for a European game, and it used
	// to do nothing at all: the search sat after a return that fired when no
	// numbered font was found.
	//
	// Their cell counts towards the scale for the same reason - when they
	// are the only fonts present, they are what the scale must come from.
	bool haveLatin = false;
	{
		Graphics::HiResBitmapFont latinProbe;
		for (int i = 0; i < kMaxFonts; ++i) {
			const Common::String name = expandFontPattern(kSimpleLatinPattern, i);
			Common::FSNode node(gameDir.appendComponent(name));
			if (!node.exists())
				continue;
			Common::SeekableReadStream *stream = node.createReadStream();
			if (!stream)
				continue;
			if (latinProbe.load(*stream)) {
				haveLatin = true;
				if (smallestCell == 0 || latinProbe.cellHeight() < smallestCell)
					smallestCell = latinProbe.cellHeight();
				if (_simpleCellCount < kMaxFonts)
					_simpleCells[_simpleCellCount++] = latinProbe.cellHeight();
				anyCoverage = anyCoverage || latinProbe.bpp() == 8;
				latinProbe.free();
			} else {
				warning("SCUMM: %s is not a usable hi-res font", name.c_str());
			}
			delete stream;
		}
	}

	if (!found && !haveSingle && !haveLatin)
		return false;

	// Which slot the set belongs in comes from the fonts, not from the file
	// name. A European game emits only single-byte characters, and faceFor()
	// sends those to the Latin slot - so a Latin set filed under the numbered
	// name would load and then never be consulted, drawing nothing while
	// reporting eight fonts loaded. The header already says which it is.
	const bool numberedAreLatin = (found > 0 && singleByte == found);

	if (found) {
		if (numberedAreLatin)
			_config.legacy.latinBitmapName = usedPattern;
		else
			_config.bitmapPattern = usedPattern;
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

	// The dedicated name wins over a numbered set routed here by its header:
	// someone who wrote out both meant the explicit one for the Latin half.
	if (haveLatin) {
		_config.legacy.latinEnabled = true;
		_config.legacy.latinBitmapName = kSimpleLatinPattern;
	}

	_simpleFonts = true;
	_simpleCellHeight = smallestCell;
	debug(1, "SCUMM: hi-res fonts found by name (no map): %d %s%s%s%s, "
			 "smallest cell %d",
		  found, found ? usedPattern.c_str() : "numbered",
		  haveSingle ? " + single" : "",
		  numberedAreLatin ? " (single-byte, used as Latin)" : "",
		  haveLatin ? ", with Latin" : "", smallestCell);
	return true;
}

/**
 * Does some font in a map-less set sit at exactly this multiple of the game's
 * own cell?
 *
 * The set holds one font per charset, each at that charset's own cell - the
 * Korean MI2 set is 24, 16, 18, 16, 24 for game charsets of 12, 8, 9, 8, 12 -
 * while only one game height is known at this point. Dividing the smallest
 * cell by that one height pairs a font with the wrong charset (16 over 12)
 * and would refuse a set whose every charset is exactly 2x. So the question
 * is whether SOME font matches, not whether the smallest one does.
 *
 * Static and taking its array, so the rule can be tested without a set of
 * font files on disk: this is the one place both the automatic scale and the
 * user-named scale ask their question, and a rule with no test is a rule that
 * drifts.
 */
bool ScummHiResText::cellMatchesScale(const int *cells, int count,
									  int gameFontHeight, int scale) {
	if (!cells || count <= 0 || gameFontHeight <= 0 || scale <= 0)
		return false;
	for (int i = 0; i < count; ++i) {
		if (cells[i] == gameFontHeight * scale)
			return true;
	}
	return false;
}

void ScummHiResText::resolveScale(int gameFontHeight) {
	if (!_enabled || !_simpleFonts)
		return;

	// A scale the user named is honoured even when the set does not fit it:
	// they asked for it explicitly, and refusing would leave them with no
	// way to run a font this code cannot measure. But it is said out loud.
	// Silently accepting a mismatch is what makes the result - glyphs drawn
	// at 30px on a 16px grid - read as a rendering bug rather than as the
	// font being baked at the wrong size.
	if (_scaleFromUser) {
		if (gameFontHeight > 0 && _simpleCellCount > 0 &&
			!cellMatchesScale(_simpleCells, _simpleCellCount, gameFontHeight,
							  _config.scale))
			warning("SCUMM: hi-res fonts are %dpx, which is not %d times the "
					"%dpx game font; drawing them anyway because the scale was "
					"asked for. Bake them at %dpx to fit",
					_simpleCellHeight, _config.scale, gameFontHeight,
					gameFontHeight * _config.scale);
		return;
	}

	// Policy: a bitmap font is accepted only at a whole multiple of the
	// game's own cell. The surface can only be enlarged by an integer, so a
	// font baked at 1.8x has no scale that draws it correctly - rounding it
	// to 2 would stretch every glyph by a ninth. Rejecting is honest where
	// rounding is not.
	//
	// The measurement is the CELL, never the ink. A Latin face deliberately
	// leaves room above its capitals and below for descenders - measured on
	// the shipped MI2 set, 'H' fills 18 rows of a 24 row cell - so ink
	// height would reject a perfectly good font. Which font in the set is
	// allowed to match is cellMatchesScale()'s business.
	//
	// Smallest scale first, because a set spans several cells and a large
	// one can satisfy a high multiple of a small charset by coincidence:
	// the 24px font of a 12px charset is also 8x3, and reading it as 3x
	// draws every glyph half again too large. The smallest multiple that
	// any font matches is the one the set was baked at.
	if (gameFontHeight > 0 && _simpleCellCount > 0) {
		int scale = 0;
		for (int s = 1; s <= 3 && scale == 0; ++s) {
			if (cellMatchesScale(_simpleCells, _simpleCellCount, gameFontHeight, s))
				scale = s;
		}

		if (scale == 0) {
			warning("SCUMM: hi-res fonts are %dpx for a %dpx game font, which is "
					"not a whole multiple; ignoring them. Bake them at %dpx or %dpx",
					_simpleCellHeight, gameFontHeight,
					gameFontHeight * 2, gameFontHeight * 3);
			// Refusing the scale is not enough on its own. The fonts are
			// loaded by this point, so leaving the layer on would draw
			// 20px glyphs on a 1x layout - overlapping, clipped text that
			// looks far worse than the original. The whole layer goes.
			reset();
			return;
		}

		_config.scale = scale;
		debug(1, "SCUMM: hi-res scale %d from the fonts (a %dpx cell over the "
				 "%dpx game font)", scale, gameFontHeight * scale, gameFontHeight);
		return;
	}

	_config.scale = 1;
	debug(1, "SCUMM: hi-res scale 1 from the fonts (%dpx cell over %dpx game font)",
		  _simpleCellHeight, gameFontHeight);
}

void ScummHiResText::setGameFontCell(int charsetId, int width, int height) {
	if (charsetId >= 0 && charsetId < kMaxFonts) {
		_gameFontW[charsetId] = width;
		_gameFontH[charsetId] = height;
	}
}

bool ScummHiResText::cjkTablesPresent() {
	// Any double-byte page would do; CP949 is the one the patches use. The
	// table-less conversion yields U+FFFD or nothing, never U+AC00.
	const Common::U32String probe("\xb0\xa1", Common::kWindows949);
	return probe.size() == 1 && probe[0] == 0xAC00;
}

bool ScummHiResText::mapNamesNoFonts(const Graphics::HiResTextConfig &config,
									 const Common::Path &ttfPath) {
	return config.bitmapPattern.empty() && config.bitmapSingle.empty() &&
		   config.legacy.latinBitmapName.empty() && ttfPath.empty();
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
		haveMap = probeSimpleFonts(gameDir, language);

	// A TrueType face, from the config or the map, opened when the fonts
	// load. The config key wins, since it is the one a user reaches for.
	if (ConfMan.hasKey("hires_text_font"))
		_ttfPath = Graphics::HiResFontMap::resolvePath(ConfMan.get("hires_text_font"), gameDir);
	else if (!_config.ttfPath[Graphics::kHiResRoleDefault].empty())
		_ttfPath = _config.ttfPath[Graphics::kHiResRoleDefault];

	// A map naming no fonts at all - no bitmap set and no face - is almost
	// always one written for the older TrueType loader, which understands a
	// different set of sections. Left unsaid, the symptom is that the legacy
	// system draws the text while this one supplies only the scale - two
	// systems laying out one screen. A map that names a face is used.
	if (haveMap && !mapPath.empty() && mapNamesNoFonts(_config, _ttfPath))
		warning("SCUMM: '%s' names no [bitmap] fonts; if this is an older "
				"TrueType map, the hi-res text layer will not use it",
				mapPath.toString().c_str());

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
	// nothing naming the fonts, so the engine stays on its original path. A
	// Latin-only set counts as named fonts too - it is the whole of what a
	// European game draws - or the layer would switch itself off for exactly
	// the case the map-less form was added to serve.
	const bool haveNamedFonts = !_config.bitmapPattern.empty() ||
								!_config.bitmapSingle.empty() ||
								!_config.legacy.latinBitmapName.empty();
	_enabled = (haveMap || haveTtf) &&
			   (_config.scale > 1 || haveNamedFonts || haveTtf);

	// Every double-byte string is decoded through encoding.dat. Without it
	// the layer quietly draws no CJK glyph at all - the game's own font
	// shows instead - so say once what is missing and how to supply it.
	if (_enabled) {
		const Common::CodePage page = _config.encoding;
		if ((page == Common::kWindows932 || page == Common::kWindows936 ||
			 page == Common::kWindows949 || page == Common::kWindows950 ||
			 page == Common::kJohab) && !cjkTablesPresent())
			warning("SCUMM: encoding.dat not found (pass --extrapath to dists/engine-data); "
					"CJK glyphs disabled");
	}

	if (_enabled) {
		const char *fontsNamed = "(none named)";
		if (!_config.bitmapPattern.empty())
			fontsNamed = _config.bitmapPattern.c_str();
		else if (!_config.bitmapSingle.empty())
			fontsNamed = _config.bitmapSingle.c_str();
		else if (!_config.legacy.latinBitmapName.empty())
			fontsNamed = _config.legacy.latinBitmapName.c_str();

		debug(1, "SCUMM: hi-res text enabled: scale %d, alpha %s, metrics %s, "
				 "source encoding %s, fonts %s",
			  _config.scale,
			  _config.alpha ? "on" : "off",
			  _config.metricsSource == Graphics::kHiResMetricsFont ? "font" : "game",
			  codePageName(_config.encoding),
			  fontsNamed);
	}
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
