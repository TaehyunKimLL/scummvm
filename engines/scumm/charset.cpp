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

#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/surface.h"
#include "common/str-enc.h"
#include "common/fs.h"
#include "common/hashmap.h"
#include "common/formats/ini-file.h"
#include "common/config-manager.h"
#include "scumm/charset.h"
#include "scumm/file.h"
#include "scumm/scumm.h"
#include "scumm/macgui/macgui.h"
#include "scumm/nut_renderer.h"
#include "scumm/util.h"
#include "scumm/he/intern_he.h"
#include "scumm/he/wiz_he.h"

namespace Scumm {

static const int kMaxRawJpCharNum = 1500;

/*
TODO:
Right now our charset renderers directly access _textSurface, as well as the
virtual screens of ScummEngine. Ideally, this would not be the case. Instead,
ScummVM would simply pass the appropriate Surface to the resp. methods.
Of course it is not quite as simple, various flags and offsets have to
be taken into account for that.

The advantage will be cleaner coder (easier to debug, in particular), and a
better separation of the various modules.
*/

bool ScummEngine::isScummvmKorTarget() {
	if (_language == Common::KO_KOR && (_game.version < 7 || _game.id == GID_FT)) {
		return true;
	}
	return false;
}

/**
 * Whether the hi-res TrueType text path applies to this target.
 *
 * The Korean fan translations were the first users and still get in on the
 * language alone, since the engine has special cases for them elsewhere.
 * Any other CJK translation joins by shipping a font map: that keeps the
 * default behaviour untouched for the Japanese and Chinese releases that
 * already render fine through the engine's own CJK mode.
 */
bool ScummEngine::isHiResTextTarget() {
	if (isScummvmKorTarget())
		return true;

	if (_game.version >= 7 && _game.id != GID_FT)
		return false;

	switch (_language) {
	case Common::JA_JPN:
	case Common::ZH_CHN:
	case Common::ZH_TWN:
	case Common::KO_KOR:
		break;
	default:
		return false;
	}

	// Only opt in when a map is actually there. A configured but missing
	// path must not divert these targets away from the engine's own CJK
	// loaders, which is what they would otherwise fall back to.
	if (ConfMan.hasKey("korean_ttf_map")) {
		Common::Path mapPath(ConfMan.getPath("korean_ttf_map"));
		if (!mapPath.empty()) {
			if (Common::FSNode(mapPath).exists())
				return true;

			// Relative names are resolved against the game folder.
			Common::FSNode rel(ConfMan.getPath("path").join(mapPath));
			if (rel.exists())
				return true;
		}
	}

	Common::FSNode probe(ConfMan.getPath("path").appendComponent("korean_ttf.map"));
	return probe.exists();
}

void ScummEngine::loadCJKFont() {
	_useCJKMode = false;
	_textSurfaceMultiplier = 1;
	_newLineCharacter = 0;

	_useMultiFont = false;	// Korean Multi-Font

	// Sega CD Rebel Assault uses its SMUSH subtitle font.
	if (_game.id == GID_REBEL1 && _game.platform == Common::kPlatformSegaCD)
		return;

	// The fan translation path: Korean always, and any other CJK language
	// that ships a font map. Loads the .fnt bitmaps and, when configured,
	// the TrueType replacement on top.
	if (isHiResTextTarget()) {
		loadKorFont();

		return;
	}

	ScummFile fp(this);

	if (_game.version <= 5 && _game.platform == Common::kPlatformFMTowns && _language == Common::JA_JPN) { // FM-TOWNS v3 / v5 Kanji
#if defined(DISABLE_TOWNS_DUAL_LAYER_MODE) || !defined(USE_RGB_COLOR)
		GUIErrorMessage("FM-Towns Kanji font drawing requires dual graphics layer support which is disabled in this build");
		error("FM-Towns Kanji font drawing requires dual graphics layer support which is disabled in this build");
#else
		// use FM-TOWNS font rom, since game files don't have kanji font resources
		_cjkFont = Graphics::FontSJIS::createFont(_game.platform);
		if (!_cjkFont)
			error("SCUMM::Font: Could not open file 'FMT_FNT.ROM'");
		_textSurfaceMultiplier = 2;
		_useCJKMode = true;
#endif
	} else if (_game.id == GID_LOOM && _game.platform == Common::kPlatformPCEngine && _language == Common::JA_JPN) {
#ifdef USE_RGB_COLOR
		// use PC-Engine System Card, since game files don't have kanji font resources
		_cjkFont = Graphics::FontSJIS::createFont(_game.platform);
		if (!_cjkFont)
			error("SCUMM::Font: Could not open file 'pce.cdbios'");

		_cjkFont->setDrawingMode(Graphics::FontSJIS::kShadowRightMode);
		_2byteWidth = _2byteHeight = 12;
		_useCJKMode = true;
#endif
	} else if ((_game.id == GID_MONKEY && _game.platform == Common::kPlatformSegaCD && _language == Common::JA_JPN) || _isIndy4Jap) {
		_2byteWidth = 16;
		_2byteHeight = 16;
		_useCJKMode = true;
		_newLineCharacter = 0x5F;
		// charset resources are not inited yet, load charset later
		_2byteFontPtr = new byte[_2byteWidth * _2byteHeight * kMaxRawJpCharNum / 8];
		// set byte 0 to 0xFF (0x00 when loaded) to indicate that the font was not loaded
		_2byteFontPtr[0] = 0xFF;
	} else if (_language == Common::KO_KOR ||
			   (_game.version >= 7 && (_language == Common::JA_JPN || _language == Common::ZH_TWN)) ||
			   (_game.version >= 3 && _language == Common::ZH_CHN)) {
		int numChar = 0;
		const char *fontFile = nullptr;

		switch (_language) {
		case Common::KO_KOR:
			fontFile = "korean.fnt";
			numChar = 2350;
			break;
		case Common::JA_JPN:
			if (_game.id == GID_DIG)
				fontFile = "kanji16.fnt";
			else if (_game.id == GID_REBEL2)
				fontFile = "LAUNCH/KANJI.FNT";
			else
				fontFile = "japanese.fnt";
			numChar = 8192;
			break;
		case Common::ZH_TWN:
			// Both The DIG and COMI use same font
			fontFile = "chinese.fnt";
			numChar = 13630;
			break;
		case Common::ZH_CHN:
			if (_game.id == GID_FT || _game.id == GID_LOOM || _game.id == GID_INDY3 ||
				_game.id == GID_INDY4 || _game.id == GID_MONKEY || _game.id == GID_MONKEY2 ||
				_game.id == GID_TENTACLE) {
				fontFile = "chinese_gb16x12.fnt";
				numChar = 8178;
			}
			break;
		default:
			break;
		}
		if (fontFile && openFile(fp, fontFile)) {
			debug(2, "Loading CJK Font");
			_useCJKMode = true;
			_textSurfaceMultiplier = 1; // No multiplication here

			switch (_language) {
			case Common::KO_KOR:
				fp.seek(2, SEEK_CUR);
				_2byteWidth = fp.readByte();
				_2byteHeight = fp.readByte();
				_newLineCharacter = (_game.id == GID_CMI) ? 0xff : 0xfe;
				break;
			case Common::JA_JPN:
				_2byteWidth = 16;
				_2byteHeight = 16;
				_newLineCharacter = 0xfe;
				break;
			case Common::ZH_TWN:
				_2byteWidth = 16;
				_2byteHeight = 15;
				_newLineCharacter = 0x21;
				break;
			case Common::ZH_CHN:
				_2byteWidth = 12;
				_2byteHeight = 12;
				_newLineCharacter = 0x21;
				break;
			default:
				break;
			}

			_2byteFontPtr = new byte[((_2byteWidth + 7) / 8) * _2byteHeight * numChar];
			fp.read(_2byteFontPtr, ((_2byteWidth + 7) / 8) * _2byteHeight * numChar);
			fp.close();
		} else {
			if (fontFile)
				error("SCUMM::Font: Could not open %s",fontFile);
			else
				error("SCUMM::Font: Could not load any font");
		}
	}
}

/**
 * Build the name of the i-th bitmap font file from the pattern in the map.
 *
 * The pattern comes from a user supplied file, so it cannot be handed to
 * snprintf() as a format string: a stray "%s" or "%n" in the map would be
 * read as a conversion and make the call read arbitrary memory. Accept a
 * single integer conversion, "%d" with an optional zero padded width, and
 * treat every other percent sequence as a literal.
 */
static Common::String buildCJKFontName(const Common::String &pattern, int index) {
	Common::String out;

	for (uint i = 0; i < pattern.size(); ++i) {
		if (pattern[i] != '%') {
			out += pattern[i];
			continue;
		}

		// Collect an optional zero padded width, e.g. "%02d".
		uint j = i + 1;
		int width = 0;
		bool pad = false;

		if (j < pattern.size() && pattern[j] == '0') {
			pad = true;
			++j;
		}
		while (j < pattern.size() && pattern[j] >= '0' && pattern[j] <= '9') {
			width = width * 10 + (pattern[j] - '0');
			++j;
		}

		if (j < pattern.size() && pattern[j] == 'd') {
			Common::String num = Common::String::format("%d", index);
			while (pad && num.size() < (uint)width)
				num = Common::String("0") + num;
			out += num;
			i = j;
		} else {
			// Not a conversion we support: keep the percent verbatim.
			out += '%';
		}
	}

	return out;
}

/**
 * Recognise the extended bitmap font format and fill in its geometry.
 *
 * The original .fnt has no signature: it opens with a four byte header
 * whose first byte happens to always be 2, and everything else about the
 * file -- how many glyphs, which encoding -- has to be inferred from its
 * size. SVFN starts with a magic instead and says all of it outright,
 * which is what lets it carry 8bpp coverage and per-glyph advances.
 *
 * Returns false for anything that is not SVFN, including the old format,
 * so the caller can fall back to reading it the original way.
 */
bool ScummEngine::parseSvfnHeader(const byte *buf, uint32 size, SvfnFont &out) const {
	if (!buf || size < 32)
		return false;
	if (READ_BE_UINT32(buf) != MKTAG('S', 'V', 'F', 'N'))
		return false;

	const uint16 version = READ_LE_UINT16(buf + 4);
	if (version != 1) {
		warning("SCUMM::Font: SVFN version %d is newer than this build understands", version);
		return false;
	}

	const uint16 flags = READ_LE_UINT16(buf + 6);
	const int bpp = buf[8];
	const int glyphs = READ_LE_UINT16(buf + 12);
	const int cellW = buf[14];
	const int cellH = buf[15];
	const uint32 metricsOff = READ_LE_UINT32(buf + 20);
	const uint32 dataOff = READ_LE_UINT32(buf + 24);
	const uint32 dataSize = READ_LE_UINT32(buf + 28);

	if (bpp != 1 && bpp != 8) {
		warning("SCUMM::Font: SVFN has unsupported depth %d", bpp);
		return false;
	}
	if (cellW <= 0 || cellH <= 0 || glyphs <= 0) {
		warning("SCUMM::Font: SVFN has an empty glyph box");
		return false;
	}

	const int stride = (bpp == 1) ? ((cellW + 7) / 8) * cellH : cellW * cellH;

	// Everything the header points at has to be inside the file: this is
	// data a translation ships, so it cannot be taken on trust.
	if (dataOff > size || dataSize > size - dataOff) {
		warning("SCUMM::Font: SVFN glyph data runs past the end of the file");
		return false;
	}
	if ((uint32)stride * (uint32)glyphs > dataSize) {
		warning("SCUMM::Font: SVFN declares %d glyphs but only holds room for fewer", glyphs);
		return false;
	}

	out.valid = true;
	out.bpp = bpp;
	out.cellW = cellW;
	out.cellH = cellH;
	out.ascent = buf[16];
	out.glyphs = glyphs;
	out.stride = stride;
	out.variable = (flags & 1) != 0;
	out.data = buf + dataOff;
	out.metrics = nullptr;

	if (out.variable) {
		if (metricsOff > size || (uint32)glyphs * 4 > size - metricsOff) {
			warning("SCUMM::Font: SVFN metrics table runs past the end of the file");
			out.variable = false;
		} else {
			out.metrics = buf + metricsOff;
		}
	}

	return true;
}

const byte *ScummEngine::getSvfnGlyph(const SvfnFont &font, int idx) const {
	if (!font.valid || idx < 0 || idx >= font.glyphs)
		return nullptr;

	return font.data + (uint32)idx * (uint32)font.stride;
}

/**
 * Draw one glyph of an extended bitmap font into the text surface.
 *
 * At 8bpp the stored value is coverage, and it goes into the companion
 * alpha channel exactly like a rasterised TrueType glyph would, so the
 * composite step blends it against the upscaled background. That is the
 * point of the format: anti-aliased text without FreeType in the build.
 *
 * The glyph is drawn at its stored size. Unlike the original 1bpp path
 * there is no pixel doubling, because the font was baked for the scaled
 * surface in the first place.
 */
/**
 * Record that the hi-res text surface now has ink at this spot.
 *
 * The charset renderer's own mask only covers text it means to erase
 * itself. Anything drawn with ignoreCharsetMask set - the verb area, the
 * sentence line - never enters it, so nothing would ever clear those
 * glyphs off the scaled surface and they survive into the next room.
 */
void ScummEngine::noteHiResTextDrawn(int x, int y, int w, int h, bool keep) {
	const Common::Rect r(x, y, x + w, y + h);
	if (_hiResTextDirty.isEmpty())
		_hiResTextDirty = r;
	else
		_hiResTextDirty.extend(r);

	// Glyphs the game means to burn into the picture must survive the next
	// clear: it will not draw them again. MI2's difficulty screen puts its
	// whole text up that way and then loses it the moment an ordinary,
	// masked glyph triggers restoreCharsetBg().
	if (keep) {
		if (_hiResTextKeep.isEmpty())
			_hiResTextKeep = r;
		else
			_hiResTextKeep.extend(r);
	}
}

bool ScummEngine::drawSvfnGlyph(Graphics::Surface &dest, const SvfnFont &font, int idx,
								int x, int y, byte color, byte shadowColor) {

	const byte *glyph = getSvfnGlyph(font, idx);
	if (!glyph)
		return false;

	const bool alpha = (font.bpp == 8) && _korAlphaSurface.getPixels();
	const int rowBytes = (font.bpp == 1) ? (font.cellW + 7) / 8 : font.cellW;

	// Where the shadow goes, relative to the glyph. The outline forms are
	// drawn first and in full, so that a later pixel of the same glyph --
	// or the next character along -- cannot paint over a stroke already
	// laid down.
	static const int8 dropX[]    = { 1 };
	static const int8 dropY[]    = { 1 };
	static const int8 outlineX[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static const int8 outlineY[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	static const int8 strokeX[]  = { -1, 0, 1, -1, 1, -1, 0, 1, -1, -1, -2 };
	static const int8 strokeY[]  = { -1, -1, -1, 0, 0, 1, 1, 1, 2, 1, 0 };

	const int8 *offX = nullptr;
	const int8 *offY = nullptr;
	int offCount = 0;

	switch (hiResShadowMode()) {
	case kHiResShadowDrop:
		offX = dropX; offY = dropY; offCount = ARRAYSIZE(dropX);
		break;
	case kHiResShadowOutline:
		offX = outlineX; offY = outlineY; offCount = ARRAYSIZE(outlineX);
		break;
	case kHiResShadowStroke:
		offX = strokeX; offY = strokeY; offCount = ARRAYSIZE(strokeX);
		break;
	default:
		break;
	}

	const byte shadow = _hiResShadowColorSet ? _hiResShadowColor : shadowColor;
	const int step = hiResShadowOffset();

	// The shadow is pointless when it cannot be told apart from the text.
	if (shadow == color)
		offCount = 0;

	for (int pass = (offCount ? 0 : 1); pass < 2; ++pass) {
		const int copies = pass ? 1 : offCount;

		for (int c = 0; c < copies; ++c) {
			const int ox = pass ? 0 : offX[c] * step;
			const int oy = pass ? 0 : offY[c] * step;
			const byte ink = pass ? color : shadow;

			for (int gy = 0; gy < font.cellH; ++gy) {
				const byte *row = glyph + gy * rowBytes;
				const int py = y + gy + oy;

				if (py < 0 || py >= dest.h)
					continue;

				for (int gx = 0; gx < font.cellW; ++gx) {
					byte cov;

					if (font.bpp == 1)
						cov = (row[gx >> 3] & (0x80 >> (gx & 7))) ? 0xFF : 0;
					else
						cov = row[gx];

					if (!cov)
						continue;

					const int px = x + gx + ox;
					if (px < 0 || px >= dest.w)
						continue;

					// The outline must not eat into the glyph body, and at
					// 8bpp a fainter pixel must not replace a stronger one
					// that is already there.
					if (!pass && alpha
							&& px < _korAlphaSurface.w && py < _korAlphaSurface.h) {
						if (*(const byte *)_korAlphaSurface.getBasePtr(px, py) >= cov)
							continue;
					}

					*(byte *)dest.getBasePtr(px, py) = ink;

					if (alpha && px < _korAlphaSurface.w && py < _korAlphaSurface.h)
						*(byte *)_korAlphaSurface.getBasePtr(px, py) = cov;
				}
			}
		}
	}

	// Remember what the hi-res surface now holds so it can be taken down
	// later: the engine's charset mask does not track this text.
	noteHiResTextDrawn(x, y, font.cellW, font.cellH, _hiResTextBurnIn);

	return true;
}

/**
 * Load the single byte companion font named by [latin] bitmap=.
 *
 * Its glyphs are indexed by character code, not through a code page, so
 * entry 65 is 'A'. Everything else about it -- depth, cell size, optional
 * per-glyph advance -- follows the same header as the Hangul font.
 */
/**
 * Which outline or shadow the hi-res glyphs should get.
 *
 * The engine's own _2byteShadow is only consulted by the bitmap blitter,
 * and it describes what the game's .fnt was drawn for. A replacement font
 * is a different shape entirely, so the map gets to override it; without
 * an override we follow the game so nothing changes by accident.
 */
int ScummEngine::hiResShadowMode() const {
	if (_hiResShadowMode != kHiResShadowGame)
		return _hiResShadowMode;

	// _2byteShadow: 1 = none, 2 = drop, 3 = stroke, anything else = outline.
	switch (_2byteShadow) {
	case 1:
		return kHiResShadowNone;
	case 2:
		return kHiResShadowDrop;
	case 3:
		return kHiResShadowStroke;
	default:
		return kHiResShadowOutline;
	}
}

/**
 * How far the shadow sits from the glyph, in scaled pixels.
 *
 * The original fonts carry a one pixel shadow at 320x200, so following the
 * scale keeps the same weight on a 2x or 3x surface. A map can pin it when
 * its font wants something tighter.
 */
int ScummEngine::hiResShadowOffset() const {
	if (_hiResShadowOffset > 0)
		return _hiResShadowOffset;

	return MAX(1, _koreanHiResScale);
}

void ScummEngine::loadSvfnLatin() {
	if (_svfnLatinName.empty() || _svfnLatin.valid)
		return;

	Common::File fp;
	if (!fp.open(Common::Path(_svfnLatinName))) {
		warning("SCUMM::Font: Could not open Latin bitmap font '%s'", _svfnLatinName.c_str());
		return;
	}

	const uint32 size = (uint32)fp.size();
	byte *raw = new byte[size];
	fp.seek(0);
	fp.read(raw, size);
	fp.close();

	if (!parseSvfnHeader(raw, size, _svfnLatin)) {
		warning("SCUMM::Font: '%s' is not in the extended bitmap format", _svfnLatinName.c_str());
		delete[] raw;
		return;
	}

	_svfnLatinData = raw;
	debug(1, "SVFN Latin font: %dx%d %dbpp, %d glyphs%s",
		  _svfnLatin.cellW, _svfnLatin.cellH, _svfnLatin.bpp, _svfnLatin.glyphs,
		  _svfnLatin.variable ? ", variable width" : "");
}

/**
 * The advance a font in the extended format wants for this character, in
 * game pixels, or -1 when it has no opinion.
 *
 * -1 leaves the game's own charset width in charge and keeps the original
 * line breaks, which is the default. Taking the width from the font lays
 * the glyphs out the way they were drawn, but moves where lines wrap, and
 * not every translation wants that.
 *
 * v0-v2 never get here: CharsetRendererV2::getCharWidth() returns a fixed
 * 8 because the scripts lay their screens out on that grid.
 */
int ScummEngine::getSvfnWidth(uint16 chr) const {
	if (!_svfnLatinMetrics)
		return -1;

	// Single byte characters come from the Latin font, double byte ones
	// from whichever CJK font the current charset selected.
	const SvfnFont &font = (chr < 256) ? _svfnLatin : _svfn;
	if (!font.valid || !font.variable || !font.metrics)
		return -1;

	const int idx = (chr < 256) ? chr : get2byteCharIndex(chr);
	if (idx < 0 || idx >= font.glyphs)
		return -1;

	const int div = _koreanHiResScale > 0 ? _koreanHiResScale : 1;
	const int advance = font.metrics[idx * 4];

	// Round to nearest: truncating loses up to a pixel per character and
	// the error piles up across a line.
	return (advance + div / 2) / div;
}

void ScummEngine::loadKorFont() {
	Common::File fp;

	// Read the map first: it can name the bitmap fonts and the glyph count,
	// both of which this function needs. It is idempotent, so the call later
	// on for the hi-res settings is harmless.
	loadKorTtfConfig();

	// The map may point at another translation's fonts; the glyph count has
	// to follow the code page, since it decides how large each file is.
	int numChar = _cjkFontGlyphs > 0 ? _cjkFontGlyphs : 2350;
	const Common::String multiPattern = _cjkFontPattern.empty()
		? Common::String("korean%02d.fnt") : _cjkFontPattern;
	_useCJKMode = true;

	if (_game.version < 7 || _game.id == GID_FT)
		_useMultiFont = true;

	if (_useMultiFont) {
		debug("Loading CJK Multi Font System");
		_numLoadedFont = 0;
		_2byteFontPtr = nullptr;
		_2byteWidth = 0;
		_2byteHeight = 0;
		for (int i = 0; i < 20; i++) {
			Common::Path fontFile(buildCJKFontName(multiPattern, i));
			_2byteMultiFontPtr[i] = nullptr;
			if (fp.open(fontFile)) {
				_numLoadedFont++;

				// Read the whole file: the extended format is described by
				// a header we have to look at before we know how big the
				// glyph data is, and the old one is small enough that
				// slurping it costs nothing.
				const uint32 fileSize = (uint32)fp.size();
				byte *raw = new byte[fileSize];
				fp.seek(0);
				fp.read(raw, fileSize);
				fp.close();

				if (parseSvfnHeader(raw, fileSize, _svfnMulti[i])) {
					_2byteMultiFontPtr[i] = raw;
					// The font was baked for the scaled surface, but the
					// game lays text out in its own 320x200 coordinates:
					// report the cell in those, or every glyph advances by
					// the scale factor twice over.
					const int div = _koreanHiResScale > 0 ? _koreanHiResScale : 1;
					_2byteMultiWidth[i] = _svfnMulti[i].cellW / div;
					_2byteMultiHeight[i] = _svfnMulti[i].cellH / div;
					_2byteMultiShadow[i] = 1;   // the format carries coverage instead
					debug(1, "SVFN font #%d: %dx%d %dbpp, %d glyphs",
						  i, _svfnMulti[i].cellW, _svfnMulti[i].cellH,
						  _svfnMulti[i].bpp, _svfnMulti[i].glyphs);
				} else {
					_2byteMultiShadow[i] = raw[1];
					_2byteMultiWidth[i] = raw[2];
					_2byteMultiHeight[i] = raw[3];

					int fontSize = ((_2byteMultiWidth[i] + 7) / 8) * _2byteMultiHeight[i] * numChar;
					_2byteMultiFontPtr[i] = new byte[fontSize];
					memcpy(_2byteMultiFontPtr[i], raw + 4,
						   MIN<uint32>((uint32)fontSize, fileSize > 4 ? fileSize - 4 : 0));
					delete[] raw;
				}
				if (_2byteFontPtr == nullptr) {	// for non-initialized Smushplayer drawChar
					_2byteFontPtr = _2byteMultiFontPtr[i];
					_2byteWidth = _2byteMultiWidth[i];
					_2byteHeight = _2byteMultiHeight[i];
					_2byteShadow = _2byteMultiShadow[i];
					_svfn = _svfnMulti[i];
				}
			}
		}
		if (_numLoadedFont == 0) {
			warning("Cannot load any font for multi font");
			_useMultiFont = false;
		} else {
			debug("%d fonts are loaded", _numLoadedFont);
		}
	}

	if (!_useMultiFont) {
		debug("Loading CJK Single Font System");
		const char *const singleName = _cjkFontSingle.empty()
			? "korean.fnt" : _cjkFontSingle.c_str();
		if (fp.open(singleName)) {
			const uint32 fileSize = (uint32)fp.size();
			byte *raw = new byte[fileSize];
			fp.seek(0);
			fp.read(raw, fileSize);
			fp.close();

			if (parseSvfnHeader(raw, fileSize, _svfn)) {
				_2byteFontPtr = raw;
				const int div = _koreanHiResScale > 0 ? _koreanHiResScale : 1;
				_2byteWidth = _svfn.cellW / div;
				_2byteHeight = _svfn.cellH / div;
				_2byteShadow = 1;
				debug(1, "SVFN font: %dx%d %dbpp, %d glyphs",
					  _svfn.cellW, _svfn.cellH, _svfn.bpp, _svfn.glyphs);
			} else {
				_2byteWidth = raw[2];
				_2byteHeight = raw[3];

				const int fontSize = ((_2byteWidth + 7) / 8) * _2byteHeight * numChar;
				_2byteFontPtr = new byte[fontSize];
				memcpy(_2byteFontPtr, raw + 4,
					   MIN<uint32>((uint32)fontSize, fileSize > 4 ? fileSize - 4 : 0));
				delete[] raw;
			}
		} else {
			error("Couldn't load any font: %s", fp.getName());
		}
	}

	// Hi-res text mode for the Korean fan translations. This works like the
	// FM-Towns/PC98 Japanese modes: the game graphics stay at their original
	// low resolution and get scaled up on output, while the text overlay is
	// kept at the higher resolution, so that the glyphs can be rendered with
	// much more detail than the 320x200 framebuffer would ever allow.
	//
	// The font map is read first: it may name a font, which implies hi-res
	// mode, and it may pin the scale outright.
	loadKorTtfConfig();

	loadSvfnLatin();

	if (_koreanHiResScale > 1) {
		if (_game.platform == Common::kPlatformFMTowns || _game.platform == Common::kPlatformPCEngine ||
			_game.platform == Common::kPlatformSegaCD || _game.platform == Common::kPlatformNES ||
			_game.platform == Common::kPlatformMacintosh || _game.platform == Common::kPlatformC64 ||
			_game.platform == Common::kPlatformApple2GS) {
			// These platforms already own the text surface / do their own scaling.
			debug(1, "Korean hi-res mode not available on this platform, disabling");
			_koreanHiResScale = 1;
		} else {
			_textSurfaceMultiplier = _koreanHiResScale;
			debug(1, "Korean hi-res text mode enabled (scale %d)", _koreanHiResScale);
			loadKorTtfFont();
		}
	}

	return;
}

/**
 * Load the TrueType font used to replace the bitmap glyphs in Korean hi-res
 * mode. This is entirely optional: if no font is configured or FreeType2 is
 * not available, we silently fall back to the scaled bitmap fonts.
 *
 * The point size is derived from the bitmap font height so that the TTF text
 * occupies roughly the same space as the original, just with m times more
 * detail.
 */

namespace {

enum KoreanTtfRole {
	kKorTtfDefaultRole = 0,
	kKorTtfBoldRole = 1,
	kKorTtfTitleRole = 2
};

int getKoreanTtfRoleFromName(const Common::String &name) {
	if (name.equalsIgnoreCase("title"))
		return kKorTtfTitleRole;
	if (name.equalsIgnoreCase("bold"))
		return kKorTtfBoldRole;
	return kKorTtfDefaultRole;
}

// Resolve a path read out of the config or the map file. Relative paths are
// taken to be relative to baseDir - the game folder for the map itself, the
// map's own folder for the fonts it names - so a translation can ship its
// fonts alongside the game and stay movable. Absolute paths are used as is.
static Common::Path resolveKorTtfPath(const Common::String &value, const Common::Path &baseDir) {
	if (value.empty())
		return Common::Path();

	const char first = value[0];
	const bool absolute = (first == '/' || first == '\\') ||
						  (value.size() > 2 && value[1] == ':');
	if (absolute)
		return Common::Path(value);

	return baseDir.join(Common::Path(value));
}

} // End of anonymous namespace

/**
 * Read the font map and the related config keys. This runs before hi-res mode
 * is decided, because the map is allowed to ask for a scale itself: a
 * translation can then ship a single map file and need no config at all.
 */
void ScummEngine::loadKorTtfConfig() {
	// Called from both setupScumm() and loadCJKFont(); the first one settles
	// the scale and the alpha mode, the second finds the state already there.
	if (_ttfConfigLoaded || !isHiResTextTarget())
		return;
	_ttfConfigLoaded = true;

	// Default the code page to whatever the language implies; the map can
	// still override it for a translation that ships in another encoding.
	switch (_language) {
	case Common::JA_JPN:
		_ttfCodePage = Common::kWindows932;
		break;
	case Common::ZH_CHN:
		_ttfCodePage = Common::kWindows936;
		break;
	case Common::ZH_TWN:
		_ttfCodePage = Common::kWindows950;
		break;
	default:
		_ttfCodePage = Common::kWindows949;
		break;
	}

	_korTtfHeightRoles.clear();

	// The map may be given as an absolute path, or relative to the game
	// folder so that a translation can ship its own fonts and stay movable.
	// With no key at all we still look for a conventionally named file in
	// the game folder, which makes the feature work without any config.
	Common::Path mapPath;
	if (ConfMan.hasKey("korean_ttf_map"))
		mapPath = resolveKorTtfPath(ConfMan.get("korean_ttf_map"), ConfMan.getPath("path"));
	else
		mapPath = ConfMan.getPath("path").appendComponent("korean_ttf.map");

	if (!mapPath.empty()) {
		Common::FSNode probe(mapPath);
		if (probe.exists())
			loadKorTtfMap(mapPath);
		else if (ConfMan.hasKey("korean_ttf_map"))
			warning("SCUMM::Font: Korean TTF map not found: '%s'", mapPath.toString().c_str());
	}

	// Backward-compatible fallback: if no map file supplied a default font,
	// keep accepting the earlier per-role config keys.
#ifdef USE_FREETYPE2
	if (_korTtfPath.empty() && ConfMan.hasKey("korean_ttf_font"))
		_korTtfPath = Common::Path(ConfMan.getPath("korean_ttf_font"));
	if (_korTtfBoldPath.empty()) {
		if (ConfMan.hasKey("korean_ttf_bold_font"))
			_korTtfBoldPath = Common::Path(ConfMan.getPath("korean_ttf_bold_font"));
		else
			_korTtfBoldPath = _korTtfPath;
	}
	if (_korTtfTitlePath.empty()) {
		if (ConfMan.hasKey("korean_ttf_title_font"))
			_korTtfTitlePath = Common::Path(ConfMan.getPath("korean_ttf_title_font"));
		else
			_korTtfTitlePath = _korTtfPath;
	}

	// A TrueType font is only worth having at a higher resolution, so having
	// one configured implies hi-res mode. korean_hires_scale still wins if it
	// was set explicitly, then the map's own [hires] scale, then this.
	if (!ConfMan.hasKey("korean_hires_scale") && !_korTtfPath.empty() && _koreanHiResScale < 2)
		_koreanHiResScale = 2;
#endif
}

void ScummEngine::loadKorTtfFont() {
#ifdef USE_FREETYPE2
	if (_korTtfPath.empty())
		return;

	// v0-v2 place every glyph in a fixed cell that the scripts rely on.
	// Run-at-a-time rendering spaces the glyphs by the font's own advance
	// and drifts off that grid, so keep those games per-character.
	if (_game.version <= 2)
		_korTtfStringMode = false;

	Common::FSNode fontNode(_korTtfPath);
	if (!fontNode.exists() || fontNode.isDirectory()) {
		warning("SCUMM::Font: Korean TTF font not found: '%s'", _korTtfPath.toString().c_str());
		return;
	}

	_korTtfEnabled = true;

	// Default map when no explicit [map] was provided: large 12px bitmap
	// fonts are title/credit style; compact 8px fonts become bold at 3x
	// because their line box is exactly 24px.
	if (_korTtfHeightRoles.empty()) {
		_korTtfHeightRoles[8] = kKorTtfBoldRole;
		_korTtfHeightRoles[12] = kKorTtfTitleRole;
	}

	// Preload the font for the charset that is active right now; further
	// sizes are created lazily as the game switches charsets.
	selectKorTtfFont(_2byteHeight * _koreanHiResScale);
#endif
}

// Look a key up in a section, letting a more specific section override the
// generic one. Sections are tried from the most specific to the least:
//
//   [fonts:maniac]   this game only        (game id)
//   [fonts:v2]       this SCUMM version    (engine version)
//   [fonts]          everything
//
// so one map file can carry the settings for several games and versions
// without them stepping on each other.
bool ScummEngine::getKorTtfMapKey(const Common::INIFile &map, const Common::String &key,
								  const Common::String &section, Common::String &value) const {
	if (map.getKey(key, Common::String::format("%s:%s", section.c_str(), _game.gameid), value))
		return true;

	if (map.getKey(key, Common::String::format("%s:v%d", section.c_str(), _game.version), value))
		return true;

	return map.getKey(key, section, value);
}

void ScummEngine::loadKorTtfMap(const Common::Path &mapPath) {
	Common::FSNode mapNode(mapPath);
	Common::SeekableReadStream *stream = mapNode.createReadStream();
	if (!stream) {
		warning("SCUMM::Font: Could not open Korean TTF map '%s'", mapPath.toString().c_str());
		return;
	}

	Common::INIFile map;
	map.requireKeyValueDelimiter();
	if (!map.loadFromStream(*stream)) {
		delete stream;
		warning("SCUMM::Font: Could not parse Korean TTF map '%s'", mapPath.toString().c_str());
		return;
	}
	delete stream;

	Common::String value;

	// The TrueType sections only mean something in a FreeType build. The
	// rest of the map -- the code page, the bitmap font names and the
	// translation bundle -- drives the CLUT8 path and is read either way.
#ifdef USE_FREETYPE2
	if (getKorTtfMapKey(map, "default", "fonts", value))
		_korTtfPath = resolveKorTtfPath(value, mapPath.getParent());
	if (getKorTtfMapKey(map, "bold", "fonts", value))
		_korTtfBoldPath = resolveKorTtfPath(value, mapPath.getParent());
	if (getKorTtfMapKey(map, "title", "fonts", value))
		_korTtfTitlePath = resolveKorTtfPath(value, mapPath.getParent());

	// Optional [sizes] section: pin a role to an exact pixel size instead of
	// letting the auto-fit pick one. Pixel fonts only render cleanly at
	// integer multiples of their design grid (16px for Neo Dunggeunmo, for
	// instance), and the scaled line boxes rarely land on such a multiple.
	//
	//   [sizes]
	//   title=32          ; render at exactly 32px
	//   bold=16x2         ; render at 32px, then downscale by 2 to fit 16px
	//   default=12pt      ; 12 points at the current scale, i.e. 12 * scale px
	//
	// The "NxM" form supersamples: the glyph is rasterised M times larger and
	// box-filtered back down, which keeps a pixel font on its native grid
	// while still fitting a line box that is not a multiple of it. The "pt"
	// suffix is resolution independent: the size follows korean_hires_scale,
	// so the same map looks the same at 2x and 3x.
	static const char *const roleNames[] = { "default", "bold", "title" };
	for (int r = 0; r < ARRAYSIZE(roleNames); ++r) {
		if (!getKorTtfMapKey(map, roleNames[r], "sizes", value))
			continue;

		const char *sep = strchr(value.c_str(), 'x');
		int size = atoi(value.c_str());
		const int super = sep ? atoi(sep + 1) : 1;

		if (strstr(value.c_str(), "pt"))
			size *= _koreanHiResScale;

		if (size > 0)
			_korTtfRoleSizes[r] = size;
		if (super > 1)
			_korTtfRoleSupersample[r] = super;
	}

	// Optional [latin] section: route the single byte characters (Latin
	// letters, digits, punctuation) through the TrueType renderer as well,
	// so a line does not mix TTF Hangul with the original bitmap glyphs.
	//
	//   [latin]
	//   enabled=true      ; render 1 byte characters with the TTF too
	//   font=<path>       ; optional, defaults to the Korean font
	//
	// The advance width still comes from the game's own font, so the text
	// keeps its original layout and line breaks.
	if (getKorTtfMapKey(map, "font", "latin", value))
		_korTtfLatinPath = resolveKorTtfPath(value, mapPath.getParent());
	if (getKorTtfMapKey(map, "metrics", "latin", value))
		_korTtfMetrics = value.equalsIgnoreCase("ttf");

	// [render] mode=string draws whole runs at once instead of one glyph
	// at a time, letting the font place the characters within a line.
	if (getKorTtfMapKey(map, "mode", "render", value))
		_korTtfStringMode = value.equalsIgnoreCase("string");

#endif

	if (getKorTtfMapKey(map, "enabled", "latin", value))
		_korTtfLatin = (value.equalsIgnoreCase("true") || atoi(value.c_str()) != 0);

	// [latin] bitmap= names a font for the single byte range in the same
	// extended format as the Hangul one. Without it a line drawn from an
	// 8bpp font still shows pixel-doubled Latin letters, since those keep
	// coming from the game's own 1bpp charset.
	//
	//   [latin]
	//   enabled=true
	//   bitmap=latin24.fnt
	//   metrics=bitmap        ; or "game" to keep the original layout
	if (getKorTtfMapKey(map, "bitmap", "latin", value)) {
		_svfnLatinName = value;
		_korTtfLatin = true;
	}
	if (getKorTtfMapKey(map, "metrics", "latin", value))
		_svfnLatinMetrics = value.equalsIgnoreCase("bitmap");

	// [shadow] forces an outline or drop shadow on the hi-res glyphs. The
	// engine's own setting describes the game's bitmap font, which a
	// replacement font has no reason to match.
	//
	//   [shadow]
	//   mode=outline      ; none | drop | outline | stroke | game
	//   offset=2          ; scaled pixels; omit to follow the scale
	//   color=0           ; palette index; omit to use the game's
	if (getKorTtfMapKey(map, "mode", "shadow", value)) {
		if (value.equalsIgnoreCase("none"))
			_hiResShadowMode = kHiResShadowNone;
		else if (value.equalsIgnoreCase("drop"))
			_hiResShadowMode = kHiResShadowDrop;
		else if (value.equalsIgnoreCase("outline"))
			_hiResShadowMode = kHiResShadowOutline;
		else if (value.equalsIgnoreCase("stroke"))
			_hiResShadowMode = kHiResShadowStroke;
		else
			_hiResShadowMode = kHiResShadowGame;
	}
	if (getKorTtfMapKey(map, "offset", "shadow", value))
		_hiResShadowOffset = atoi(value.c_str());
	if (getKorTtfMapKey(map, "color", "shadow", value)) {
		_hiResShadowColor = (byte)atoi(value.c_str());
		_hiResShadowColorSet = true;
	}

	// [hires] scale pins the text surface multiplier from the map, so a
	// translation can pick the resolution its font was drawn for without
	// the user having to add a config key. An explicit korean_hires_scale
	// still wins.
	//
	//   [hires]
	//   scale=2         ; 1 disables the mode, 3 is the maximum
	//   alpha=true      ; 32 bit anti-aliased text, needs an RGB backend
	if (!ConfMan.hasKey("korean_hires_scale") && getKorTtfMapKey(map, "scale", "hires", value)) {
		const int scale = atoi(value.c_str());
		if (scale >= 1 && scale <= 3)
			_koreanHiResScale = scale;
		else
			warning("SCUMM::Font: Korean TTF map asks for scale %d, ignoring", scale);
	}
	if (!ConfMan.hasKey("korean_alpha_text") && getKorTtfMapKey(map, "alpha", "hires", value))
		ConfMan.setBool("korean_alpha_text", value.equalsIgnoreCase("true") || atoi(value.c_str()) != 0);

	// [encoding] names the code page the double byte characters are in, for
	// translations that are not Korean. The default follows the language, so
	// this only has to be set when the two disagree.
	//
	//   [encoding]
	//   codepage=cp932      ; sjis | gbk | big5 | uhc | johab
	if (getKorTtfMapKey(map, "codepage", "encoding", value)) {
		if (value.equalsIgnoreCase("cp932") || value.equalsIgnoreCase("sjis"))
			_ttfCodePage = Common::kWindows932;
		else if (value.equalsIgnoreCase("cp936") || value.equalsIgnoreCase("gbk"))
			_ttfCodePage = Common::kWindows936;
		else if (value.equalsIgnoreCase("cp949") || value.equalsIgnoreCase("uhc"))
			_ttfCodePage = Common::kWindows949;
		else if (value.equalsIgnoreCase("cp950") || value.equalsIgnoreCase("big5"))
			_ttfCodePage = Common::kWindows950;
		else if (value.equalsIgnoreCase("johab"))
			_ttfCodePage = Common::kJohab;
		else
			warning("SCUMM::Font: unknown TTF code page '%s', keeping the default", value.c_str());
	}

	// [bitmap] names the engine's own bitmap fonts, for translations that do
	// not follow the Korean naming. The glyph count has to match the code
	// page's character set: 2350 for KS X 1001, 6879 for Shift-JIS and so on.
	//
	//   [bitmap]
	//   multi=japanese%02d.fnt   ; numbered set, one per charset
	//   single=japanese.fnt      ; fallback when no numbered file is found
	//   glyphs=6879
	if (getKorTtfMapKey(map, "multi", "bitmap", value))
		_cjkFontPattern = value;
	if (getKorTtfMapKey(map, "single", "bitmap", value))
		_cjkFontSingle = value;
	if (getKorTtfMapKey(map, "glyphs", "bitmap", value)) {
		const int glyphs = atoi(value.c_str());
		if (glyphs > 0)
			_cjkFontGlyphs = glyphs;
		else
			warning("SCUMM::Font: bad glyph count '%s', keeping the default", value.c_str());
	}

	// [translation] names the runtime translation bundle when it is not the
	// default korean.trs. The format is the same either way.
	//
	//   [translation]
	//   file=japanese.trs
	if (getKorTtfMapKey(map, "file", "translation", value))
		_cjkTrsName = value;

	// The height map is merged rather than overridden: the generic section
	// provides the defaults and the specific ones refine individual heights.
	const Common::String mapVer = Common::String::format("map:v%d", _game.version);
	const Common::String mapGame = Common::String::format("map:%s", _game.gameid);
	const char *const sections[] = { "map", mapVer.c_str(), mapGame.c_str() };

	for (int s = 0; s < ARRAYSIZE(sections); ++s) {
		if (!map.hasSection(sections[s]))
			continue;

		const Common::INIFile::SectionKeyList keys = map.getKeys(sections[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin(); it != keys.end(); ++it) {
			if (it->key.hasPrefix("height_")) {
				const int height = atoi(it->key.c_str() + 7);
				if (height > 0)
					_korTtfHeightRoles[height] = getKoreanTtfRoleFromName(it->value);
			}
		}
	}

}

/**
 * Pick (and, if needed, build) the TrueType instance matching a given line box.
 *
 * The Korean multi-font system swaps _2byteHeight whenever the game changes
 * charset -- 12 pixels for dialogue, 8 for the verb/inventory interface, and
 * so on. Each of those needs its own TTF size, otherwise text drawn for the
 * small charsets would overflow its line and collide with the next one.
 */
void ScummEngine::selectKorTtfFont(int lineBox) {
#ifdef USE_FREETYPE2
	if (!_korTtfEnabled || lineBox <= 0)
		return;

	int role = kKorTtfDefaultRole;
	if (_korTtfHeightRoles.contains(_2byteHeight))
		role = _korTtfHeightRoles[_2byteHeight];
	else if (_2byteHeight >= 12)
		role = kKorTtfTitleRole;
	else if (lineBox >= 24)
		role = kKorTtfBoldRole;

	Common::Path fontPath = _korTtfPath;
	if (role == kKorTtfTitleRole && !_korTtfTitlePath.empty())
		fontPath = _korTtfTitlePath;
	else if (role == kKorTtfBoldRole && !_korTtfBoldPath.empty())
		fontPath = _korTtfBoldPath;

	const int cacheKey = lineBox + role * 10000;

	// Fast path: same line box / font family as the last call.
	if (cacheKey == _korTtfCurLineBox)
		return;

	if (_korTtfFonts.contains(cacheKey)) {
		_korTtfCurLineBox = cacheKey;
		_korTtfFont = _korTtfFonts[cacheKey];
		_korTtfSupersample = _korTtfRoleSupersample.contains(role)
			? CLIP<int>(_korTtfRoleSupersample[role], 1, 8) : 1;
		if (_korTtfFont)
			// A font taller than the line box centres on it and overhangs
			// both ways; the dirty rectangle carries a cell of slack on
			// each side so the parts outside still get composited.
			_korTtfYOffset = (lineBox - _korTtfFont->getFontHeight() / _korTtfSupersample) / 2;
		return;
	}

	Common::FSNode fontNode(fontPath);

	// The engine advances to the next line using the *bitmap* metrics, and
	// several games squeeze lines tighter than the nominal glyph box (MI1
	// draws its inventory straight from script with a hardcoded pitch).
	// Leave a little headroom so descenders never bleed into the next line.
	int size = lineBox - _koreanHiResScale;
	bool pinned = false;

	// A per-role size from the font map wins over the auto-fit, and the
	// global korean_ttf_size overrides everything.
	if (_korTtfRoleSizes.contains(role)) {
		size = _korTtfRoleSizes[role];
		pinned = true;
	}
	if (ConfMan.hasKey("korean_ttf_size")) {
		size = CLIP<int>(ConfMan.getInt("korean_ttf_size"), 6, 128);
		pinned = true;
	}

	// Supersampling: rasterise the glyphs N times larger than the size we
	// actually want, then let drawKorTtfChar() box-filter them down. This
	// keeps a pixel font on an integer multiple of its design grid even when
	// the target line box is not one.
	int supersample = 1;
	if (_korTtfRoleSupersample.contains(role))
		supersample = CLIP<int>(_korTtfRoleSupersample[role], 1, 8);

	if (supersample > 1)
		size *= supersample;

	Graphics::Font *result = nullptr;

	// Monochrome rendering keeps the CLUT8 text surface (and its 0xFD
	// transparency mask) working exactly as before: a glyph pixel is either
	// fully set or not set at all.
	//
	// The engine lays out lines using the bitmap font metrics, so a TTF whose
	// cell height exceeds that box would make consecutive lines overlap.
	// Shrink the requested size until the rendered height fits.
	for (int attempt = 0; attempt < 16 && size >= 6; ++attempt) {
		Common::SeekableReadStream *stream = fontNode.createReadStream();
		if (!stream)
			break;

		Graphics::Font *font = Graphics::loadTTFFont(stream, DisposeAfterUse::YES, size,
						Graphics::kTTFSizeModeCell, 0, 0,
						_koreanAlphaText ? Graphics::kTTFRenderModeLight
										 : Graphics::kTTFRenderModeMonochrome);
		if (!font)
			break;

		if (font->getFontHeight() / supersample <= lineBox - _koreanHiResScale || pinned) {
			result = font;
			break;
		}

		delete font;
		size -= supersample;
	}

	// Cache negative results too, so a failing size is not retried endlessly.
	_korTtfFonts[cacheKey] = result;
	_korTtfFont = result;
	_korTtfCurLineBox = cacheKey;
	_korTtfSupersample = supersample;

	if (!result) {
		warning("SCUMM::Font: Could not fit Korean TTF font into a %d pixel line box", lineBox);
		return;
	}

	_korTtfYOffset = (lineBox - result->getFontHeight() / supersample) / 2;

	debug(1, "Korean TTF font: role %d size %d ss %d (height %d, lineBox %d, yOffset %d)",
		  role, size, supersample, result->getFontHeight(), lineBox, _korTtfYOffset);
#endif
}

/**
 * Render one CP949 double-byte character with the TrueType font.
 *
 * The engine stores the two bytes of a Korean character byte-swapped in a
 * single int (low byte first, see printString()), so they have to be put back
 * in order before the shared CP949 -> Unicode table can be used. That table
 * lives in common/str-enc.cpp and is backed by encoding.dat, so no external
 * iconv dependency is involved.
 *
 * Returns false when the character cannot be rendered, in which case the
 * caller should fall back to the bitmap glyph.
 */
/**
 * Map a game character to Unicode so the TrueType font can be asked for a
 * glyph. Single byte values pass through: the fan translations keep ASCII
 * where the original had it. Double byte pairs go through the code page the
 * font map named, which defaults to the one the engine's own CJK mode uses.
 *
 * The engine packs the pair byte-swapped, hence the shuffling below.
 */
uint16 ScummEngine::ttfCharToUnicode(uint16 chr) const {
	if (chr < 256)
		return chr;

	const uint8 hi = chr & 0xFF;
	const uint8 lo = chr >> 8;

	if (_ttfCodePage == Common::kWindows949)
		return Common::convertUHCToUCS(hi, lo);

	// The other code pages have no single character entry point, so convert
	// a two byte string and take the first code point.
	const char pair[3] = { (char)hi, (char)lo, 0 };
	const Common::U32String out = Common::convertToU32String(pair, _ttfCodePage);
	return out.empty() ? 0 : (uint16)out[0];
}

/**
 * Append a character to the current run instead of drawing it straight away.
 * A run is flushed when the pen jumps, the colour changes or the frame ends;
 * per-character colour changes still come out right - they just split the
 * line into several runs.
 */
/**
 * Collect characters into a run and draw them with one TTF call.
 *
 * Drawing a glyph at a time forces every character onto the game's own
 * grid: the advance is rounded to a game pixel, punctuation ends up on a
 * baseline of its own, and the dirty rectangle has to be widened by hand.
 * Handing whole runs to the font instead lets it place the glyphs, which
 * is what the metrics were designed for.
 *
 * A run ends when the caller moves somewhere else or changes colour, so
 * per-character colour changes still come out right - they just split the
 * line into several runs.
 */
bool ScummEngine::korTtfRunAppend(uint16 chr, Graphics::Surface &dest, int x, int y, byte color, byte shadowColor) {
#ifdef USE_FREETYPE2
	if (!_korTtfFont)
		return false;

	// A run is flushed later, from drawDirtyScreenParts(). Only the text
	// surface is guaranteed to still be alive by then; callers that draw
	// into a surface of their own are refused here and fall back to the
	// immediate per-character path.
	if (&dest != &_textSurface)
		return false;

	const uint16 unicode = ttfCharToUnicode(chr);
	if (!unicode)
		return false;

	// Continue the current run when this character picks up exactly where
	// the last one left off, in the same colour and on the same line.
	// The caller's x is where the game would have put the character on its
	// own grid; inside a run the font decides the spacing instead, so only
	// the accumulated drift matters. Allow a cell of it before breaking:
	// rounding the advance to game pixels leaves a pixel or two per glyph.
	const int expectedX = _korTtfRunX + _korTtfFont->getStringWidth(_korTtfRun);
	const int drift = ABS(expectedX - x);
	const int tolerance = _2byteWidth * _koreanHiResScale;

	const bool contiguous = _korTtfRunActive
			&& _korTtfRunDest == &dest
			&& _korTtfRunY == y
			&& _korTtfRunColor == color
			&& _korTtfRunShadow == shadowColor
			&& drift <= tolerance;

	if (!contiguous) {
		korTtfRunFlush();
		_korTtfRunDest = &dest;
		_korTtfRunX = x;
		_korTtfRunY = y;
		_korTtfRunColor = color;
		_korTtfRunShadow = shadowColor;
		_korTtfRunActive = true;
	}

	_korTtfRun += (Common::u32char_type_t)unicode;
	return true;
#else
	return false;
#endif
}

void ScummEngine::korTtfRunFlush() {
#ifdef USE_FREETYPE2
	if (!_korTtfRunActive || _korTtfRun.empty() || !_korTtfFont || !_korTtfRunDest) {
		_korTtfRun.clear();
		_korTtfRunActive = false;
		return;
	}

	Graphics::Surface &dest = *_korTtfRunDest;
	const int ty = _korTtfRunY + _korTtfYOffset;

	if (_koreanAlphaText && _korAlphaSurface.getPixels()) {
		// Rasterise the whole run into a scratch surface, then transfer the
		// colour to the text surface and the coverage to its companion.
		const int rw = _korTtfFont->getStringWidth(_korTtfRun) + _korTtfFont->getFontHeight();
		const int rh = _korTtfFont->getFontHeight() * 2;

		const Graphics::PixelFormat covFmt(4, 8, 8, 8, 8, 24, 16, 8, 0);
		Graphics::Surface cov;
		cov.create(rw, rh, covFmt);
		cov.fillRect(Common::Rect(0, 0, rw, rh), 0);

		// Draw a quarter of the way down so ascenders have room too.
		const int padY = _korTtfFont->getFontHeight() / 4;
		_korTtfFont->drawString(&cov, _korTtfRun, 0, padY, rw,
								covFmt.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF));

		const int shadowOff = hiResShadowOffset();

		for (int gy = 0; gy < rh; ++gy) {
			const uint32 *covRow = (const uint32 *)cov.getBasePtr(0, gy);

			for (int gx = 0; gx < rw; ++gx) {
				uint8 ca, cr, cg, cb;
				covFmt.colorToARGB(covRow[gx], ca, cr, cg, cb);
				if (!ca)
					continue;

				const int px = _korTtfRunX + gx;
				const int py = ty + gy - padY;

				const byte runShadowInk = _hiResShadowColorSet ? _hiResShadowColor : _korTtfRunShadow;
				if (hiResShadowMode() != kHiResShadowNone && runShadowInk != _korTtfRunColor) {
					const int sxp = px + shadowOff;
					const int syp = py + shadowOff;
					// The coverage channel is a separate surface: check it
					// against its own bounds, not the destination's.
					if (sxp >= 0 && syp >= 0 && sxp < dest.w && syp < dest.h
							&& sxp < _korAlphaSurface.w && syp < _korAlphaSurface.h) {
						byte *aDst = (byte *)_korAlphaSurface.getBasePtr(sxp, syp);
						if (*aDst < ca) {
							*(byte *)dest.getBasePtr(sxp, syp) = runShadowInk;
							*aDst = ca;
						}
					}
				}

				if (px < 0 || py < 0 || px >= dest.w || py >= dest.h
						|| px >= _korAlphaSurface.w || py >= _korAlphaSurface.h)
					continue;

				*(byte *)dest.getBasePtr(px, py) = _korTtfRunColor;
				*(byte *)_korAlphaSurface.getBasePtr(px, py) = ca;
			}
		}

		cov.free();
	} else {
		const byte runShadow = _hiResShadowColorSet ? _hiResShadowColor : _korTtfRunShadow;
		if (hiResShadowMode() != kHiResShadowNone && runShadow != _korTtfRunColor) {
			const int d = hiResShadowOffset();
			_korTtfFont->drawString(&dest, _korTtfRun, _korTtfRunX + d, ty + d, dest.w, runShadow);
			_korTtfFont->drawString(&dest, _korTtfRun, _korTtfRunX + d, ty, dest.w, runShadow);
			_korTtfFont->drawString(&dest, _korTtfRun, _korTtfRunX, ty + d, dest.w, runShadow);
		}
		_korTtfFont->drawString(&dest, _korTtfRun, _korTtfRunX, ty, dest.w, _korTtfRunColor);
	}

	_korTtfRun.clear();
	_korTtfRunActive = false;
#endif
}

bool ScummEngine::drawKorTtfChar(Graphics::Surface &dest, uint16 chr, int x, int y, byte color, byte shadowColor) {
#ifdef USE_FREETYPE2
	if (!_korTtfEnabled)
		return false;
	// Some games (MI1 for instance) draw parts of their UI straight from
	// script without going through setCurID(), so the charset switch hook is
	// not enough: re-check the current line box for every glyph. This is
	// cheap because selectKorTtfFont() hits the cache on the common path.
	//
	// Single byte characters are excluded: _2byteHeight describes the Hangul
	// metrics and is not updated for them, so re-selecting here would pick a
	// different size mid-line and break the shared baseline.
	if (chr >= 256)
		selectKorTtfFont(_2byteHeight * _koreanHiResScale);

	if (!_korTtfFont)
		return false;

	// Single byte characters are already their own code point; only the
	// double byte ones need the CP949 -> Unicode conversion.
	const uint16 unicode = ttfCharToUnicode(chr);
	if (!unicode)
		return false;

	// String mode: hand the character to the run collector and let the
	// font lay the line out; korTtfRunFlush() does the actual drawing.
	if (_korTtfStringMode && korTtfRunAppend(chr, dest, x, y, color, shadowColor))
		return true;

	int tx = x;

	// With TTF metrics the caller's x was rounded down to a game pixel;
	// re-derive it from the accumulated sub-pixel pen instead. The pen is
	// reset whenever the caller jumps somewhere else (a new line, say).
	if (_korTtfMetrics) {
		if (_korTtfPenLeft != x)
			_korTtfPenX = x;
		tx = _korTtfPenX;
		_korTtfPenX += _korTtfFont->getCharWidth(unicode) / _korTtfSupersample;
		_korTtfPenLeft = (_korTtfPenX / _koreanHiResScale) * _koreanHiResScale;
	}

	const int ty = y + _korTtfYOffset;





	// Alpha path: rasterise the glyph once with anti-aliasing, then store the
	// colour in the text surface and the coverage in the companion channel.
	// compositeHiResText() blends the two against the upscaled background.
	if (_koreanAlphaText && _korAlphaSurface.getPixels()) {
		// Glyphs are not confined to the advance box: descenders reach
		// below the baseline, commas and parentheses stick out further
		// than the nominal line height, and italics can overhang on the
		// sides. Size the scratch from the glyph's own bounding box and
		// remember where its origin ended up, so nothing is clipped and
		// the piece still lands at the right place on screen.
		const Common::Rect gbox = _korTtfFont->getBoundingBox(unicode);
		const int originX = MIN(0, (int)gbox.left);
		const int originY = MIN(0, (int)gbox.top);
		const int gw = MAX((int)gbox.right, _korTtfFont->getCharWidth(unicode)) - originX;
		const int gh = MAX((int)gbox.bottom, _korTtfFont->getFontHeight()) - originY;
		if (gw <= 0 || gh <= 0)
			return false;

		// Rasterise into a 32bpp scratch surface: drawAlphaChar() writes the
		// glyph colour with the coverage in the alpha channel, and for a
		// CLUT8 destination it would have nothing meaningful to write. We
		// draw opaque white and recover the coverage from the alpha byte.
		const Graphics::PixelFormat covFmt(4, 8, 8, 8, 8, 24, 16, 8, 0);
		Graphics::Surface cov;
		cov.create(gw, gh, covFmt);
		cov.fillRect(Common::Rect(0, 0, gw, gh), 0);
		_korTtfFont->drawAlphaChar(&cov, unicode, -originX, -originY, covFmt.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF));

		// Without TTF metrics the glyph is centred in the game's own advance
		// box; with them the pen already carries the exact position.
		if (!_korTtfMetrics) {
			const int adv = _2byteWidth * _koreanHiResScale;
			if (gw < adv)
				tx += (adv - gw) / 2;
		}

		const int shadowOff = hiResShadowOffset();

		// The scratch was shifted by the glyph origin; undo that here so
		// the parts that reach outside the advance box still land where
		// the font intended.
		const int dx = tx + originX;
		const int dy = ty + originY;


		for (int gy = 0; gy < gh; ++gy) {
			const uint32 *covRow = (const uint32 *)cov.getBasePtr(0, gy);

			for (int gx = 0; gx < gw; ++gx) {
				uint8 ca, cr, cg, cb;
				covFmt.colorToARGB(covRow[gx], ca, cr, cg, cb);
				const byte a = ca;
				if (!a)
					continue;

				// Drop shadow first, at reduced coverage.
				const byte shadowInk = _hiResShadowColorSet ? _hiResShadowColor : shadowColor;
				if (hiResShadowMode() != kHiResShadowNone && shadowInk != color) {
					const int sxp = dx + gx + shadowOff;
					const int syp = dy + gy + shadowOff;
					if (sxp >= 0 && syp >= 0 && sxp < dest.w && syp < dest.h
							&& sxp < _korAlphaSurface.w && syp < _korAlphaSurface.h) {
						byte *aDst = (byte *)_korAlphaSurface.getBasePtr(sxp, syp);
						if (*aDst < a) {
							*(byte *)dest.getBasePtr(sxp, syp) = shadowInk;
							*aDst = a;
						}
					}
				}

				const int px = dx + gx;
				const int py = dy + gy;
				if (px < 0 || py < 0 || px >= dest.w || py >= dest.h
						|| px >= _korAlphaSurface.w || py >= _korAlphaSurface.h)
					continue;

				*(byte *)dest.getBasePtr(px, py) = color;
				*(byte *)_korAlphaSurface.getBasePtr(px, py) = a;
			}
		}

		cov.free();
		return true;
	}

	// Supersampled path: render the glyph into a scratch surface at N times
	// the target size, then box-filter it down. A source block counts as set
	// when at least half of its pixels are, which keeps pixel-font strokes
	// crisp instead of smearing them.
	if (_korTtfSupersample > 1) {
		const int ss = _korTtfSupersample;
		const int bigW = _korTtfFont->getCharWidth(unicode);
		const int bigH = _korTtfFont->getFontHeight();
		if (bigW <= 0 || bigH <= 0)
			return false;

		Graphics::Surface tmp;
		tmp.create(bigW, bigH, Graphics::PixelFormat::createFormatCLUT8());
		tmp.fillRect(Common::Rect(0, 0, bigW, bigH), 0);
		_korTtfFont->drawChar(&tmp, unicode, 0, 0, 1);

		const int outW = bigW / ss;
		const int outH = bigH / ss;
		const int adv = _2byteWidth * _koreanHiResScale;
		int tx = x;
		if (outW > 0 && outW < adv)
			tx += (adv - outW) / 2;

		const int threshold = (ss * ss + 1) / 2;

		// Two passes so the drop shadow never paints over glyph pixels that
		// a later block would have set.
		for (int pass = 0; pass < 2; ++pass) {
			if (pass == 0 && shadowColor == color)
				continue;

			for (int oy = 0; oy < outH; ++oy) {
				for (int ox = 0; ox < outW; ++ox) {
					int hits = 0;
					for (int sy = 0; sy < ss; ++sy) {
						const byte *row = (const byte *)tmp.getBasePtr(ox * ss, oy * ss + sy);
						for (int sx = 0; sx < ss; ++sx)
							hits += row[sx] ? 1 : 0;
					}

					if (hits < threshold)
						continue;

					const int px = tx + ox + (pass == 0 ? _koreanHiResScale : 0);
					const int py = ty + oy + (pass == 0 ? _koreanHiResScale : 0);
					if (px < 0 || py < 0 || px >= dest.w || py >= dest.h)
						continue;

					*(byte *)dest.getBasePtr(px, py) = (pass == 0) ? shadowColor : color;
				}
			}
		}

		tmp.free();
		return true;
	}

	// The glyph is rendered at the requested cell size; center it
	// horizontally in the (scaled) advance box so text doesn't drift.
	const int adv = _2byteWidth * _koreanHiResScale;
	const int gw = _korTtfFont->getCharWidth(unicode);
	if (!_korTtfMetrics && gw > 0 && gw < adv)
		tx += (adv - gw) / 2;

	// Monochrome TTF draws only set pixels, leaving the 0xFD transparency
	// mask untouched everywhere else, so the background shows through.
	const byte monoShadow = _hiResShadowColorSet ? _hiResShadowColor : shadowColor;
	if (hiResShadowMode() != kHiResShadowNone && monoShadow != color) {
		const int d = hiResShadowOffset();
		_korTtfFont->drawChar(&dest, unicode, tx + d, ty + d, monoShadow);
		_korTtfFont->drawChar(&dest, unicode, tx + d, ty, monoShadow);
		_korTtfFont->drawChar(&dest, unicode, tx, ty + d, monoShadow);
	}

	_korTtfFont->drawChar(&dest, unicode, tx, ty, color);

	// Same bookkeeping as the bitmap path: the charset mask will not
	// account for this text, so remember it here.
	noteHiResTextDrawn(x, y, _2byteWidth * _koreanHiResScale,
					   _2byteHeight * _koreanHiResScale, _hiResTextBurnIn);

	return true;
#else
	return false;
#endif
}

/**
 * Where a double byte character sits in a fan translation's font file.
 *
 * The glyph order follows the code page rather than whatever the original
 * release used, since the .fnt was generated from that code page's chart.
 * The engine hands us the pair byte-swapped, hence the shuffling.
 */
int ScummEngine::get2byteCharIndex(int chr) const {
	const uint8 hi = chr % 256;
	const uint8 lo = chr / 256;

	switch (_ttfCodePage) {
	case Common::kWindows932:
		// Shift-JIS: two contiguous lead byte ranges, 188 trail slots.
		return ((hi < 0xe0 ? hi - 0x81 : hi - 0xc1) * 188)
			+ (lo < 0x7f ? lo - 0x40 : lo - 0x41);
	case Common::kWindows936:
	case Common::kWindows950:
		return (hi - 0x81) * 191 + lo - 0x40;
	case Common::kWindows949:
	default:
		return (hi - 0xb0) * 94 + lo - 0xa1;
	}
}

byte *ScummEngine::get2byteCharPtr(int idx) {
	if (!isScummvmKorTarget() && (_game.platform == Common::kPlatformFMTowns || _game.platform == Common::kPlatformPCEngine))
		return nullptr;

	// A fan translation supplies its own .fnt files, so the glyph order is
	// the code page's rather than whatever the original release used. Take
	// that route whenever the map named a bitmap font.
	if (!_cjkFontPattern.empty() || !_cjkFontSingle.empty() || _svfn.valid) {
		idx = get2byteCharIndex(idx);

		// The index is derived from bytes in the game's own data, so a
		// character outside the code page's assigned range would reach
		// past the loaded font. Bound it by the glyph count the map
		// declared for this file.
		// The extended format keeps its own geometry, and its glyphs are
		// not a plain 1bpp grid, so callers that want to blit raw bits
		// must not be handed one.
		if (_svfn.valid)
			return const_cast<byte *>(getSvfnGlyph(_svfn, idx));

		const int numChar = _cjkFontGlyphs > 0 ? _cjkFontGlyphs : 2350;
		if (idx < 0 || idx >= numChar)
			return nullptr;

		return _2byteFontPtr + ((_2byteWidth + 7) / 8) * _2byteHeight * idx;
	}

	switch (_language) {
	case Common::KO_KOR:
		idx = ((idx % 256) - 0xb0) * 94 + (idx / 256) - 0xa1;
		break;
	case Common::JA_JPN:
		if ((_game.id == GID_MONKEY && _game.platform == Common::kPlatformSegaCD) || _isIndy4Jap) {
			// init pointer to charset resource
			if (_2byteFontPtr[0] == 0xFF) {
				int charsetId = 5;
				int numChar = (getResourceSize(rtCharset, charsetId) - 14) / 32;
				assert(numChar <= kMaxRawJpCharNum);
				byte *charsetPtr = getResourceAddress(rtCharset, charsetId);
				if (charsetPtr == nullptr)
					error("ScummEngine::get2byteCharPtr: charset %d not found", charsetId);
				memcpy(_2byteFontPtr, charsetPtr + 14, _2byteWidth * _2byteHeight * numChar / 8);
			}

			idx = (SWAP_CONSTANT_16(idx) & 0x7fff);
		} else {
			idx = Graphics::FontTowns::getCharFMTChunk(idx);
		}

		break;
	case Common::ZH_TWN:
		{
			int base = 0;
			byte low = idx % 256;
			int high = 0;

			if (low >= 0x20 && low <= 0x7e) {
				base = (3 * low + 81012) * 5;
			} else {
				if (low >= 0xa1 && low <= 0xa3) {
					base = 392820;
					low += 0x5f;
				} else if (low >= 0xa4 && low <= 0xc6) {
					base = 0;
					low += 0x5c;
				} else if (low >= 0xc9 && low <= 0xf9) {
					base = 162030;
					low += 0x37;
				} else {
					base = 392820;
					low = 0xff;
				}

				if (low != 0xff) {
					high = idx / 256;
					if (high >= 0x40 && high <= 0x7e) {
						high -= 0x40;
					} else {
						high -= 0x62;
					}

					base += (low * 0x9d + high) * 30;
				}
			}

			return _2byteFontPtr + base;
		}
	case Common::ZH_CHN:
		idx = ((idx % 256) - 0xa1)* 94  + ((idx / 256) - 0xa1);
		break;
	default:
		idx = 0;
	}
	return	_2byteFontPtr + ((_2byteWidth + 7) / 8) * _2byteHeight * idx;
}


#pragma mark -


CharsetRenderer::CharsetRenderer(ScummEngine *vm) {
	_top = 0;
	_left = 0;
	_startLeft = 0;
	_right = 0;

	_color = 0;

	_center = false;
	_hasMask = false;
	_textScreenID = kMainVirtScreen;
	_blitAlso = false;
	_firstChar = false;
	_disableOffsX = false;

	_vm = vm;
	_curId = -1;
}

CharsetRenderer::~CharsetRenderer() {
}

CharsetRendererCommon::CharsetRendererCommon(ScummEngine *vm)
	: CharsetRenderer(vm), _fontPtr(nullptr), _bitsPerPixel(0), _fontHeight(0), _numChars(0), _shadowType(kNoShadowType) {
	_shadowColor = 0;
}

void CharsetRendererCommon::setCurID(int32 id) {
	if (id == -1)
		return;

	assertRange(0, id, _vm->_numCharsets - 1, "charset");

	_curId = id;

	_fontPtr = _vm->getResourceAddress(rtCharset, id);
	if (_fontPtr == nullptr)
		error("CharsetRendererCommon::setCurID: charset %d not found", id);

	if (_vm->_game.version == 4)
		_fontPtr += 17;
	else
		_fontPtr += 29;

	_bitsPerPixel = _fontPtr[0];
	_fontHeight = _fontPtr[1];
	_numChars = READ_LE_UINT16(_fontPtr + 2);

	if (_vm->_useMultiFont) {
		if (id == 6)    // HACK: Fix monkey1cd/monkey2/dott font error
			id = 0;

		if (_vm->_2byteMultiFontPtr[id]) {
			_vm->_2byteFontPtr = _vm->_2byteMultiFontPtr[id];
			_vm->_2byteWidth = _vm->_2byteMultiWidth[id];
			_vm->_2byteHeight = _vm->_2byteMultiHeight[id];
			_vm->_2byteShadow = _vm->_2byteMultiShadow[id];
		} else {
			// Get nearest font set (by height)
			debug(7, "Cannot find matching font set for charset #%d, use nearest font set", id);
			int dstHeight = _fontHeight;
			int nearest = 0;
			for (int i = 0; i < _vm->_numLoadedFont; i++) {
				if (ABS(_vm->_2byteMultiHeight[i] - dstHeight) <= ABS(_vm->_2byteMultiHeight[nearest] - dstHeight)) {
					nearest = i;
				}
			}
			debug(7, "Found #%d", nearest);
			_vm->_2byteFontPtr = _vm->_2byteMultiFontPtr[nearest];
			_vm->_2byteWidth = _vm->_2byteMultiWidth[nearest];
			_vm->_2byteHeight = _vm->_2byteMultiHeight[nearest];
			_vm->_2byteShadow = _vm->_2byteMultiShadow[nearest];
		}

		// The line box changed with the charset, so the TrueType instance
		// used for Korean hi-res text has to follow it.
		_vm->selectKorTtfFont(_vm->_2byteHeight * _vm->_koreanHiResScale);
	}
}

void CharsetRendererV3::setCurID(int32 id) {
	if (id == -1)
		return;

	assertRange(0, id, _vm->_numCharsets - 1, "charset");

	_curId = id;

	_fontPtr = _vm->getResourceAddress(rtCharset, id);
	if (_fontPtr == nullptr)
		error("CharsetRendererCommon::setCurID: charset %d not found", id);

	_bitsPerPixel = 1;
	_numChars = _fontPtr[4];
	_fontHeight = _fontPtr[5];

	_fontPtr += 6;
	_widthTable = _fontPtr;
	_fontPtr += _numChars;

	if (_vm->_useMultiFont) {
		if (_vm->_2byteMultiFontPtr[id]) {
			_vm->_2byteFontPtr = _vm->_2byteMultiFontPtr[id];
			_vm->_2byteWidth = _vm->_2byteMultiWidth[id];
			_vm->_2byteHeight = _vm->_2byteMultiHeight[id];
			_vm->_2byteShadow = _vm->_2byteMultiShadow[id];
		} else {
			// Get nearest font set (by height)
			debug(7, "Cannot find matching font set for charset #%d, use nearest font set", id);
			int dstHeight = _fontHeight;
			int nearest = 0;
			for (int i = 0; i < _vm->_numLoadedFont; i++) {
				if (ABS(_vm->_2byteMultiHeight[i] - dstHeight) <= ABS(_vm->_2byteMultiHeight[nearest] - dstHeight)) {
					nearest = i;
				}
			}
			debug(7, "Found #%d", nearest);
			_vm->_2byteFontPtr = _vm->_2byteMultiFontPtr[nearest];
			_vm->_2byteWidth = _vm->_2byteMultiWidth[nearest];
			_vm->_2byteHeight = _vm->_2byteMultiHeight[nearest];
			_vm->_2byteShadow = _vm->_2byteMultiShadow[nearest];
		}

		// The line box changed with the charset, so the TrueType instance
		// used for Korean hi-res text has to follow it.
		_vm->selectKorTtfFont(_vm->_2byteHeight * _vm->_koreanHiResScale);
	}
}

int CharsetRendererCommon::getFontHeight() const {
	bool isSegaCD = _vm->_game.platform == Common::kPlatformSegaCD;

	if (isSegaCD) {
		return _vm->_force2ByteCharHeight ? _vm->_2byteHeight : _fontHeight;
	} else if (_vm->_isIndy4Jap) {
		return _vm->_force2ByteCharHeight ? 14 : _fontHeight;
	} else if (_vm->_useCJKMode && !isSegaCD) {
		return MAX(_vm->_2byteHeight + 1, _fontHeight);
	} else {
		return _fontHeight;
	}
}

// do spacing for variable width old-style font
int CharsetRendererClassic::getCharWidth(uint16 chr) const {
	int spacing = 0;

	// With TTF metrics enabled the advance comes from the font itself, so
	// the spacing follows the glyphs instead of the original bitmap grid.
	// This reflows the text: line breaks computed by the game no longer
	// match, which is the trade-off for a consistent typeface.
	const int ttfWidth = _vm->getKorTtfCharWidth(chr);
	if (ttfWidth >= 0)
		return ttfWidth;

	if (_vm->_useCJKMode && chr >= 0x80)
		return _vm->_2byteWidth / 2;

	int offs = READ_LE_UINT32(_fontPtr + chr * 4 + 4);
	if (offs)
		spacing = _fontPtr[offs] + (signed char)_fontPtr[offs + 2];

	return spacing;
}

/**
 * Advance width for one character, taken from the TrueType font.
 *
 * Returns -1 when the caller should fall back to the game's own metrics,
 * which is the default: replacing the advance reflows every line, so the
 * word wrapping the game computed for its bitmap font no longer holds.
 * Enabled with "metrics=ttf" in the [latin] section of the font map.
 */
int ScummEngine::getKorTtfCharWidth(uint16 chr) {
	// A bitmap font answers first when it was told to: it is the one
	// actually drawing the glyph, so its advance is what keeps the line
	// evenly spaced.
	{
		const int w = getSvfnWidth(chr);
		if (w >= 0)
			return w;
	}

#ifdef USE_FREETYPE2
	if (!_korTtfMetrics || !_korTtfEnabled || !isKoreanHiRes())
		return -1;

	// v0-v2 lay their text out on a fixed cell and the scripts depend on it.
	// Those games are meant to be used with a pixel font that matches the
	// cell, so keep the grid and only take the glyphs from the font.
	if (_game.version <= 2)
		return -1;

	// Hangul keeps driving the font size; see drawKorTtfChar().
	if (chr >= 256)
		selectKorTtfFont(_2byteHeight * _koreanHiResScale);

	if (!_korTtfFont)
		return -1;

	const uint16 unicode = ttfCharToUnicode(chr);
	if (!unicode)
		return -1;

	// The renderer works in scaled coordinates, the layout in game ones.
	// getCharWidth() is in scaled pixels; the layout works in game pixels.
	// Round to nearest instead of truncating, otherwise the accumulated
	// error pulls the glyphs apart over a line.
	const int w = _korTtfFont->getCharWidth(unicode) / _korTtfSupersample;
	return MAX(1, (w + _koreanHiResScale / 2) / _koreanHiResScale);
#else
	return -1;
#endif
}

// CharsetRenderer hook: draw a Korean glyph with the engine's TrueType font
// directly into the scaled hi-res text surface. Returns false when the TTF
// path doesn't apply, so the caller falls back to the bitmap renderer.
bool CharsetRendererCommon::drawHiResKorChar(Graphics::Surface &s, int x, int y, int drawTop, uint16 chr) {
	if (!_vm->isKoreanHiRes() || !_vm->_useCJKMode)
		return false;

	// Single byte characters normally keep the game's own bitmap font. With
	// [latin] enabled they go through the TrueType renderer too, so a mixed
	// line does not show two different typefaces.
	if (chr < 256 && !_vm->_korTtfLatin)
		return false;

	// A font in the extended bitmap format was baked for this resolution
	// already, coverage and all, so it is drawn straight rather than going
	// through the scaling blitter. This is the path that gives anti-aliased
	// text in a build with no FreeType at all.
	if (chr >= 256 && _vm->_svfn.valid) {
		const int idx = _vm->get2byteCharIndex(chr);
		if (_vm->drawSvfnGlyph(s, _vm->_svfn, idx, x, y, _color, _shadowColor))
			return true;
	}

	// Latin letters have a font of their own, indexed by character code.
	if (chr < 256 && _vm->_svfnLatin.valid) {
		if (_vm->drawSvfnGlyph(s, _vm->_svfnLatin, chr, x, y, _color, _shadowColor))
			return true;
	}

	if (!_vm->_korTtfFont)
		return false;

	return _vm->drawKorTtfChar(s, chr, x, y, _color, _shadowColor);
}

int CharsetRenderer::getStringWidth(int arg, const byte *text) {
	int pos = 0;
	bool isV3Towns = _vm->_game.version == 3 && _vm->_game.platform == Common::kPlatformFMTowns;

	// I have confirmed from disasm that neither LOOM EGA and FM-TOWNS (EN/JP) nor any other games within the
	// v0-v3 version range add 1 to the width. There isn't even a getStringWidth method. And the v0-2 games don't
	// even support text rendering over strip borders. However, LOOM VGA Talkie and MONKEY1 EGA do have the
	// getStringWidth method and they do add 1 to the width. So that seems to have been introduced with version 4.
	int width = (_vm->_game.version < 4 || _vm->_game.id == GID_FT) ? 0 : 1;

	int chr;
	int oldID = getCurID();
	int code = (_vm->_game.heversion >= 80) ? 127 : 64;

	while ((chr = text[pos++]) != 0) {
		// Given that the loop increments pos two times per loop in Towns games,
		// we risk missing the termination character. Let's catch it and break the loop.
		// This happens at least for the restart prompt message on INDY3 Towns JAP.
		if (isV3Towns && pos > 1 && text[pos - 2] == 0)
			break;

		if (chr == '\n' || chr == '\r' || chr == _vm->_newLineCharacter)
			break;

		if (_vm->_game.heversion >= 72) {
			if (chr == code) {
				chr = text[pos++];
				if (chr == 84 || chr == 116) {  // Strings of speech offset/size
					while (chr != code)
						chr = text[pos++];
					continue;
				}
				if (chr == 119) // 'Wait'
					break;
				if (chr == 104|| chr == 110) // 'Newline'
					break;
			}
		} else {
			if (chr == '@')
				continue;
			if (chr == 255 || (_vm->_game.version <= 6 && chr == 254)) {
				chr = text[pos++];
				if (chr == 3)	// 'WAIT'
					break;
				if (chr == 8) { // 'Verb on next line'
					if (arg == 1)
						break;
					while (text[pos++] == ' ') {}
					continue;
				}
				if (chr == 10 || chr == 21 || chr == 12 || chr == 13) {
					pos += 2;
					continue;
				}
				if (chr == 9 || chr == 1 || chr == 2) // 'Newline'
					break;
				if (chr == 14) {
					int set = text[pos] | (text[pos + 1] << 8);
					pos += 2;
					setCurID(set);
					continue;
				}
			}
		}

		if (_vm->_useCJKMode) {
			if (_vm->_language == Common::JA_JPN && _vm->_game.platform == Common::kPlatformFMTowns) {
				if (checkSJISCode(chr))
					// This strange character conversion is the exact way the original does it here.
					// This is the only way to get an accurate text formatting in the MI1 intro.
					chr = (int8)text[pos++] | (chr << 8);
			} else if (_vm->_isIndy4Jap) {
				if (checkSJISCode(chr)) {
					chr = text[pos++] | (chr << 8);
					if (chr >= 256) {
						width += 15;
						continue;
					}
				}
			} else if (chr & 0x80) {
				if (_vm->_game.platform == Common::kPlatformSegaCD) {
					// Special character: this one has to be rendered as a space (0x20)
					// and also inherits its rendering dimensions.
					if (chr == 0xFD && text[pos] == 0xFA) {
						width += getCharWidth(0x20);
					} else {
						width += _vm->_2byteWidth;
					}
				} else {
					// With TTF metrics the string width has to agree with
					// what printChar() will actually advance, otherwise the
					// game wraps a line at the wrong place and the last
					// glyph spills onto the next one.
					const uint16 pair = (uint16)((text[pos] << 8) | chr);
					const int ttfWidth = _vm->getKorTtfCharWidth(pair);
					if (ttfWidth >= 0) {
						width += ttfWidth;
					} else {
						width += _vm->_2byteWidth;
						// Original keeps glyph width and character dimensions separately
						if (_vm->_language == Common::KO_KOR || _vm->_language == Common::ZH_TWN) {
							width++;
						}
					}
				}

				pos++;
				continue;
			}
		}
		width += getCharWidth(chr);
	}

	setCurID(oldID);

	return width;
}

void CharsetRenderer::addLinebreaks(int a, byte *str, int pos, int maxwidth) {
	int lastKoreanLineBreak = -1;
	int origPos = pos;
	int lastspace = -1;
	int curw = 1;
	int chr;
	int oldID = getCurID();
	int code = (_vm->_game.heversion >= 80) ? 127 : 64;

	int strLength = _vm->resStrLen(str);

	while ((chr = str[pos++]) != 0) {
		if (_vm->_game.heversion >= 72) {
			if (chr == code) {
				chr = str[pos++];
				if (chr == 84 || chr == 116) {  // Strings of speech offset/size
					while (chr != code)
						chr = str[pos++];
					continue;
				}
				if (chr == 119) // 'Wait'
					break;
				if (chr == 110) { // 'Newline'
					curw = 1;
					continue;
				}
				if (chr == 104) // 'Don't terminate with \n'
					break;
			}
		} else {
			if (chr == '@')
				continue;
			if (chr == 255 || (_vm->_game.version <= 6 && chr == 254)) {
				chr = str[pos++];
				if (chr == 3) // 'Wait'
					break;
				if (chr == 8) { // 'Verb on next line'
					if (a == 1) {
						curw = 1;
					} else {
						while (str[pos] == ' ')
							str[pos++] = '@';
					}
					continue;
				}
				if (chr == 10 || chr == 21 || chr == 12 || chr == 13) {
					pos += 2;
					continue;
				}
				if (chr == 1) { // 'Newline'
					curw = 1;
					continue;
				}
				if (chr == 2) // 'Don't terminate with \n'
					break;
				if (chr == 14) {
					int set = str[pos] | (str[pos + 1] << 8);
					pos += 2;
					setCurID(set);
					continue;
				}
			}
		}
		if (chr == ' ')
			lastspace = pos - 1;

		if (chr == _vm->_newLineCharacter)
			lastspace = pos - 1;

		if (_vm->_useCJKMode) {
			if (_vm->_language == Common::JA_JPN && _vm->_game.platform == Common::kPlatformFMTowns) {
				if (checkSJISCode(chr))
					// This strange character conversion is the exact way the original does it here.
					// This is the only way to get an accurate text formatting in the MI1 intro.
					chr = (int8)str[pos++] | (chr << 8);
				curw += getCharWidth(chr);
			} else if (chr & 0x80) {
				// The wrapper walks the bytes one at a time; rebuild the
				// pair the way printChar() sees it so the TTF advance
				// matches what will be drawn.
				const uint16 pair = (uint16)((str[pos] << 8) | chr);
				pos++;

				const int ttfWidth = _vm->getKorTtfCharWidth(pair);
				if (ttfWidth >= 0) {
					curw += ttfWidth;
				} else {
					curw += _vm->_2byteWidth;
					// Original keeps glyph width and character dimensions separately
					if (_vm->_language == Common::KO_KOR || _vm->_language == Common::ZH_TWN) {
						curw++;
					}
				}
			} else if (chr != _vm->_newLineCharacter) {
				curw += getCharWidth(chr);
			}

			if (_vm->isScummvmKorTarget() && !_center) {
				// Break Korean words at any character
				// Used in Korean fan translated games
				if (chr & 0x80) {
					if (checkKSCode(chr, str[pos - 1])
					    && !(pos - 4 >= origPos && str[pos - 3] == '`' && str[pos - 4] == ' ')  // prevents hanging quotation mark at the end of line
					    && !(pos - 4 >= origPos && str[pos - 3] == '\'' && str[pos - 4] == ' ') // prevents hanging single quotation mark at the end of line
					    && !(pos - 3 >= origPos && str[pos - 3] == '('))  // prevents hanging parenthesis at the end of line
						lastKoreanLineBreak = pos - 2;
				} else {
					if (chr == '(' && pos - 3 >= origPos && checkKSCode(str[pos - 3], str[pos - 2]))
						lastKoreanLineBreak = pos - 1;
				}
			}
		} else {
			curw += getCharWidth(chr);
		}
		if (lastspace == -1) {
			if (!_vm->isScummvmKorTarget() || lastKoreanLineBreak == -1) {
				continue;
			}
		}
		if (curw > maxwidth) {
			if (!_vm->isScummvmKorTarget()) {
				str[lastspace] = 0xD;
				curw = 1;
				pos = lastspace + 1;
				lastspace = -1;
			} else {
				// Handle Korean line break mode (break Korean words at any character)
				// Used in Korean fan translated games
				if (lastspace >= lastKoreanLineBreak) {
					str[lastspace] = 0xD;
					curw = 1;
					pos = lastspace + 1;
					lastspace = -1;
					lastKoreanLineBreak = -1;
				} else {
					byte *breakPtr = str + lastKoreanLineBreak;
					memmove(breakPtr + 1, breakPtr, strLength - lastKoreanLineBreak + 1);
					str[lastKoreanLineBreak] = 0xD;
					curw = 1;
					pos = lastKoreanLineBreak + 1;
					lastspace = -1;
					lastKoreanLineBreak = -1;
				}
			}
		}
	}

	setCurID(oldID);
}

int CharsetRendererV3::getCharWidth(uint16 chr) const {
	int spacing = 0;

	const int ttfWidth = _vm->getKorTtfCharWidth(chr);
	if (ttfWidth >= 0)
		return ttfWidth;

	if (_vm->_useCJKMode && (chr & 0x80))
		spacing = _vm->_2byteWidth / 2;

	if (!spacing)
		spacing = *(_widthTable + chr);

	return spacing;
}

void CharsetRendererPC::setShadowMode(ShadowType mode) {
	_shadowColor = 0;
	_shadowType = mode;
}

void CharsetRendererPC::drawBits1(Graphics::Surface &dest, int x, int y, const byte *src, int drawTop, int width, int height) {
	if (_vm->_useCJKMode && _vm->isScummvmKorTarget()) {
		drawBits1Kor(dest, x, y, src, drawTop, width, height);
		return;
	}

	if (_shadowType == kOutlineShadowType) {
		x++;
		y++;
	}

	byte *dst = (byte *)dest.getBasePtr(x, y);
	byte bits = 0;
	uint8 col = _color;
	int pitch = dest.pitch - width * dest.format.bytesPerPixel;
	byte *dst2 = dst + dest.pitch;
	byte *dst3 = dst - 1;
	byte *dst4 = dst - dest.pitch;
	byte prevBits = 0;
	bool leftShadePixel = false;
	int savedX = x;

	for (y = 0; y < height && y + drawTop < dest.h; y++) {
		for (x = 0; x < width; x++) {
			if ((x % 8) == 0) {
				prevBits = ~bits;
				bits = *src++;
				leftShadePixel = true;
			}
			if ((bits & revBitMask(x % 8)) && y + drawTop >= 0) {
				if (_shadowType == kNormalShadowType) {
					dst[1] = dst2[1] = _shadowColor;

					// Mac and DOS/V versions of Japanese INDY4 don't
					// draw a shadow pixel below the first pixel.
					// Verified from disasm.
					if (!_vm->_isIndy4Jap)
						dst2[0] = _shadowColor;
				} else if (_shadowType == kHorizontalShadowType) {
					dst[1] = _shadowColor;
				} else if (_shadowType == kOutlineShadowType) {
					dst[1] = dst2[0] = dst2[1] = _shadowColor;
					if (leftShadePixel) {
						dst3[0] = _shadowColor;
						leftShadePixel = false;
					}
					if (prevBits & revBitMask(x % 8))
						dst4[0] = _shadowColor;
				}

				// Since C64 texts are moved one pixel forward in the X axis, let's avoid
				// any out-of-line pixel drawing...
				if (_vm->_game.platform != Common::kPlatformC64 || (savedX + x < dest.pitch)) {
					dst[0] = col;
				}

			} else if (!(bits & revBitMask(x % 8))) {
				leftShadePixel = true;
				if (y < height - 1 && _vm->_useCJKMode && _vm->_game.platform == Common::kPlatformSegaCD)
					dst[0] = 0;
			}

			dst += dest.format.bytesPerPixel;
			dst2 += dest.format.bytesPerPixel;
			dst3 += dest.format.bytesPerPixel;
			dst4 += dest.format.bytesPerPixel;
		}

		dst += pitch;
		dst2 += pitch;
		dst3 += pitch;
		dst4 += pitch;
	}
}

void CharsetRendererPC::drawBits1Kor(Graphics::Surface &dest, int x1, int y1, const byte *src, int drawTop, int width, int height) {
	byte *dst = (byte *)dest.getBasePtr(x1, y1);

	int y, x;
	byte bits = 0;

	// HACK: Since Korean fonts don't have shadow/stroke information,
	//	   we use NUT-Renderer-like shadow drawing method.

	int offsetX[14] = {-2, -2, -2, -1, 0, -1, 0, 1, -1, 1, -1, 0, 1, 0};
	int offsetY[14] = {0, 1, 2, 2, 2, -1, -1, -1, 0, 0, 1, 1, 1, 0};
	int cTable[14] = {_shadowColor, _shadowColor, _shadowColor,
						_shadowColor, _shadowColor, _shadowColor, _shadowColor,
						_shadowColor, _shadowColor, _shadowColor, _shadowColor,
						_shadowColor, _shadowColor, _color};
	int i = 0;

	// The map can force a different outline for every font, including this
	// one: a translation that replaced the .fnt has no reason to keep the
	// decoration the original was drawn with.
	switch (_vm->hiResShadowMode()) {
	case ScummEngine::kHiResShadowNone:
		i = 13;
		break;
	case ScummEngine::kHiResShadowDrop:
		i = 12;
		break;
	case ScummEngine::kHiResShadowStroke:
		i = 0;
		break;
	default: // outline
		i = 5;
	}

	if (_vm->_hiResShadowColorSet) {
		for (int c = 0; c < 13; ++c)
			cTable[c] = _vm->_hiResShadowColor;
	}

	const byte *origSrc = src;
	byte *origDst = dst;

	// In Korean hi-res mode the destination surface is scaled up, so every
	// glyph pixel becomes an m x m block and the shadow/stroke offsets have
	// to be scaled along with it. m == 1 leaves the original behaviour.
	// NB: callers pass a by-value copy of _textSurface, so identify it by the
	// pixel buffer rather than by object address.
	const int m = (dest.getPixels() == _vm->_textSurface.getPixels()) ? _vm->_textSurfaceMultiplier : 1;

	for (; i < 14; i++) {
		src = origSrc;
		dst = origDst;

		const int offX = offsetX[i] * m;
		const int offY = offsetY[i] * m;

		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				if ((x % 8) == 0)
					bits = *src++;

				if (bits & revBitMask(x % 8)) {
					// Where this glyph pixel's m x m block lands. The
					// whole block has to be inside the surface, not just
					// its top left corner, or the last row and column of
					// a glyph at the edge write past the end.
					const int bx = (x * m) + x1 + offX;
					const int by = (y * m) + y1 + offY;

					if (bx >= 0 && by >= 0 && bx + m <= dest.w && by + m <= dest.h) {
						byte *p = dst + (dest.pitch * offY) + offX;
						for (int sy = 0; sy < m; ++sy) {
							for (int sx = 0; sx < m; ++sx)
								p[sy * dest.pitch + sx] = cTable[i];
						}
					}
				}
				dst += m;
			}

			dst += dest.pitch * m - width * m;
		}
	}
}

