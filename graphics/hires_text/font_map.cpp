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

#include "graphics/hires_text/font_map.h"

#include "common/formats/ini-file.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace Graphics {

// The maximum the text surface multiplier is allowed to reach. Three is not a
// technical limit but a practical one: at 3x a 320x200 game already needs a
// 960x600 text surface. Raising this policy limit needs adapter memory tests.
static const int kMaxScale = 3;

HiResTextConfig::HiResTextConfig() {
	clear();
}

void HiResTextConfig::clear() {
	scale = 1;
	alpha = false;
	scaleFromMap = false;
	alphaFromMap = false;

	encoding = Common::kCodePageInvalid;
	encodingFromMap = false;

	bitmapPattern.clear();
	bitmapSingle.clear();
	bitmapGlyphs = 0;

	for (int i = 0; i < kHiResRoleCount; ++i) {
		ttfPath[i] = Common::Path();
		ttfSize[i] = 0;
		ttfSizeRelative[i] = false;
		ttfSupersample[i] = 1;
	}
	ttfStringMode = false;

	metricsSource = kHiResMetricsGame;
	legacy.latinEnabled = false;
	legacy.latinTtfPath = Common::Path();
	legacy.latinBitmapName.clear();
	legacy.latinTtfMetrics = kHiResMetricsGame;
	legacy.latinBitmapMetrics = kHiResMetricsGame;

	shadowMode = kHiResShadowGame;
	shadowOffset = -1;
	shadowColor = 0;
	shadowColorSet = false;

	translationName.clear();
	heightRoles.clear();
	glyphOverrides.clear();
	scopedGlyphOverrides.clear();
}

bool HiResTextConfig::glyphOverride(uint32 code, HiResGlyphOverride &out, int scope) const {
	// The scope's own table wins, so a charset that draws an arrow where the
	// others draw an underscore can say so without disturbing them.
	if (scope >= 0 && scope < (int)scopedGlyphOverrides.size()) {
		Common::HashMap<uint32, HiResGlyphOverride>::const_iterator s =
			scopedGlyphOverrides[scope].find(code);
		if (s != scopedGlyphOverrides[scope].end()) {
			out = s->_value;
			return true;
		}
	}

	Common::HashMap<uint32, HiResGlyphOverride>::const_iterator it =
		glyphOverrides.find(code);
	if (it == glyphOverrides.end())
		return false;
	out = it->_value;
	return true;
}

int HiResTextConfig::roleForHeight(int height) const {
	Common::HashMap<int, int>::const_iterator it = heightRoles.find(height);
	if (it != heightRoles.end())
		return it->_value;
	return kHiResRoleDefault;
}

int HiResFontMap::parseRole(const Common::String &name) {
	if (name.equalsIgnoreCase("title"))
		return kHiResRoleTitle;
	if (name.equalsIgnoreCase("bold"))
		return kHiResRoleBold;
	return kHiResRoleDefault;
}

Common::CodePage HiResFontMap::parseCodePage(const Common::String &name) {
	static const struct {
		const char *name;
		Common::CodePage page;
	} singleBytePages[] = {
		{ "cp1250", Common::kWindows1250 },
		{ "cp1251", Common::kWindows1251 },
		{ "cp1252", Common::kWindows1252 },
		{ "cp1253", Common::kWindows1253 },
		{ "cp1254", Common::kWindows1254 },
		{ "cp1255", Common::kWindows1255 },
		{ "cp1256", Common::kWindows1256 },
		{ "cp1257", Common::kWindows1257 },
		{ "iso-8859-1", Common::kISO8859_1 },
		{ "latin1", Common::kISO8859_1 },
		{ "iso-8859-2", Common::kISO8859_2 },
		{ "iso-8859-5", Common::kISO8859_5 },
		{ "macroman", Common::kMacRoman },
		{ "maccentraleurope", Common::kMacCentralEurope },
		{ "cp850", Common::kDos850 },
		{ "cp862", Common::kDos862 },
		{ "cp866", Common::kDos866 },
		{ "ascii", Common::kASCII }
	};
	for (uint i = 0; i < ARRAYSIZE(singleBytePages); ++i) {
		if (name.equalsIgnoreCase(singleBytePages[i].name))
			return singleBytePages[i].page;
	}
	if (name.equalsIgnoreCase("cp932") || name.equalsIgnoreCase("sjis"))
		return Common::kWindows932;
	if (name.equalsIgnoreCase("cp936") || name.equalsIgnoreCase("gbk"))
		return Common::kWindows936;
	if (name.equalsIgnoreCase("cp949") || name.equalsIgnoreCase("uhc") || name.equalsIgnoreCase("ksc5601") ||
		name.equalsIgnoreCase("euc-kr"))
		return Common::kWindows949;
	if (name.equalsIgnoreCase("cp950") || name.equalsIgnoreCase("big5"))
		return Common::kWindows950;
	if (name.equalsIgnoreCase("johab"))
		return Common::kJohab;
	if (name.equalsIgnoreCase("utf8") || name.equalsIgnoreCase("utf-8"))
		return Common::kUtf8;
	return Common::kCodePageInvalid;
}