int CharsetRendererV3::getDrawWidthIntern(uint16 chr) {
	return getCharWidth(chr);
}

int CharsetRendererV3::getDrawHeightIntern(uint16) {
	return 8;
}

void CharsetRendererV3::setColor(byte color, bool shadowModeSpecialFlag) {
	ShadowType mode = kNoShadowType;
	_color = color;

	if (_vm->_game.features & GF_OLD256) {
		if (_color & 0x80)
			mode = kNormalShadowType;
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
		if (_vm->_game.platform == Common::kPlatformFMTowns) {
			_color = (_color & 0x0f) | ((_color & 0x0f) << 4);
			if (_color == 0)
				_color = 0x88;
		} else
#endif
		_color = (_vm->_game.id == GID_LOOM) ? _color & 0x0f : _color & 0x7f;
	} else if (_vm->_game.id == GID_LOOM && _vm->_game.version == 3) {
		mode = (_color & 0x40) ? (shadowModeSpecialFlag ? kNoShadowType : kOutlineShadowType) : ((_color & 0x80) ? kNormalShadowType : kNoShadowType);
		_color &= 0x0f;
	} else if (_vm->_game.version >= 2 && (_vm->_game.features & GF_16COLOR)) {
		mode = (_color & 0x80) ? kNormalShadowType : kNoShadowType;
		_color &= 0x0f;
	}

	setShadowMode(mode);

	translateColor();
}

#ifdef USE_RGB_COLOR
void CharsetRendererPCE::setColor(byte color, bool) {
	_vm->setPCETextPalette(color);
	_color = 15;

	setShadowMode(kNormalShadowType);
}
#endif

void CharsetRendererV3::printChar(int chr, bool ignoreCharsetMask) {

	// WORKAROUND for bug #2703: Indy3 Mac does not show black
	// characters (such as in the grail diary) if ignoreCharsetMask
	// is true. See also bug #8759.
	if (_vm->_game.id == GID_INDY3 && _vm->_game.platform == Common::kPlatformMacintosh && _color == 0)
		ignoreCharsetMask = false;

	// Indy3 / Zak256 / Loom
	int width, height, origWidth = 0, origHeight;
	VirtScreen *vs;
	const byte *charPtr;
	int is2byte = (chr >= 256 && _vm->_useCJKMode) ? 1 : 0;

	assertRange(0, _curId, _vm->_numCharsets - 1, "charset");

	if ((vs = _vm->findVirtScreen(_top)) == nullptr) {
		warning("findVirtScreen(%d) failed, therefore printChar cannot print '\\x%X'", _top, chr);
		return;
	}

	if (chr == '@')
		return;

	if (_vm->isScummvmKorTarget()) {
		// Keep the code point even for single byte characters: with [latin]
		// enabled they are rendered with the TrueType font as well.
		_curKorChar = (is2byte || _vm->_korTtfLatin) ? static_cast<uint16>(chr) : 0;
		if (is2byte) {
			charPtr = _vm->get2byteCharPtr(chr);
			width = _vm->_2byteWidth;
			height = _vm->_2byteHeight;

			// With TTF metrics the advance follows the font, not the
			// fixed double byte cell.
			const int ttfWidth = _vm->getKorTtfCharWidth(chr);
			if (ttfWidth >= 0)
				width = ttfWidth;
		} else {
			charPtr = _fontPtr + chr * 8;
			width = getDrawWidthIntern(chr);
			height = getDrawHeightIntern(chr);
		}
	} else {
		charPtr = (_vm->_useCJKMode && chr > 127) ? _vm->get2byteCharPtr(chr) : _fontPtr + chr * 8;
		width = getDrawWidthIntern(chr);
		height = getDrawHeightIntern(chr);
	}
	setDrawCharIntern(chr);

	origWidth = width;
	origHeight = height;

	// Clip at the right side (to avoid drawing "outside" the screen bounds).
	if (_left + origWidth > _right + 1)
		return;

	if (_shadowType == kNormalShadowType) {
		width++;
		height++;
	} else if (_shadowType == kOutlineShadowType) {
		width += 2;
		height += 2;
	}

	if (_firstChar) {
		_str.left = _left;
		_str.top = _top;
		_str.right = _left;
		_str.bottom = _top;
		_firstChar = false;
	}

	int drawTop = _top - vs->topline;

	int dirtyTop = drawTop;
	int dirtyHeight = height;

	// See the note in CharsetRendererClassic::printChar(): TTF glyphs need
	// room above and below the game's cell.
	if (_vm->isKoreanHiRes() && _vm->hasHiResFont()) {
		const int slack = _vm->_2byteHeight;
		dirtyTop = MAX(0, dirtyTop - slack);
		dirtyHeight += slack * 2;
	}

	_vm->markRectAsDirty(vs->number, _left, _left + width, dirtyTop, dirtyTop + dirtyHeight);

	if (!ignoreCharsetMask) {
		_hasMask = true;
		_textScreenID = vs->number;
	}

	// Korean hi-res text always goes to the scaled text surface, even for
	// the virtual screens that normally receive text directly (the verb
	// area): that is the only buffer with the resolution to hold it.
	const bool korTtfTarget = _vm->isKoreanHiRes() && _vm->hasHiResFont()
			&& (is2byte || _vm->_korTtfLatin);

	if ((ignoreCharsetMask || !vs->hasTwoBuffers) && !korTtfTarget
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
		&& (_vm->_game.platform != Common::kPlatformFMTowns)
#endif
		) {
		drawBits1(*vs, _left + vs->xstart, drawTop, charPtr, drawTop, origWidth, origHeight);
	}
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
	else if (_vm->_game.platform == Common::kPlatformFMTowns && vs->number == kBannerVirtScreen)
		drawBits1(*vs, _left * _vm->_textSurfaceMultiplier, drawTop * _vm->_textSurfaceMultiplier, charPtr, drawTop, origWidth, origHeight);
#endif
	else {
		if (!drawHiResKorChar(_vm->_textSurface,
				_left * _vm->_textSurfaceMultiplier,
				(_top - _vm->_screenTop) * _vm->_textSurfaceMultiplier, drawTop, static_cast<uint16>(chr)))
			drawBits1(_vm->_textSurface, _left * _vm->_textSurfaceMultiplier, _top * _vm->_textSurfaceMultiplier, charPtr, drawTop, origWidth, origHeight);
	}

	// The double byte advance is expressed in the scaled coordinates the
	// original CJK modes set up, so it gets divided back down here. Korean
	// hi-res mode drives the multiplier itself and keeps _2byteWidth in game
	// pixels, so dividing would shrink the advance to a fraction of the cell.
	if (is2byte && !_vm->isKoreanHiRes()) {
		origWidth /= _vm->_textSurfaceMultiplier;
		height /= _vm->_textSurfaceMultiplier;
	}

	if (_str.left > _left)
		_str.left = _left;

	_left += origWidth;

	if (_str.right < _left) {
		_str.right = _left;
		if (_shadowType != kNoShadowType)
			_str.right++;
	}

	if (_str.bottom < _top + height)
		_str.bottom = _top + height;
}