Common::Path HiResFontMap::resolvePath(const Common::String &value, const Common::Path &baseDir) {
	if (value.empty())
		return Common::Path();

	const char first = value[0];
	const bool absolute = (first == '/' || first == '\\') ||
						  (value.size() > 2 && value[1] == ':');
	if (absolute)
		return Common::Path(value);

	return baseDir.join(Common::Path(value));
}

namespace {

/// Read a key, trying each qualified section name before the bare one.
bool getKey(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
			const char *section, const char *key, Common::String &value) {
	for (uint i = 0; i < qualifiers.size(); ++i) {
		if (qualifiers[i].empty())
			continue;
		const Common::String qualified =
			Common::String::format("%s:%s", section, qualifiers[i].c_str());
		if (ini.getKey(key, qualified, value))
			return true;
	}
	return ini.getKey(key, section, value);
}

// Bounded decimal parsing without atoi overflow or acceptance of trailing junk.
bool parseNumber(const char *&p, int maxValue, int &out) {
	if (*p < '0' || *p > '9')
		return false;
	int n = 0;
	while (*p >= '0' && *p <= '9') {
		const int digit = *p++ - '0';
		if (n > (maxValue - digit) / 10 || digit > maxValue)
			return false;
		n = n * 10 + digit;
	}
	out = n;
	return true;
}

bool parseInteger(const Common::String &value, int maxValue, int &out) {
	const char *p = value.c_str();
	return parseNumber(p, maxValue, out) && *p == 0;
}

bool parseMapBool(const Common::String &value, bool &out) {
	if (value.equalsIgnoreCase("true")) {
		out = true;
		return true;
	}
	if (value.equalsIgnoreCase("false")) {
		out = false;
		return true;
	}
	int n;
	if (!parseInteger(value, 0x7fffffff, n))
		return false;
	out = n != 0;
	return true;
}

/**
 * A character code written as "0x5e", "u+2192" or plain decimal.
 *
 * Both sides of a [glyphs] entry are codes - the game's own character on the
 * left, a Unicode code point on the right - so one parser serves both. Hex is
 * bounded the same way the decimal one is, since these strings ship with a
 * translation and are not the engine's own data.
 */
bool parseCodeValue(const Common::String &value, uint32 &out) {
	if (value.empty())
		return false;

	const char *p = value.c_str();
	int base = 10;
	if (value.hasPrefixIgnoreCase("u+")) {
		p += 2;
		base = 16;
	} else if (value.hasPrefixIgnoreCase("0x")) {
		p += 2;
		base = 16;
	}

	if (!*p)
		return false;

	// Above the Unicode range there is nothing to name, and the cap keeps the
	// accumulation from overflowing.
	const uint32 maxValue = 0x10FFFF;
	uint32 n = 0;
	for (; *p; ++p) {
		int digit;
		if (*p >= '0' && *p <= '9')
			digit = *p - '0';
		else if (base == 16 && *p >= 'a' && *p <= 'f')
			digit = *p - 'a' + 10;
		else if (base == 16 && *p >= 'A' && *p <= 'F')
			digit = *p - 'A' + 10;
		else
			return false;

		if (n > (maxValue - (uint32)digit) / (uint32)base)
			return false;
		n = n * (uint32)base + (uint32)digit;
	}

	out = n;
	return true;
}

/**
 * Read one [glyphs] style section into a table.
 *
 * Qualified section names are tried before the bare one, exactly as getKey()
 * does for single keys, so a map may carry per-game exception lists.
 */
void readGlyphSection(const Common::INIFile &ini,
					  const Common::Array<Common::String> &qualifiers,
					  const char *section,
					  Common::HashMap<uint32, HiResGlyphOverride> &out) {
	// Least specific first, so a qualified entry overwrites the bare one.
	Common::Array<Common::String> names;
	names.push_back(section);
	for (int i = (int)qualifiers.size() - 1; i >= 0; --i) {
		if (!qualifiers[i].empty())
			names.push_back(Common::String::format("%s:%s", section, qualifiers[i].c_str()));
	}

	for (uint s = 0; s < names.size(); ++s) {
		if (!ini.hasSection(names[s]))
			continue;

		const Common::INIFile::SectionKeyList keys = ini.getKeys(names[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin();
			 it != keys.end(); ++it) {
			uint32 code;
			if (!parseCodeValue(it->key, code)) {
				warning("HiResText: '%s' is not a character code, ignoring",
						it->key.c_str());
				continue;
			}

			if (it->value.equalsIgnoreCase("keep")) {
				out[code] = HiResGlyphOverride(kHiResGlyphKeep, 0);
				continue;
			}

			uint32 target;
			if (parseCodeValue(it->value, target)) {
				out[code] = HiResGlyphOverride(kHiResGlyphRemap, target);
				continue;
			}

			warning("HiResText: glyph %s: '%s' is neither 'keep' nor a code point, ignoring",
					it->key.c_str(), it->value.c_str());
		}
	}
}

} // End of anonymous namespace

bool HiResFontMap::load(const Common::Path &mapPath,
						const Common::Array<Common::String> &qualifiers,
						HiResTextConfig &out,
						const Common::Array<Common::String> *scopes) {
	Common::FSNode mapNode(mapPath);
	Common::SeekableReadStream *stream = mapNode.createReadStream();
	if (!stream) {
		warning("HiResText: could not open font map '%s'", mapPath.toString().c_str());
		return false;
	}

	const bool ok = loadFromStream(*stream, mapPath.getParent(), qualifiers, out, scopes);
	delete stream;

	if (!ok)
		warning("HiResText: could not parse font map '%s'", mapPath.toString().c_str());

	return ok;
}

bool HiResFontMap::loadFromStream(Common::SeekableReadStream &stream,
								  const Common::Path &baseDir,
								  const Common::Array<Common::String> &qualifiers,
								  HiResTextConfig &out,
								  const Common::Array<Common::String> *scopes) {
	Common::INIFile ini;
	ini.requireKeyValueDelimiter();
	if (!ini.loadFromStream(stream) || stream.err())
		return false;

	Common::String value;

	// [hires] sets the geometry the font was drawn for, so a translation can
	// ship one map file and need no user configuration at all. The caller is
	// told the value came from the map (rather than from the user) so an
	// explicit setting can still win.
	//
	//   [hires]
	//   scale=2
	//   alpha=true
	if (getKey(ini, qualifiers, "hires", "scale", value)) {
		int scale;
		if (parseInteger(value, kMaxScale, scale) && scale >= 1) {
			out.scale = scale;
			out.scaleFromMap = true;
		} else {
			warning("HiResText: invalid scale '%s', ignoring", value.c_str());
		}
	}
	if (getKey(ini, qualifiers, "hires", "alpha", value)) {
		bool alpha;
		if (parseMapBool(value, alpha)) {
			out.alpha = alpha;
			out.alphaFromMap = true;
		} else {
			warning("HiResText: invalid alpha '%s', ignoring", value.c_str());
		}
	}

	// [encoding] names the code page the game's strings are in. Everything
	// past the engine adapter works on Unicode code points, so this is the
	// single place that decides how bytes become characters.
	//
	//   [encoding]
	//   codepage=cp932
	if (getKey(ini, qualifiers, "encoding", "codepage", value)) {
		const Common::CodePage page = parseCodePage(value);
		if (page != Common::kCodePageInvalid) {
			out.encoding = page;
			out.encodingFromMap = true;
		} else {
			warning("HiResText: unknown code page '%s', keeping the default", value.c_str());
		}
	}

	// [bitmap] names the replacement bitmap fonts. This is the primary path:
	// an 8bpp bitmap font carries the same anti-aliased shapes a TrueType
	// rasteriser would produce, but needs no FreeType at run time.
	//
	//   [bitmap]
	//   multi=korean%02d.fnt
	//   single=korean.fnt
	//   glyphs=2350
	if (getKey(ini, qualifiers, "bitmap", "multi", value))
		out.bitmapPattern = value;
	if (getKey(ini, qualifiers, "bitmap", "single", value))
		out.bitmapSingle = value;
	if (getKey(ini, qualifiers, "bitmap", "glyphs", value)) {
		int glyphs;
		if (parseInteger(value, 0x110000, glyphs) && glyphs > 0)
			out.bitmapGlyphs = glyphs;
		else
			warning("HiResText: bad glyph count '%s', keeping the default", value.c_str());
	}

	// [fonts] and [sizes] drive the TrueType path. They are parsed even in a
	// build without FreeType so that a bad map is reported the same way
	// everywhere; the loader simply ignores the paths it cannot use.
	//
	//   [fonts]
	//   default=NanumGothic.ttf
	//
	//   [sizes]
	//   title=32
	//   bold=16x2
	//   default=12pt
	//
	// The "NxM" form supersamples: a pixel font stays on its native grid while
	// still fitting a line box that is not a multiple of it. The "pt" suffix
	// is resolution independent, so one map looks the same at 2x and 3x.
	static const char *const roleNames[kHiResRoleCount] = { "default", "bold", "title" };
	for (int r = 0; r < kHiResRoleCount; ++r) {
		if (getKey(ini, qualifiers, "fonts", roleNames[r], value))
			out.ttfPath[r] = resolvePath(value, baseDir);

		if (!getKey(ini, qualifiers, "sizes", roleNames[r], value))
			continue;

		const char *p = value.c_str();
		int size = 0, super = 1;
		bool relative = false;
		bool valid = parseNumber(p, 4096, size) && size > 0;
		if (valid && *p == 'x') {
			++p;
			valid = parseNumber(p, 16, super) && super > 0;
		} else if (valid && strcmp(p, "pt") == 0) {
			relative = true;
			p += 2;
		}
		if (valid && *p == 0) {
			// Resolve logical sizes only AFTER the adapter applies user
			// overrides to the scale. Otherwise map scale=3 bakes in 36px
			// even when the user ultimately chooses scale=2.
			out.ttfSize[r] = size;
			out.ttfSizeRelative[r] = relative;
			out.ttfSupersample[r] = super;
		} else {
			warning("HiResText: invalid font size '%s', ignoring", value.c_str());
		}
	}

	// [render] mode=string draws whole runs at once instead of one glyph at a
	// time, letting the font place the characters within a line.
	if (getKey(ini, qualifiers, "render", "mode", value))
		out.ttfStringMode = value.equalsIgnoreCase("string");

	if (getKey(ini, qualifiers, "render", "metrics", value)) {
		if (value.equalsIgnoreCase("font"))
			out.metricsSource = kHiResMetricsFont;
		else if (value.equalsIgnoreCase("game"))
			out.metricsSource = kHiResMetricsGame;
		else
			warning("HiResText: unknown metrics '%s', ignoring", value.c_str());
	}

	// Legacy [latin] keys are retained as adapter data, not a byte-width
	// dispatch rule for the generic renderer. The original adapter routed
	// the single byte range through the replacement font,
	// so a line does not mix new glyphs with the game's original ones.
	//
	//   [latin]
	//   enabled=true
	//   bitmap=latin24.fnt
	//   metrics=font
	if (getKey(ini, qualifiers, "latin", "enabled", value)) {
		bool enabled;
		if (parseMapBool(value, enabled))
			out.legacy.latinEnabled = enabled;
		else
			warning("HiResText: invalid legacy enabled '%s', ignoring", value.c_str());
	}
	if (getKey(ini, qualifiers, "latin", "font", value))
		out.legacy.latinTtfPath = resolvePath(value, baseDir);
	if (getKey(ini, qualifiers, "latin", "bitmap", value)) {
		out.legacy.latinBitmapName = value;
		out.legacy.latinEnabled = true;
	}
	if (getKey(ini, qualifiers, "latin", "metrics", value)) {
		// Preserve the two independent legacy switches. "bitmap" never
		// enabled TTF metrics, nor did "ttf" enable bitmap metrics.
		if (value.equalsIgnoreCase("game") || value.equalsIgnoreCase("font") ||
			value.equalsIgnoreCase("ttf") || value.equalsIgnoreCase("bitmap")) {
			out.legacy.latinTtfMetrics = (value.equalsIgnoreCase("ttf") || value.equalsIgnoreCase("font")) ? kHiResMetricsFont : kHiResMetricsGame;
			out.legacy.latinBitmapMetrics = (value.equalsIgnoreCase("bitmap") || value.equalsIgnoreCase("font")) ? kHiResMetricsFont : kHiResMetricsGame;
		} else {
			warning("HiResText: unknown legacy metrics '%s', ignoring", value.c_str());
		}
	}

	// [shadow] forces an outline or drop shadow on the replacement glyphs.
	//
	//   [shadow]
	//   mode=outline
	//   offset=2
	//   color=0
	if (getKey(ini, qualifiers, "shadow", "mode", value)) {
		if (value.equalsIgnoreCase("none"))
			out.shadowMode = kHiResShadowNone;
		else if (value.equalsIgnoreCase("drop"))
			out.shadowMode = kHiResShadowDrop;
		else if (value.equalsIgnoreCase("outline"))
			out.shadowMode = kHiResShadowOutline;
		else if (value.equalsIgnoreCase("stroke"))
			out.shadowMode = kHiResShadowStroke;
		else
			out.shadowMode = kHiResShadowGame;
	}
	if (getKey(ini, qualifiers, "shadow", "offset", value)) {
		int offset;
		if (value == "-1")
			out.shadowOffset = -1;
		else if (parseInteger(value, 4096, offset))
			out.shadowOffset = offset;
		else
			warning("HiResText: invalid shadow offset '%s', ignoring", value.c_str());
	}
	if (getKey(ini, qualifiers, "shadow", "color", value)) {
		int color;
		if (parseInteger(value, 255, color)) {
			out.shadowColor = color;
			out.shadowColorSet = true;
		} else {
			warning("HiResText: invalid shadow color '%s', ignoring", value.c_str());
		}
	}

	// [translation] names the runtime translation bundle when it is not the
	// engine's default.
	if (getKey(ini, qualifiers, "translation", "file", value))
		out.translationName = value;

	// The height map is merged rather than overridden: the bare section
	// provides the defaults and the qualified ones refine individual heights.
	// Iteration therefore runs from the least specific qualifier to the most.
	Common::Array<Common::String> sections;
	sections.push_back("map");
	for (int i = (int)qualifiers.size() - 1; i >= 0; --i) {
		if (!qualifiers[i].empty())
			sections.push_back(Common::String::format("map:%s", qualifiers[i].c_str()));
	}

	for (uint s = 0; s < sections.size(); ++s) {
		if (!ini.hasSection(sections[s]))
			continue;

		const Common::INIFile::SectionKeyList keys = ini.getKeys(sections[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin();
			 it != keys.end(); ++it) {
			if (!it->key.hasPrefix("height_"))
				continue;
			int height;
			if (parseInteger(it->key.c_str() + 7, 65535, height) && height > 0)
				out.heightRoles[height] = parseRole(it->value);
		}
	}

	// [glyphs] lists character codes the game did not use for what the code
	// page says. Which slots are repurposed depends on the game's own font:
	// 0x5F is a left arrow in a dialogue charset and a plain underscore in
	// the others, so the common table is refined per scope rather than
	// merged into one.
	//
	//   [glyphs]
	//   0x5e = keep        ; an ellipsis, not a caret
	//   [glyphs:cs1]
	//   0x5f = keep        ; left arrow in this charset only
	//   0x7f = u+2192      ; drawn from the replacement at another code point
	readGlyphSection(ini, qualifiers, "glyphs", out.glyphOverrides);

	if (scopes) {
		out.scopedGlyphOverrides.resize(scopes->size());
		for (uint i = 0; i < scopes->size(); ++i) {
			if ((*scopes)[i].empty())
				continue;
			const Common::String section =
				Common::String::format("glyphs:%s", (*scopes)[i].c_str());
			readGlyphSection(ini, qualifiers, section.c_str(),
							 out.scopedGlyphOverrides[i]);
		}
	}

	return true;
}

bool hiResCellsFitScale(const int *cells, int count,
						int gameCellHeight, int scale) {
	if (!cells || count <= 0 || gameCellHeight <= 0 || scale <= 0)
		return false;

	for (int i = 0; i < count; ++i) {
		if (cells[i] == gameCellHeight * scale)
			return true;
	}
	return false;
}

} // End of namespace Graphics