void CharsetRendererV3::drawChar(int chr, Graphics::Surface &s, int x, int y) {
	const byte *charPtr;
	int width;
	int height;
	int is2byte = (chr > 0xff && _vm->_useCJKMode) ? 1 : 0;

	if (_vm->isScummvmKorTarget()) {
		if (is2byte) {
			charPtr = _vm->get2byteCharPtr(chr);
			width = _vm->_2byteWidth;
			height = _vm->_2byteHeight;

			// With TTF metrics the advance follows the font, not the
			// fixed double byte cell.
			const int ttfWidth = _vm->getKorTtfCharWidth(chr);
			if (ttfWidth >= 0)
				width = ttfWidth;
		} else {
			charPtr = _fontPtr + chr * 8;
			width = getDrawWidthIntern(chr);
			height = getDrawHeightIntern(chr);
		}
	} else {
		charPtr = (_vm->_useCJKMode && chr > 127) ? _vm->get2byteCharPtr(chr) : _fontPtr + chr * 8;
		width = getDrawWidthIntern(chr);
		height = getDrawHeightIntern(chr);
	}
	setDrawCharIntern(chr);
	if ((is2byte || _vm->_korTtfLatin) && drawHiResKorChar(s, x, y, y, static_cast<uint16>(chr)))
		return;
	drawBits1(s, x, y, charPtr, y, width, height);
}

void CharsetRenderer::translateColor() {
	// Don't do anything for v1 and v2 CGA and Hercules modes
	// here (and v0 doesn't have any of these modes).
	if (_vm->_game.version < 3)
		return;

	// Based on disassembly
	if (_vm->_renderMode == Common::kRenderCGA) {
		static const byte CGAtextColorMap[16] = {0,  3, 3, 3, 5, 5, 5,  15,
										   15, 3, 3, 3, 5, 5, 15, 15};
		_color = CGAtextColorMap[_color & 0x0f];
	}

	if (_vm->_renderMode == Common::kRenderHercA || _vm->_renderMode == Common::kRenderHercG) {
		static const byte HercTextColorMap[16] = {0, 15,  2, 15, 15,  5, 15,  15,
										   8, 15, 15, 15, 15, 15, 15, 15};
		_color = HercTextColorMap[_color & 0x0f];
	}
}

void CharsetRenderer::saveLoadWithSerializer(Common::Serializer &ser) {
	ser.syncAsByte(_curId, VER(73), VER(73));
	ser.syncAsSint32LE(_curId, VER(74));
	ser.syncAsByte(_color, VER(73));

	if (ser.isLoading()) {
		// Some old v0.13.x saves have bogus values, for some reason (see
		// bug #15931). When detecting such weird values made before the
		// v1.0.0 release (VER(80)) that followed it, reinitialize the id
		// using a, hopefully, sane value.
		if (ser.getVersion() < VER(80) && _curId > _vm->_numCharsets - 1)
			_curId = _vm->_string[0]._default.charset;

		setCurID(_curId);
		setColor(_color);
	}
}

void CharsetRendererClassic::printChar(int chr, bool ignoreCharsetMask) {
	VirtScreen *vs;
	bool is2byte = (chr >= 256 && _vm->_useCJKMode);

	if (_vm->_game.platform == Common::kPlatformSegaCD && chr == 0xFAFD) {
		is2byte = false;
		chr = 0x20;
	}

	assertRange(1, _curId, _vm->_numCharsets - 1, "charset");

	if ((vs = _vm->findVirtScreen(_top)) == nullptr && (vs = _vm->findVirtScreen(_top + getFontHeight())) == nullptr)
		return;

	if (chr == '@')
		return;

	// This is an actual check from disasm:
	// it appears that a certain Japanese glyph was previously being drawn with the '_' character.
	// The executable now disables any attempt to draw this character. Removing this check draws
	// an additional '_' character where it should be drawn.
	// This, of course, disables the text cursor when writing a savegame name, but that's in the
	// original as well.
	if ((_vm->_isIndy4Jap || (_vm->_game.platform == Common::kPlatformSegaCD && _vm->_language == Common::JA_JPN)) &&
		chr == '_')
		return;

	translateColor();

	_vm->_charsetColorMap[1] = _color;
	_curKorChar = (_vm->isScummvmKorTarget() && (is2byte || _vm->_korTtfLatin)) ? (uint16)chr : 0;
	if (_vm->isScummvmKorTarget() && is2byte) {
		setShadowMode(kNormalShadowType);
		_charPtr = _vm->get2byteCharPtr(chr);
		_width = _vm->_2byteWidth;
		_height = _vm->_2byteHeight;
		_offsX = _offsY = 0;

		// With TTF metrics the advance follows the font, not the fixed
		// double byte cell.
		const int ttfWidth = _vm->getKorTtfCharWidth(chr);
		if (ttfWidth >= 0)
			_width = ttfWidth;
	} else {
		if (!prepareDraw(chr))
			return;
	}

	if (_vm->isScummvmKorTarget()) {
		_origWidth = _width;
		_origHeight = _height;
	}

	if (_firstChar) {
		_str.left = 0;
		_str.top = 0;
		_str.right = 0;
		_str.bottom = 0;
	}

	_top += _offsY;
	_left += _offsX;

	if (_left + _origWidth > _right + 1 || _left < 0) {
		_left += _origWidth;
		_top -= _offsY;
		return;
	}

	_disableOffsX = false;

	if (_firstChar) {
		_str.left = _left;
		_str.top = _top;
		_str.right = _left;
		_str.bottom = _top;
		_firstChar = false;
	}

	if (_left < _str.left)
		_str.left = _left;

	if (_top < _str.top)
		_str.top = _top;

	int drawTop = _top - vs->topline;

	// Clip the dialog choices to a rectangle starting 35 pixels from the left
	// for Japanese Monkey Island 1 SegaCD. _scummVars[451] is set by script 187,
	// responsible for handling the dialog horizontal scrolling.
	bool isSegaCDDialogChoice = _vm->_game.platform == Common::kPlatformSegaCD &&
		_vm->_language == Common::JA_JPN && vs->number == kVerbVirtScreen && _vm->_scummVars[451] == 1;
	if (isSegaCDDialogChoice && _left < 35) {
		_left += _origWidth;
		return;
	} else {
		int dirtyTop = drawTop;
		int dirtyHeight = _height;

		// TrueType glyphs are not confined to the game's cell the way the
		// original bitmaps are: ascenders reach above it, commas and
		// parentheses hang below. Only what falls inside the dirty
		// rectangle gets composited, so give the line a cell of slack on
		// each side rather than clipping the font to the old grid.
		if (_vm->isKoreanHiRes() && _vm->hasHiResFont()) {
			const int slack = _vm->_2byteHeight;
			dirtyTop = MAX(0, dirtyTop - slack);
			dirtyHeight += slack * 2;
		}

		_vm->markRectAsDirty(vs->number, _left, _left + _width, dirtyTop, dirtyTop + dirtyHeight);
	}

	// This check for kPlatformFMTowns and kMainVirtScreen is at least required for the chat with
	// the navigator's head in front of the ghost ship in Monkey Island 1
	if (!ignoreCharsetMask || (_vm->_game.platform == Common::kPlatformFMTowns && vs->number == kMainVirtScreen)) {
		_hasMask = true;
		_textScreenID = vs->number;
	}

	// We need to know the virtual screen we draw on for Indy 4 Amiga, since
	// it selects the palette map according to this. We furthermore can not
	// use _textScreenID here, since that will cause inventory graphics
	// glitches.
	if (_vm->_game.platform == Common::kPlatformAmiga && _vm->_game.id == GID_INDY4)
		_drawScreen = vs->number;

	// The following kerning corrections are taken from disasm. Originally they were
	// used as character widths (e.g. 13, 15, 16), but we adapt them to small negative
	// numbers using the formula:_cjkSpacing = japWidthCorrection - 16; where 16 is the
	// full width of a Japanese character in this version.
	if (_vm->_isIndy4Jap) {
		int japWidthCorrection = (_top == 161) ? 13 : 14;
		int japHeightCorrection = 15;
		if (_vm->findVirtScreen(_top)->number == kMainVirtScreen && !_vm->isMessageBannerActive()) {
			japWidthCorrection = 15;
			japHeightCorrection = 16;
		} else if (_vm->isMessageBannerActive()) {
			japWidthCorrection = 13;
		}

		if (is2byte)
			_height = _origHeight = japHeightCorrection;
		_cjkSpacing = japWidthCorrection - 16;
	}

	printCharIntern(is2byte, _charPtr, _origWidth, _origHeight, _width, _height, vs, ignoreCharsetMask);

	// Original keeps glyph width and character dimensions separately
	if ((_vm->_language == Common::ZH_TWN || _vm->_language == Common::KO_KOR) && is2byte)
		_origWidth++;

	_left += _origWidth;
	if (is2byte)
		_left += _cjkSpacing;

	if (_str.right < _left) {
		_str.right = _left;
		if (_vm->_game.platform != Common::kPlatformFMTowns && _shadowType != kNoShadowType)
			_str.right++;
	}

	if (_str.bottom < _top + _origHeight)
		_str.bottom = _top + _origHeight;

	_top -= _offsY;
}

void CharsetRendererClassic::printCharIntern(bool is2byte, const byte *charPtr, int origWidth, int origHeight, int width, int height, VirtScreen *vs, bool ignoreCharsetMask) {
	byte *dstPtr = nullptr;
	byte *back = nullptr;
	int drawTop = _top - vs->topline;

	if ((_vm->_game.heversion >= 71 && _bitsPerPixel >= 8) || (_vm->_game.heversion >= 90 && _bitsPerPixel == 0)) {
#ifdef ENABLE_HE
		if (ignoreCharsetMask || !vs->hasTwoBuffers) {
			dstPtr = vs->getPixels(0, 0);
		} else {
			dstPtr = (byte *)_vm->_textSurface.getPixels();
		}

		if (_blitAlso && vs->hasTwoBuffers) {
			dstPtr = vs->getBackPixels(0, 0);
		}

		byte *colorLookupTable;
		byte generalColorLookupTable[256];
		int black = _vm->VAR_COLOR_BLACK != 0xFF ? _vm->VAR(_vm->VAR_COLOR_BLACK) : 0;

		memset(generalColorLookupTable, black, sizeof(generalColorLookupTable));
		for (int i = 0; i < 4; i++) {
			generalColorLookupTable[i] = _vm->_charsetColorMap[i];
		}
		generalColorLookupTable[1] = _color;

		if (_bitsPerPixel || _vm->_game.heversion < 90) {
			colorLookupTable = generalColorLookupTable;
		} else {
			colorLookupTable = nullptr;
		}

		if (ignoreCharsetMask && vs->hasTwoBuffers) {
			((ScummEngine_v71he *)_vm)->_wiz->pgDrawWarpDrawLetter(
				(WizRawPixel *)dstPtr,
				vs->w, vs->h,
				charPtr, _left, drawTop, origWidth, origHeight,
				colorLookupTable);

			Common::Rect blitRect(_left, drawTop, _left + origWidth - 1, drawTop + origHeight);
			((ScummEngine_v71he *)_vm)->backgroundToForegroundBlit(blitRect);
		} else {
			((ScummEngine_v71he *)_vm)->_wiz->pgDrawWarpDrawLetter((WizRawPixel *)dstPtr,
				vs->w, vs->h,
				charPtr, _left, drawTop, origWidth, origHeight,
				colorLookupTable);
		}
#endif
	} else {
		Graphics::Surface dstSurface;
		Graphics::Surface backSurface;
		if (ignoreCharsetMask || !vs->hasTwoBuffers) {
			dstSurface = *vs;
			dstPtr = vs->getPixels(_left, drawTop);
		} else {
			dstSurface = _vm->_textSurface;
			dstPtr = (byte *)_vm->_textSurface.getBasePtr(_left * _vm->_textSurfaceMultiplier, (_top - _vm->_screenTop) * _vm->_textSurfaceMultiplier);
		}

		if (_blitAlso && vs->hasTwoBuffers) {
			backSurface = dstSurface;
			back = dstPtr;
			dstSurface = *vs;
			dstPtr = vs->getBackPixels(_left, drawTop);
		}

		if (!ignoreCharsetMask && vs->hasTwoBuffers) {
			drawTop = _top - _vm->_screenTop;
		}

		if ((is2byte || (_vm->_korTtfLatin && _vm->isKoreanHiRes())) && _vm->_game.platform != Common::kPlatformFMTowns) {
			int dx = (ignoreCharsetMask || !vs->hasTwoBuffers) ? _left + vs->xstart : _left;
			int dy = drawTop;

			// Korean hi-res mode: double-byte glyphs always go into the
			// scaled text surface, even for virtual screens which normally
			// receive text directly (the verb area, for instance). That is
			// the only buffer with enough resolution to hold them, and
			// drawStripToScreen() composites it over the upscaled graphics.
			//
			// The text surface is addressed in full screen coordinates, so
			// the vertical offset is derived from _top rather than drawTop.
			if (_vm->isKoreanHiRes()) {
				// Tell the drawing helpers whether this glyph is meant to
				// become part of the picture, so the text surface can keep
				// it when the transient layer is cleared.
				_vm->_hiResTextBurnIn = ignoreCharsetMask;
				const int m = _vm->_textSurfaceMultiplier;
				const int tx = (_left + vs->xstart) * m;
				int top = _top;

				// The game positions single byte characters against the
				// ascii font and double byte ones against the CJK font,
				// which lined up while each had its own bitmaps. A single
				// TrueType face draws both, so pin the single byte
				// characters to the same line the Hangul uses - otherwise
				// the punctuation sits on a baseline of its own.
				if (_vm->_korTtfLatin && _vm->_korTtfFont && !is2byte)
					top = _korTtfLineTop;
				else
					_korTtfLineTop = _top;

				const int ty = (top - _vm->_screenTop) * m;

				if (drawHiResKorChar(_vm->_textSurface, tx, ty, drawTop, _curKorChar))
					goto charDrawn;

				// Single byte characters have no double byte bitmap to fall
				// back on, so let the regular renderer handle them.
				if (!is2byte) {
					drawBitsN(dstSurface, dstPtr, charPtr, *_fontPtr, drawTop, origWidth, origHeight);
					goto charDrawn;
				}

				drawBits1(_vm->_textSurface, tx, ty, charPtr, ty, origWidth, origHeight);
				goto charDrawn;
			}

			drawBits1(dstSurface, dx, dy, charPtr, dy, origWidth, origHeight);
		} else {
			drawBitsN(dstSurface, dstPtr, charPtr, *_fontPtr, drawTop, origWidth, origHeight);
		}
charDrawn:
		_vm->_hiResTextBurnIn = false;

		if (_blitAlso && vs->hasTwoBuffers) {
			// FIXME: Revisiting this code, I think the _blitAlso mode is likely broken
			// right now -- we are copying stuff from "dstPtr" to "back", but "dstPtr" really
			// only conatains charset data...
			// One way to fix this: don't copy etc.; rather simply render the char twice,
			// once to each of the two buffers. That should hypothetically yield
			// identical results, though I didn't try it and right now I don't know
			// any spots where I can test this...
			if (!ignoreCharsetMask)
				error("This might be broken -- please report where you encountered this to Fingolfin");

			// Perform some clipping
			int w = MIN(width, dstSurface.w - _left);
			int h = MIN(height, dstSurface.h - drawTop);
			if (_left < 0) {
				w += _left;
				back -= _left;
				dstPtr -= _left;
			}
			if (drawTop < 0) {
				h += drawTop;
				back -= drawTop * backSurface.pitch;
				dstPtr -= drawTop * dstSurface.pitch;
			}

			// Blit the image data
			if (w > 0) {
				while (h-- > 0) {
					memcpy(back, dstPtr, w);
					back += backSurface.pitch;
					dstPtr += dstSurface.pitch;
				}
			}
		}
	}
}

bool CharsetRendererClassic::prepareDraw(uint16 chr) {
	bool is2byte = (chr >= 256 && _vm->_useCJKMode);
	if (is2byte) {
		_charPtr = _vm->get2byteCharPtr(chr);
		_width = _origWidth = _vm->_2byteWidth;
		_height = _origHeight = _vm->_2byteHeight;
		_offsX = _offsY = 0;

		if (_vm->_isIndy4Jap) {
			// Characters allow shadows only if this is the main virtual screen, and we are not drawing
			// a message on a GUI banner. The main menu is fine though, and allows shadows as well.
			VirtScreen *vs = _vm->findVirtScreen(_top);
			bool canDrawShadow = vs != nullptr && vs->number == kMainVirtScreen && !_vm->isMessageBannerActive();
			setShadowMode(canDrawShadow ? kNormalShadowType : kNoShadowType);
		}

		if (_shadowType != kNoShadowType) {
			_width++;
			_height++;
		}

		return true;
	} else {
		setShadowMode(kNoShadowType);
	}

	uint32 charOffs = READ_LE_UINT32(_fontPtr + chr * 4 + 4);
	assert(charOffs < 0x14000);
	if (!charOffs)
		return false;
	_charPtr = _fontPtr + charOffs;

	_width = _origWidth = _charPtr[0];
	_height = _origHeight = _charPtr[1];

	// Single byte characters take their width from the bitmap font here,
	// bypassing getCharWidth(); with TTF metrics that would leave the
	// punctuation advancing on the old grid while the glyph is drawn on
	// the new one.
	const int ttfWidth = _vm->getKorTtfCharWidth(chr);
	if (ttfWidth >= 0)
		_width = _origWidth = ttfWidth;

	if (_disableOffsX) {
		_offsX = 0;
	} else {
		_offsX = (signed char)_charPtr[2];
	}

	_offsY = (signed char)_charPtr[3];

	_charPtr += 4;	// Skip over char header
	return true;
}

void CharsetRendererClassic::drawChar(int chr, Graphics::Surface &s, int x, int y) {
	if (!prepareDraw(chr))
		return;

	byte *dst = (byte *)s.getBasePtr(x, y);

	bool is2byte = (_vm->_useCJKMode && chr >= 256);
	if (is2byte)
		drawBits1(s, x, y, _charPtr, y, _width, _height);
	else
		drawBitsN(s, dst, _charPtr, *_fontPtr, y, _width, _height);
}

void CharsetRendererClassic::drawBitsN(const Graphics::Surface &s, byte *dst, const byte *src, byte bpp, int drawTop, int width, int height) {
	int y, x;
	int color;
	byte numbits, bits;

	// In Korean hi-res mode the text surface is scaled up; replicate every
	// glyph pixel into an m x m block so that Latin text stays the same
	// apparent size as before. NB: callers pass a by-value copy of
	// _textSurface, so identify it by the pixel buffer, not by address.
	const int m = (s.getPixels() == _vm->_textSurface.getPixels()) ? _vm->_textSurfaceMultiplier : 1;
	int pitch = s.pitch - width * m;

	assert(bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8);
	bits = *src++;
	numbits = 8;
	byte *cmap = _vm->_charsetColorMap;

	// Indy4 Amiga always uses the room or verb palette map to match colors to
	// the currently setup palette, thus we need to select it over here too.
	// Done like the original interpreter.
	byte *amigaMap = nullptr;
	if (_vm->_game.platform == Common::kPlatformAmiga && _vm->_game.id == GID_INDY4) {
		if (_drawScreen == kVerbVirtScreen)
			amigaMap = _vm->_verbPalette;
		else
			amigaMap = _vm->_roomPalette;
	}

	// The caller hands us a pointer rather than coordinates, so recover the
	// column from it: an m x m block must not be written past the right or
	// bottom edge, and only the top left pixel of it was ever checked.
	const byte *const surfaceStart = (const byte *)s.getPixels();
	const int startX = s.pitch ? (int)((dst - surfaceStart) % s.pitch) : 0;

	for (y = 0; y < height && (y * m) + drawTop + m <= s.h; y++) {
		for (x = 0; x < width; x++) {
			color = (bits >> (8 - bpp)) & 0xFF;

			if (color && (y * m) + drawTop >= 0
					&& startX + (x * m) + m <= s.pitch) {
				const byte c = amigaMap ? amigaMap[cmap[color]] : cmap[color];
				for (int sy = 0; sy < m; ++sy) {
					for (int sx = 0; sx < m; ++sx)
						dst[sy * s.pitch + sx] = c;
				}
			}
			dst += m;
			bits <<= bpp;
			numbits -= bpp;
			if (numbits == 0) {
				bits = *src++;
				numbits = 8;
			}
		}
		dst += pitch + s.pitch * (m - 1);
	}
}

CharsetRendererTownsV3::CharsetRendererTownsV3(ScummEngine *vm) : CharsetRendererV3(vm), _sjisCurChar(0) {
}

int CharsetRendererTownsV3::getCharWidth(uint16 chr) const {
	if (_vm->isScummvmKorTarget()) {
		return CharsetRendererV3::getCharWidth(chr);
	}

	int spacing = 0;

	if (_vm->_useCJKMode) {
		if (chr >= 256)
			spacing = 8;
		else if (chr >= 128)
			spacing = 4;
	}

	if (!spacing)
		spacing = *(_widthTable + chr);

	return spacing;
}

int CharsetRendererTownsV3::getFontHeight() const {
	if (_vm->isScummvmKorTarget()) {
		return CharsetRendererV3::getFontHeight();
	}

	return _vm->_useCJKMode ? 8 : _fontHeight;
}

void CharsetRendererTownsV3::setShadowMode(ShadowType mode) {
	if (_vm->isScummvmKorTarget()) {
		CharsetRendererV3::setShadowMode(mode);
		return;
	}

	_shadowColor = 8;
	_shadowType = mode;

#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
	_shadowColor = 0x88;
#ifdef USE_RGB_COLOR
	if (_vm->_cjkFont)
		_vm->_cjkFont->setDrawingMode(mode != kNoShadowType ? Graphics::FontSJIS::kFMTownsShadowMode : Graphics::FontSJIS::kDefaultMode);
#endif
#endif
}

void CharsetRendererTownsV3::drawBits1(Graphics::Surface &dest, int x, int y, const byte *src, int drawTop, int width, int height) {
	if (_vm->isScummvmKorTarget()) {
		CharsetRendererV3::drawBits1(dest, x, y, src, drawTop, width, height);
		return;
	}

	if (y + height > dest.h)
		error("Trying to draw below screen boundaries");

#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
#ifdef USE_RGB_COLOR
	if (_sjisCurChar) {
		assert(_vm->_cjkFont);
		_vm->_cjkFont->drawChar(dest, _sjisCurChar, x, y, _color, _shadowColor);
		return;
	}
#endif
	bool scale2x = (_vm->_textSurfaceMultiplier == 2 && !(_sjisCurChar >= 256 && _vm->_useCJKMode) && (&dest == &_vm->_textSurface || &dest == &_vm->_virtscr[kBannerVirtScreen]));
#endif

	byte bits = 0;
	uint8 col = _color;
	int pitch = dest.pitch - width * dest.format.bytesPerPixel;
	byte *dst = (byte *)dest.getBasePtr(x, y);
	byte *dst2 = dst + dest.pitch;

#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
	byte *dst3 = dst2;
	byte *dst4 = dst2;
	if (scale2x) {
		dst3 = dst2 + dest.pitch;
		dst4 = dst3 + dest.pitch;
		pitch <<= 1;
	}
#endif

	for (y = 0; y < height && y + drawTop < dest.h; y++) {
		for (x = 0; x < width; x++) {
			if ((x % 8) == 0)
				bits = *src++;
			if ((bits & revBitMask(x % 8)) && y + drawTop >= 0) {
				if (dest.format.bytesPerPixel == 2) {
					if (_shadowType != kNoShadowType) {
						WRITE_UINT16(dst + 2, _vm->_16BitPalette[_shadowColor]);
						WRITE_UINT16(dst + dest.pitch, _vm->_16BitPalette[_shadowColor]);
					}
					WRITE_UINT16(dst, _vm->_16BitPalette[_color]);
				} else {
					if (_shadowType != kNoShadowType) {
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
						if (scale2x) {
							dst[2] = dst[3] = dst2[2] = dst2[3] = _shadowColor;
							dst3[0] = dst4[0] = dst3[1] = dst4[1] = _shadowColor;
						} else
#endif
						{
							dst[1] = dst2[0] = _shadowColor;
						}
					}
					dst[0] = col;

#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
					if (scale2x)
						dst[1] = dst2[0] = dst2[1] = col;
#endif
				}
			}
			dst += dest.format.bytesPerPixel;
			dst2 += dest.format.bytesPerPixel;
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
			if (scale2x) {
				dst++;
				dst2++;
				dst3 += 2;
				dst4 += 2;
			}
#endif
		}

		dst += pitch;
		dst2 += pitch;
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
		dst3 += pitch;
		dst4 += pitch;
#endif
	}
}
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
int CharsetRendererTownsV3::getDrawWidthIntern(uint16 chr) {
	if (_vm->isScummvmKorTarget()) {
		return CharsetRendererV3::getDrawWidthIntern(chr);
	}

#ifdef USE_RGB_COLOR
	if (_vm->_useCJKMode && chr > 127) {
		assert(_vm->_cjkFont);
		return _vm->_cjkFont->getCharWidth(chr);
	}
#endif
	return CharsetRendererV3::getDrawWidthIntern(chr);
}

int CharsetRendererTownsV3::getDrawHeightIntern(uint16 chr) {
	if (_vm->isScummvmKorTarget()) {
		return CharsetRendererV3::getDrawHeightIntern(chr);
	}

#ifdef USE_RGB_COLOR
	if (_vm->_useCJKMode && chr > 127) {
		assert(_vm->_cjkFont);
		return _vm->_cjkFont->getFontHeight();
	}
#endif
	return CharsetRendererV3::getDrawHeightIntern(chr);
}

void CharsetRendererTownsV3::setDrawCharIntern(uint16 chr) {
	_sjisCurChar = (!_vm->isScummvmKorTarget() && _vm->_useCJKMode && chr > 127) ? chr : 0;
}
#endif

#ifdef USE_RGB_COLOR
void CharsetRendererPCE::drawBits1(Graphics::Surface &dest, int x, int y, const byte *src, int drawTop, int width, int height) {
	byte *dst = (byte *)dest.getBasePtr(x, y);
	if (_sjisCurChar) {
		assert(_vm->_cjkFont);
		uint16 col1 = _color;
		uint16 col2 = _shadowColor;

		if (dest.format.bytesPerPixel == 2) {
			col1 = _vm->_16BitPalette[col1];
			col2 = _vm->_16BitPalette[col2];
		}

		_vm->_cjkFont->drawChar(dst, _sjisCurChar, dest.pitch, dest.format.bytesPerPixel, col1, col2, -1, -1);
		return;
	}

	byte bits = 0;

	for (y = 0; y < height && y + drawTop < dest.h; y++) {
		int bitCount = 0;
		for (x = 0; x < width; x++) {
			if ((bitCount % 8) == 0)
				bits = *src++;
			if ((bits & revBitMask(bitCount % 8)) && y + drawTop >= 0) {
				if (dest.format.bytesPerPixel == 2) {
					if (_shadowType != kNoShadowType)
						WRITE_UINT16(dst + dest.pitch + 2, _vm->_16BitPalette[_shadowColor]);
					WRITE_UINT16(dst, _vm->_16BitPalette[_color]);
				} else {
					if (_shadowType != kNoShadowType)
						*(dst + dest.pitch + 1) = _shadowColor;
					*dst = _color;
				}
			}
			dst += dest.format.bytesPerPixel;
			bitCount++;
		}

		dst += dest.pitch - width * dest.format.bytesPerPixel;
	}
}

int CharsetRendererPCE::getDrawWidthIntern(uint16 chr) {
	if (_vm->_useCJKMode && chr > 127)
		return _vm->_2byteWidth;
	return CharsetRendererV3::getDrawWidthIntern(chr);
}

int CharsetRendererPCE::getDrawHeightIntern(uint16 chr) {
	if (_vm->_useCJKMode && chr > 127)
		return _vm->_2byteHeight;
	return CharsetRendererV3::getDrawHeightIntern(chr);
}

void CharsetRendererPCE::setDrawCharIntern(uint16 chr) {
	_sjisCurChar = (_vm->_useCJKMode && chr > 127) ? chr : 0;
}
#endif

CharsetRendererMac::CharsetRendererMac(ScummEngine *vm, const Common::Path &fontFile)
	: CharsetRendererCommon(vm), _lastTop(0) {

	// The original Macintosh interpreter didn't use the correct spacing
	// between characters for some of the text, e.g. the Grail Diary. This
	// appears to have been because of rounding errors, and was apparently
	// fixed in Loom. Enabling this allows ScummVM to draw the text more
	// correctly, at the cost of not matching the original quite as well.
	// (At the time of writing, there are still cases, at least in Loom,
	// where text isn't correctly positioned.)

	_useCorrectFontSpacing = _vm->_game.id == GID_LOOM || _vm->enhancementEnabled(kEnhSubFmtCntChanges);
	_pad = false;
	_glyphSurface = nullptr;

	if (_vm->_renderMode == Common::kRenderMacintoshBW) {
		const Graphics::Font *font = _vm->_macGui->getFontByScummId(0);

		_glyphSurface = new Graphics::Surface();
		_glyphSurface->create(font->getMaxCharWidth(), font->getFontHeight(), Graphics::PixelFormat::createFormatCLUT8());
	}
}

CharsetRendererMac::~CharsetRendererMac() {
	if (_glyphSurface) {
		_glyphSurface->free();
		delete _glyphSurface;
	}
}

void CharsetRendererMac::setCurID(int32 id) {
	if (id == -1)
		return;

	// This should only happen, if it happens at all, with older savegames.
	// Font 0 is the sensible default font for both Loom and Indy 3.

	int numFonts = (_vm->_game.id == GID_LOOM) ? 1 : 2;

	if (id >= numFonts) {
		warning("CharsetRenderMac::setCurId: Invalid font id %d, using 0 instead", id);
		id = 0;
	}

	_curId = id;
	_font = _vm->_macGui->getFontByScummId(_curId);
}

int CharsetRendererMac::getStringWidth(int arg, const byte *text) {
	int pos = 0;
	int width = 0;
	int chr;

	while ((chr = text[pos++]) != 0) {
		// The only control codes I've seen in use are line breaks in
		// Loom. In Indy 3, I haven't seen anything at all like it.
		if (chr == 255) {
			chr = text[pos++];
			if (chr == 1) // 'Newline'
				break;
			warning("getStringWidth: Unexpected escape sequence %d", chr);
		} else {
			width += getDrawWidthIntern(chr);
		}
	}

	return width / 2;
}

int CharsetRendererMac::getDrawWidthIntern(uint16 chr) const {
	return _font->getCharWidth(chr);
}

int CharsetRendererMac::getFontHeight() const {
	return _font->getFontHeight() / 2;
}

int CharsetRendererMac::getCharWidth(uint16 chr) const {
	return _font->getCharWidth(chr) / 2;
}

void CharsetRendererMac::printChar(int chr, bool ignoreCharsetMask) {
	// This function does most of the heavy lifting printing the game
	// text.

	// If this is the beginning of a line, assume the position will be
	// correct without any padding.

	if (_firstChar || _top != _lastTop) {
		_pad = false;
	}

	VirtScreen *vs;

	if ((vs = _vm->findVirtScreen(_top)) == nullptr) {
		warning("findVirtScreen(%d) failed, therefore printChar cannot print '\\x%X'", _top, chr);
		return;
	}

	if (chr == '@')
		return;

	// Scale up the virtual coordinates to get the high resolution ones.

	int macLeft = 2 * _left;
	int macTop = 2 * _top;
	// The last character ended on an odd X coordinate. This information
	// was lost in the rounding, so we compensate for it here.

	if (_pad) {
		macLeft++;
		_pad = false;
	}

	bool enableShadow = (_shadowType != kNoShadowType);
	int color = _color;

	// HACK: Notes and their names should always be drawn with a shadow.
	//       Actually, this doesn't quite match the original but I can't
	//       figure out what the original does here. The "c" looks like
	//       it's shadowed in the normal way, but everything else looks
	//       kind-of-but-not-quite outlined instead. Weird.
	//
	//       Even weirder, I've seen screenshots where there is no
	//       shadowing at all. I'll just keep it like this for now,
	//       because it makes the notes stand out a bit better.

	if (_vm->_game.id == GID_LOOM) {
		if ((chr >= 16 && chr <= 23) || chr == 60 || chr == 95) {
			enableShadow = true;
		}

		// HACK: Apparently, note names are never drawn in light gray.
		// Only white for known notes, and dark gray for unknown ones.
		// This hack ensures that we won't be left with a mix of white
		// and light gray note names, because apparently the game never
		// changes them back to light gray once the draft is done?

		if (chr >= 16 && chr <= 23 && _color == 7)
			color = 15;
	}

	bool drawToTextBox = (vs->number == kTextVirtScreen && _vm->_game.id == GID_INDY3);

	if (drawToTextBox)
		_vm->_macGui->printCharToTextArea(chr, macLeft, macTop, color);
	else
		printCharInternal(chr, color, enableShadow, macLeft, macTop);

	// HACK: The way we combine high and low resolution graphics means
	//       that sometimes, when a note name is drawn on the distaff, the
	//       note itself gets overdrawn by the low-resolution graphics.
	//
	//       The only workaround I can think of is to force the note to be
	//       redrawn along with its name. It's enough to redraw it on the
	//       text surface. We can assume the correct color is already on
	//       screen.

	if (_vm->_game.id == GID_LOOM) {
		if (chr >= 16 && chr <= 23) {
			int xOffset[] = { 16, 14, 12, 8, 6, 2, 0, 8 };

			int note = (chr == 23) ? 60 : 95;
			printCharInternal(note, -1, enableShadow, macLeft + 18, macTop + xOffset[chr - 16]);
		}
	}

	// Mark the virtual screen as dirty, using downscaled coordinates.

	int left, right, top, bottom, width;

	width = getDrawWidthIntern(chr);

	// HACK: Indiana Jones and the Last Crusade uses incorrect spacing
	// betweeen letters. Note that this incorrect spacing does not extend
	// to the text boxes, nor does it seem to be used when figuring out
	// the width of a string (e.g. to center text on screen). It is,
	// however, used for things like the Grail Diary.

	if (!_useCorrectFontSpacing && !drawToTextBox && (width & 1))
		width++;

	if (enableShadow) {
		left = macLeft / 2;
		right = (macLeft + width + 3) / 2;
		top = macTop / 2;
		bottom = (macTop + _font->getFontHeight() + 3) / 2;
	} else {
		left = (macLeft + 1) / 2;
		right = (macLeft + width + 1) / 2;
		top = (macTop + 1) / 2;
		bottom = (macTop + _font->getFontHeight() + 1) / 2;
	}

	if (_firstChar) {
		_str.left = left;
		_str.top = top;
		_str.right = right;
		_str.bottom = top;
		_firstChar = false;
	} else {
		if (_str.left > left)
			_str.left = left;
		if (_str.right < right)
			_str.right = right;
		if (_str.bottom < bottom)
			_str.bottom = bottom;
	}

	if (!drawToTextBox)
		_vm->markRectAsDirty(vs->number, left, right, top - vs->topline, bottom - vs->topline);

	if (!ignoreCharsetMask) {
		_hasMask = true;
		_textScreenID = vs->number;
	}

	// The next character may have to be adjusted to compensate for
	// rounding errors.

	macLeft += width;
	if (macLeft & 1)
		_pad = true;

	_left = macLeft / 2;
	_lastTop = _top;
}

byte CharsetRendererMac::getTextColor() {
	if (_vm->_renderMode == Common::kRenderMacintoshBW) {
		// White and black can be rendered as is, and 8 is the color
		// used for disabled text (notes in Loom). Everything else
		// should be white.

		if (_color == 0 || _color == 15 || _color == 8)
			return _color;
		return 15;
	}
	return _color;
}

byte CharsetRendererMac::getTextShadowColor() {
	if (_vm->_renderMode == Common::kRenderMacintoshBW) {
		if (getTextColor() == 0)
			return 15;
		return 0;
	}
	return _shadowColor;
}

void CharsetRendererMac::printCharInternal(int chr, int color, bool shadow, int x, int y) {
	if (_vm->_game.id == GID_LOOM) {
		x++;
		y++;
	}

	if (shadow) {
		byte shadowColor = getTextShadowColor();

		if (_vm->_game.id == GID_LOOM) {
			// Shadowing is a bit of guesswork. It doesn't look
			// like it's using the Mac's built-in form of shadowed
			// text (which, as I recall it, never looked
			// particularly good anyway). This seems to match the
			// original look for normal text.

			_font->drawChar(&_vm->_textSurface, chr, x + 1, y - 1, 0);
			_font->drawChar(&_vm->_textSurface, chr, x - 1, y + 1, 0);
			_font->drawChar(&_vm->_textSurface, chr, x + 2, y + 2, 0);

			if (color != -1) {
				_font->drawChar(_vm->_macScreen, chr, x + 1, y - 1 + 2 * _vm->_macScreenDrawOffset, shadowColor);
				_font->drawChar(_vm->_macScreen, chr, x - 1, y + 1 + 2 * _vm->_macScreenDrawOffset, shadowColor);
				_font->drawChar(_vm->_macScreen, chr, x + 2, y + 2 + 2 * _vm->_macScreenDrawOffset, shadowColor);
			}
		} else {
			// Indy 3 uses simpler shadowing, and doesn't need the
			// "draw only on text surface" hack.

			_font->drawChar(&_vm->_textSurface, chr, x + 1, y + 1, 0);
			_font->drawChar(_vm->_macScreen, chr, x + 1, y + 1 + 2 * _vm->_macScreenDrawOffset, shadowColor);
		}
	}

	_font->drawChar(&_vm->_textSurface, chr, x, y, 0);

	if (color != -1) {
		color = getTextColor();

		if (_vm->_renderMode == Common::kRenderMacintoshBW && color != 0 && color != 15) {
			_glyphSurface->fillRect(Common::Rect(_glyphSurface->w, _glyphSurface->h), 0);
			_font->drawChar(_glyphSurface, chr, 0, 0, 15);

			for (int y0 = 0; y0 < _glyphSurface->h; y0++) {
				for (int x0 = 0; x0 < _glyphSurface->w; x0++) {
					if (_glyphSurface->getPixel(x0, y0)) {
						int x1 = x + x0;
						int y1 = y + y0 + 2 * _vm->_macScreenDrawOffset;

						_vm->_macScreen->setPixel(x1, y1, ((x1 + y1) & 1) ? 0 : 15);
					}
				}
			}
		} else {
			_font->drawChar(_vm->_macScreen, chr, x, y + 2 * _vm->_macScreenDrawOffset, color);
		}
	}
}

void CharsetRendererMac::setColor(byte color, bool) {
	_color = color;
	_shadowColor = 0;

	_shadowType = ((color & 0xF0) != 0) ? kNormalShadowType : kNoShadowType;
	// Anything outside the ordinary palette should be fine.
	_shadowColor = 255;
	_color &= 0x0F;
}

#ifdef ENABLE_SCUMM_7_8
CharsetRendererV7::CharsetRendererV7(ScummEngine *vm) : CharsetRendererClassic(vm, vm->_useCJKMode && vm->_language != Common::JA_JPN ? 1 : 0),
	_direction(vm->_language == Common::HE_ISR ? -1 : 1),
	_newStyle(vm->_useCJKMode) {
}

int CharsetRendererV7::draw2byte(byte *buffer, Common::Rect &clipRect, int x, int y, int pitch, int16 col, uint16 chr) {
	// I am aware of not doing anything with the clipRect here, but I currently see no need to upgrade the old rendering with that.
	const byte *src = _vm->get2byteCharPtr(chr);
	buffer += (y * pitch + x);
	_origWidth = _vm->_2byteWidth;
	_origHeight = _vm->_2byteHeight;
	uint8 bits = 0;
	pitch -= _origWidth;
	while (_origHeight--) {
		for (x = 0; x < _origWidth; ++x) {
			if ((x % 8) == 0)
				bits = *src++;
			if (bits & revBitMask(x % 8)) {
				buffer[0] = col;
				buffer[1] = _shadowColor;
			}
			buffer++;
		}
		buffer += pitch;
	}
	return _origWidth + _cjkSpacing;
}

int CharsetRendererV7::drawCharV7(byte *buffer, Common::Rect &clipRect, int x, int y, int pitch, int16 col, TextStyleFlags flags, byte chr) {
	if (!prepareDraw(chr))
		return 0;

	_width = getCharWidth(chr);

	if (_direction < 0)
		x -= _width;

	int width = MIN(_origWidth, clipRect.right - x);
	int height = MIN(_origHeight, clipRect.bottom - (y + _offsY));

	// This can happen e.g. on The Dig (PT-BR version) during the credits in which
	// the above calculation, done on character 0x80, results in a negative number;
	// this could spiral in an infinite loop and bad memory accesses (see #15067)
	if (height < 0)
		height = 0;

	_vm->_charsetColorMap[1] = col;
	byte *cmap = _vm->_charsetColorMap;
	const byte *src = _charPtr;
	byte *dst = buffer + (y + _offsY) * pitch + x;
	uint8 bpp = *_fontPtr;
	byte bits = *src++;
	byte numbits = 8;
	pitch -= _origWidth;

	while (height--) {
		for (int dx = x; dx < x + _origWidth; ++dx) {
			byte color = (bits >> (8 - bpp)) & 0xFF;
			if (color && dx >= 0 && dx < x + width && (y + _offsY) >= 0)
				*dst = cmap[color];
			dst++;
			bits <<= bpp;
			numbits -= bpp;
			if (numbits == 0) {
				bits = *src++;
				numbits = 8;
			}
		}
		dst += pitch;
		++y;
	}

	return _direction * width;
}


int CharsetRendererV7::getCharWidth(uint16 chr) const {
	if ((chr & 0x80) && _vm->_useCJKMode)
		return _vm->_2byteWidth + _cjkSpacing;

	int offs = READ_LE_UINT32(_fontPtr + (chr & 0xFF) * 4 + 4);
	// SCUMM7 does not use the "kerning" from _fontPtr[offs + 2] here (compare CharsetRendererClassic::getCharWidth()
	// to see the difference. Verfied from disasm and comparison with DOSBox (hard to notice, but e. g. the 'a' character
	// used to be too narrow by 1 pixel, so all lines containing that character were slightly off).
	return offs ? _fontPtr[offs] : 0;
}

CharsetRendererNut::CharsetRendererNut(ScummEngine *vm) : CharsetRenderer(vm) {
	_current = 0;

	for (int i = 0; i < 5; i++) {
		_fr[i] = NULL;
	}
}

CharsetRendererNut::~CharsetRendererNut() {
	for (int i = 0; i < 5; i++) {
		delete _fr[i];
	}
}

void CharsetRendererNut::setCurID(int32 id) {
	if (id == -1)
		return;

	int numFonts = ((_vm->_game.id == GID_CMI) && (_vm->_game.features & GF_DEMO)) ? 4 : 5;
	assert(id < numFonts);
	(void)numFonts;
	_curId = id;
	if (!_fr[id]) {
		char fontname[11];
		Common::sprintf_s(fontname, "font%d.nut", id);
		_fr[id] = new NutRenderer(_vm, fontname);
	}
	_current = _fr[id];
	assert(_current);
}

int CharsetRendererNut::setFont(int id) {
	int old = _curId;
	if (id >= 0)
		setCurID(id);
	return old;
}

int CharsetRendererNut::getCharHeight(uint16 chr) const {
	assert(_current);
	return _current->getCharHeight(chr & 0xFF);
}

int CharsetRendererNut::getCharWidth(uint16 chr) const {
	assert(_current);
	return _current->getCharWidth(chr & 0xFF);
}

int CharsetRendererNut::getFontHeight() const {
	assert(_current);
	return _current->getFontHeight();
}

int CharsetRendererNut::draw2byte(byte *buffer, Common::Rect &clipRect, int x, int y, int pitch, int16 col, uint16 chr) {
	assert(_current);
	return _current->draw2byte(buffer, clipRect, x, y, pitch, col, chr);
}

int CharsetRendererNut::drawCharV7(byte *buffer, Common::Rect &clipRect, int x, int y, int pitch, int16 col, TextStyleFlags flags, byte chr) {
	assert(_current);
	return _current->drawCharV7(buffer, clipRect, x, y, pitch, col, flags, chr);
}
#endif

void CharsetRendererNES::printChar(int chr, bool ignoreCharsetMask) {
	int width, height, origWidth, origHeight;
	VirtScreen *vs;
	byte *charPtr;

	// Init it here each time since it is cheap and fixes bug with
	// charset after game load
	_trTable = _vm->getResourceAddress(rtCostume, 77) + 2;

	// HACK: how to set it properly?
	if (_top == 0)
		_top = 16;

	if ((vs = _vm->findVirtScreen(_top)) == nullptr)
		return;

	if (chr == '@')
		return;

	charPtr = _vm->_NESPatTable[1] + _trTable[chr - 32] * 16;
	width = getCharWidth(chr);
	height = 8;

	origWidth = width;
	origHeight = height;

	if (_firstChar) {
		_str.left = _left;
		_str.top = _top;
		_str.right = _left;
		_str.bottom = _top;
		_firstChar = false;
	}

	int drawTop = _top - vs->topline;
	int offset = vs->number == kTextVirtScreen ? 16 : 0;
	_vm->markRectAsDirty(vs->number, _left + offset, _left + width + offset, drawTop, drawTop + height);

	if (!ignoreCharsetMask) {
		_hasMask = true;
		_textScreenID = vs->number;
	}

	if (ignoreCharsetMask || !vs->hasTwoBuffers)
		drawBits1(*vs, _left + vs->xstart + offset, drawTop, charPtr, drawTop, origWidth, origHeight);
	else
		drawBits1(_vm->_textSurface, _left + offset, _top, charPtr, drawTop, origWidth, origHeight);

	if (_str.left > _left)
		_str.left = _left;

	_left += origWidth;

	if (_str.right < _left) {
		_str.right = _left;
		if (_shadowType != kNoShadowType)
			_str.right++;
	}

	if (_str.bottom < _top + height)
		_str.bottom = _top + height;
}

void CharsetRendererNES::drawChar(int chr, Graphics::Surface &s, int x, int y) {
	byte *charPtr;
	int width, height;

	if (!_trTable)
		_trTable = _vm->getResourceAddress(rtCostume, 77) + 2;

	charPtr = _vm->_NESPatTable[1] + _trTable[chr - 32] * 16;
	width = getCharWidth(chr);
	height = 8;

	drawBits1(s, x, y, charPtr, y, width, height);
}

#ifdef USE_RGB_COLOR
#ifndef DISABLE_TOWNS_DUAL_LAYER_MODE
CharsetRendererTownsClassic::CharsetRendererTownsClassic(ScummEngine *vm) : CharsetRendererClassic(vm, 0), _sjisCurChar(0) {
	assert(vm->_game.platform == Common::kPlatformFMTowns);
}

int CharsetRendererTownsClassic::getCharWidth(uint16 chr) const {
	int spacing = 0;

	if (_vm->_useCJKMode) {
		if ((chr & 0xff00) == 0xfd00) {
			chr &= 0xff;
		} else if (chr >= 256) {
			spacing = 8;
		} else if (useFontRomCharacter(chr)) {
			spacing = 4;
		}

		if (spacing) {
			if (_vm->_game.id == GID_MONKEY) {
				spacing++;
				if (_curId == 2)
					spacing++;
			} else if (_vm->_game.id != GID_INDY4 && _curId == 1) {
				spacing++;
			}
		}
	}

	if (!spacing) {
		int offs = READ_LE_UINT32(_fontPtr + chr * 4 + 4);
		if (offs)
			spacing = _fontPtr[offs] + (signed char)_fontPtr[offs + 2];
	}

	return spacing;
}

int CharsetRendererTownsClassic::getFontHeight() const {
	static const uint8 sjisFontHeightM1[] = { 0, 8, 9, 8, 9, 8, 9, 0, 0, 0 };
	static const uint8 sjisFontHeightM2[] = { 0, 8, 9, 9, 9, 8, 9, 9, 9, 8 };
	static const uint8 sjisFontHeightI4[] = { 0, 8, 9, 9, 9, 8, 8, 8, 8, 8 };
	const uint8 *htbl = (_vm->_game.id == GID_MONKEY) ? sjisFontHeightM1 : ((_vm->_game.id == GID_INDY4) ? sjisFontHeightI4 : sjisFontHeightM2);
	return _vm->_useCJKMode ? htbl[_curId] : _fontHeight;
}

void CharsetRendererTownsClassic::drawBitsN(const Graphics::Surface&, byte *dst, const byte *src, byte bpp, int drawTop, int width, int height) {
	if (_sjisCurChar) {
		assert(_vm->_cjkFont);
		_vm->_cjkFont->drawChar(_vm->_textSurface, _sjisCurChar, _left * _vm->_textSurfaceMultiplier, (_top - _vm->_screenTop) * _vm->_textSurfaceMultiplier, _vm->_townsCharsetColorMap[1], _shadowColor);
		return;
	}

	bool scale2x = (_vm->_textSurfaceMultiplier == 2);
	dst = (byte *)_vm->_textSurface.getBasePtr(_left * _vm->_textSurfaceMultiplier, (_top - _vm->_screenTop) * _vm->_textSurfaceMultiplier);

	int y, x;
	int color;
	byte numbits, bits;

	int pitch = _vm->_textSurface.pitch - width;

	assert(bpp == 1 || bpp == 2 || bpp == 4 || bpp == 8);
	bits = *src++;
	numbits = 8;
	byte *cmap = _vm->_townsCharsetColorMap;
	byte *dst2 = dst;

	if (scale2x) {
		dst2 += _vm->_textSurface.pitch;
		pitch <<= 1;
	}

	for (y = 0; y < height && y + drawTop < _vm->_textSurface.h; y++) {
		for (x = 0; x < width; x++) {
			color = (bits >> (8 - bpp)) & 0xFF;

			if (color && y + drawTop >= 0) {
				*dst = cmap[color];
				if (scale2x)
					dst[1] = dst2[0] = dst2[1] = dst[0];
			}
			dst++;

			if (scale2x) {
				dst++;
				dst2 += 2;
			}

			bits <<= bpp;
			numbits -= bpp;
			if (numbits == 0) {
				bits = *src++;
				numbits = 8;
			}
		}
		dst += pitch;
		dst2 += pitch;
	}
}

bool CharsetRendererTownsClassic::prepareDraw(uint16 chr) {
	processCharsetColors();
	bool noSjis = false;

	if (_vm->_useCJKMode) {
		if ((chr & 0x00ff) == 0x00fd) {
			chr >>= 8;
			noSjis = true;
		}
	}

	if (useFontRomCharacter(chr) && !noSjis) {
		setupShadowMode();
		_charPtr = nullptr;
		_sjisCurChar = chr;

		_width = getCharWidth(chr);
		// For whatever reason MI1 uses a different font width
		// for alignment calculation and for drawing when
		// charset 2 is active. This fixes some subtle glitches.
		if (_vm->_game.id == GID_MONKEY && _curId == 2)
			_width--;
		_origWidth = _width;

		_origHeight = _height = getFontHeight();
		_offsX = _offsY = 0;
	} else if (_vm->_useCJKMode && (chr >= 128) && !noSjis) {
		setupShadowMode();
		_origWidth = _width = _vm->_2byteWidth;
		_origHeight = _height = _vm->_2byteHeight;
		_charPtr = _vm->get2byteCharPtr(chr);
		_offsX = _offsY = 0;
		if (_shadowType != kNoShadowType) {
			_width++;
			_height++;
		}
	} else {
		_sjisCurChar = 0;
		return CharsetRendererClassic::prepareDraw(chr);
	}
	return true;
}

void CharsetRendererTownsClassic::setupShadowMode() {
	_shadowType = kNormalShadowType;
	_shadowColor = _vm->_townsCharsetColorMap[0];
	assert(_vm->_cjkFont);

	if (((_vm->_game.id == GID_MONKEY) && (_curId == 2 || _curId == 4 || _curId == 6)) ||
		((_vm->_game.id == GID_MONKEY2) && (_curId != 1 && _curId != 5 && _curId != 9)) ||
		((_vm->_game.id == GID_INDY4) && (_curId == 2 || _curId == 3 || _curId == 4))) {
			_vm->_cjkFont->setDrawingMode(Graphics::FontSJIS::kOutlineMode);
	} else {
		_vm->_cjkFont->setDrawingMode(Graphics::FontSJIS::kDefaultMode);
	}

	_vm->_cjkFont->toggleFlippedMode((_vm->_game.id == GID_MONKEY || _vm->_game.id == GID_MONKEY2) && _curId == 3);
}

bool CharsetRendererTownsClassic::useFontRomCharacter(uint16 chr) const {
	if (!_vm->_useCJKMode)
		return false;

	// Some SCUMM 5 games contain hard coded logic to determine whether to use
	// the SCUMM fonts or the FM-Towns font rom to draw a character. For the other
	// games we will simply check for a character greater 127.
	if (chr < 128) {
		if (((_vm->_game.id == GID_MONKEY2 && _curId != 0) || (_vm->_game.id == GID_INDY4 && _curId != 3)) && (chr > 31 && chr != 94 && chr != 95 && chr != 126 && chr != 127))
			return true;
		return false;
	}
	return true;
}

void CharsetRendererTownsClassic::processCharsetColors() {
	for (int i = 0; i < (1 << _bitsPerPixel); i++) {
		uint8 c = _vm->_charsetColorMap[i];

		if (c > 16) {
			uint8 t = (_vm->_currentPalette[c * 3] < 32) ? 4 : 12;
			t |= ((_vm->_currentPalette[c * 3 + 1] < 32) ? 2 : 10);
			t |= ((_vm->_currentPalette[c * 3 + 2] < 32) ? 1 : 9);
			c = t;
		}

		if (c == 0)
			c = _vm->_townsOverrideShadowColor;

		c = ((c & 0x0f) << 4) | (c & 0x0f);
		_vm->_townsCharsetColorMap[i] = c;
	}
}
#endif
#endif

void CharsetRendererNES::drawBits1(Graphics::Surface &dest, int x, int y, const byte *src, int drawTop, int width, int height) {
	byte *dst = (byte *)dest.getBasePtr(x, y);
	for (int i = 0; i < 8; i++) {
		byte c0 = src[i];
		byte c1 = src[i + 8];
		for (int j = 0; j < 8; j++)
			dst[j] = _vm->_NESPalette[0][((c0 >> (7 - j)) & 1) | (((c1 >> (7 - j)) & 1) << 1) |
			(_color ? 12 : 8)];
		dst += dest.pitch;
	}
}

} // End of namespace Scumm
