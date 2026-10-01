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

#include "common/config-manager.h"
#include "common/formats/ini-file.h"
#include "common/archive.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace Graphics {

// The maximum the text surface multiplier is allowed to reach. Three is not a
// technical limit but a practical one: at 3x a 320x200 game already needs a
// 960x600 text surface. Raising this policy limit needs adapter memory tests.
static const int kMaxScale = 3;
// The widest outline a map may ask for, in quarter pixels (8 px), and the
// furthest a shadow may be moved. Both bound a per-glyph scratch buffer.
static const int kMaxShadowWidthQ = 32;
static const int kMaxShadowShift = 16;
// [render] gamma= in hundredths: 0.5 (lighter) to 4 (heavier); 1 is off.
static const int kMinCoverageGamma = 50;
static const int kMaxCoverageGamma = 400;

HiResLayoutSettings::HiResLayoutSettings()
	: hangul(kHangulBreakWord), hangulSet(false), kinsoku(true), kinsokuSet(false),
	  thai(true), thaiSet(false) {
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

const char *const HiResFontMap::kDataPrefix = "data:";

bool HiResFontMap::isDataPath(const Common::String &value) {
	return value.hasPrefix(kDataPrefix);
}

bool HiResFontMap::isSafeDataRelative(const Common::String &relative) {
	if (relative.empty())
		return false;
	const char first = relative[0];
	if (first == '/' || first == '\\' || (relative.size() > 1 && relative[1] == ':'))
		return false;
	// No ".." component, with either separator.
	size_t start = 0;
	while (start <= relative.size()) {
		size_t end = start;
		while (end < relative.size() && relative[end] != '/' && relative[end] != '\\')
			++end;
		if (end - start == 2 && relative[start] == '.' && relative[start + 1] == '.')
			return false;
		start = end + 1;
	}
	return true;
}

Common::Path HiResFontMap::resolveDataPath(const Common::String &relative,
										   const Common::Array<Common::Path> &roots,
										   FontFileExistsFn exists) {
	if (isSafeDataRelative(relative)) {
		const Common::Path rel(relative);
		for (uint i = 0; i < roots.size(); ++i) {
			if (roots[i].empty())
				continue;
			const Common::Path candidate = roots[i].join(rel);
			Common::Path file;
			int32 faceIndex;
			if (resolveFontFace(candidate, file, faceIndex, exists))
				return candidate;
		}
	}
	return Common::Path(Common::String(kDataPrefix) + relative);
}

namespace {

/// Add @p path to @p roots unless it is empty or already there.
void addUniqueRoot(Common::Array<Common::Path> &roots, const Common::Path &path) {
	if (path.empty())
		return;
	for (uint i = 0; i < roots.size(); ++i) {
		if (roots[i] == path)
			return;
	}
	roots.push_back(path);
}

/// Add @p domain's own extrapath to @p roots, once.
void addExtraPathRoot(Common::Array<Common::Path> &roots, const Common::String &domain) {
	if (domain.empty())
		return;
	const Common::ConfigManager::Domain *dom = ConfMan.getDomain(domain);
	if (!dom || !dom->contains("extrapath"))
		return;
	addUniqueRoot(roots, Common::Path::fromConfig(dom->getVal("extrapath")));
}

} // End of anonymous namespace

Common::Array<Common::Path> HiResFontMap::dataRoots() {
	Common::Array<Common::Path> roots;
	addExtraPathRoot(roots, Common::ConfigManager::kTransientDomain);   // --extrapath
	addExtraPathRoot(roots, ConfMan.getActiveDomainName());             // the game's
	addExtraPathRoot(roots, Common::ConfigManager::kApplicationDomain); // global
	addExtraPathRoot(roots, Common::ConfigManager::kSessionDomain);     // in-tree default
#ifdef DATA_PATH
	roots.push_back(Common::Path(DATA_PATH, Common::Path::kNativeSeparator));
#endif
	const Common::Array<Common::Path> searched = searchSetRoots(SearchMan);
	for (uint i = 0; i < searched.size(); ++i)
		addUniqueRoot(roots, searched[i]);
	return roots;
}

Common::Array<Common::Path> HiResFontMap::searchSetRoots(const Common::SearchSet &set) {
	// SearchSet does not hand out its archives, but every member it lists
	// carries the name of the archive it came from, in priority order.
	// Only the folder at an archive's root matters, not its search depth:
	// the "." archive a Windows build finds its data in is one level deep,
	// while a data: path is three or four levels down from it.
	Common::Array<Common::Path> roots;
	Common::ArchiveMemberDetailsList members;
	set.listMatchingMembers(members, Common::Path("*"));
	Common::Array<Common::String> seen;
	for (Common::ArchiveMemberDetailsList::const_iterator it = members.begin(); it != members.end(); ++it) {
		bool known = false;
		for (uint i = 0; i < seen.size() && !known; ++i)
			known = seen[i] == it->arcName;
		if (known)
			continue;
		seen.push_back(it->arcName);
		const Common::FSDirectory *dir =
			dynamic_cast<const Common::FSDirectory *>(set.getArchive(it->arcName));
		if (!dir)
			continue; // a zip, the Win32 resources, ...: no folder to look in
		const Common::FSNode node = dir->getFSNode();
		if (node.isDirectory())
			addUniqueRoot(roots, node.getPath());
	}
	return roots;
}

Common::Path HiResFontMap::resolvePath(const Common::String &value, const Common::Path &baseDir) {
	if (value.empty())
		return Common::Path();

	if (isDataPath(value)) {
		const Common::String relative = value.substr(strlen(kDataPrefix));
		if (!isSafeDataRelative(relative)) {
			warning("HiResText: '%s' is refused: a data: path must be relative and must not contain '..'",
					value.c_str());
			return Common::Path(value);
		}
		const Common::Path found = resolveDataPath(relative, dataRoots());
		if (isDataPath(found.toString('/')))
			warning("HiResText: '%s' is not in the extrapath or any ScummVM data folder", value.c_str());
		return found;
	}

	const char first = value[0];
	const bool absolute = (first == '/' || first == '\\') ||
						  (value.size() > 2 && value[1] == ':');
	if (absolute)
		return Common::Path(value);

	return baseDir.join(Common::Path(value));
}

namespace {

/**
 * Drop a trailing comment from a value.
 *
 * The first ';' preceded by a space or a tab ends the value. INIFile has
 * already trimmed the value, so "key= ; note" arrives as "; note", which is
 * a comment too. A ';' with anything else before it is part of the value
 * ("ko=my;font.ttf").
 */
Common::String stripInlineComment(const Common::String &raw) {
	Common::String value(raw);
	for (uint i = 0; i < raw.size(); ++i) {
		if (raw[i] == ';' && (i == 0 || raw[i - 1] == ' ' || raw[i - 1] == '\t')) {
			value = Common::String(raw.c_str(), i);
			break;
		}
	}
	value.trim();
	return value;
}

/// Read a key, trying each qualified section name before the bare one.
bool getKey(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
			const char *section, const char *key, Common::String &value) {
	bool found = false;
	for (uint i = 0; i < qualifiers.size() && !found; ++i) {
		if (qualifiers[i].empty())
			continue;
		const Common::String qualified =
			Common::String::format("%s:%s", section, qualifiers[i].c_str());
		found = ini.getKey(key, qualified, value);
	}
	if (!found)
		found = ini.getKey(key, section, value);
	if (found)
		value = stripInlineComment(value);
	return found;
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

/// The N of a [font.N] / [glyphs.N] section: plain decimal, 0..65535, written
/// the one way ("4", not "04"), so the section can be read back by its
/// canonical name.
bool parseSectionId(const Common::String &text, int &out) {
	int id;
	if (!parseInteger(text, 65535, id) || text != Common::String::format("%d", id))
		return false;
	out = id;
	return true;
}

bool parseAlign(const Common::String &value, HiResAlign &out) {
	if (value.equalsIgnoreCase("game"))
		out = kHiResAlignGame;
	else if (value.equalsIgnoreCase("cell"))
		out = kHiResAlignCell;
	else if (value.equalsIgnoreCase("font"))
		out = kHiResAlignFont;
	else
		return false;
	return true;
}

bool parseCellMode(const Common::String &value, HiResCellMode &out) {
	if (value.equalsIgnoreCase("game"))
		out = kHiResCellGame;
	else if (value.equalsIgnoreCase("glyph"))
		out = kHiResCellGlyph;
	else
		return false;
	return true;
}

/// mirror= (design 6.2.1): only off|horizontal|vertical|both; no alias.
bool parseMirror(const Common::String &value, HiResMirror &out) {
	if (value.equalsIgnoreCase("off"))
		out = kHiResMirrorNone;
	else if (value.equalsIgnoreCase("horizontal"))
		out = kHiResMirrorHorizontal;
	else if (value.equalsIgnoreCase("vertical"))
		out = kHiResMirrorVertical;
	else if (value.equalsIgnoreCase("both"))
		out = kHiResMirrorBoth;
	else
		return false;
	return true;
}

/**
 * A decimal in units of 1/unit, to the nearest one: with unit 4, "1.5"
 * gives 6; with unit 100, "2.2" gives 220. Signs and exponents are junk.
 */
bool parseFixedPoint(const Common::String &value, int unit, int maxUnits, int &out) {
	const char *p = value.c_str();
	int whole = 0;
	if (!parseNumber(p, 1000, whole))
		return false;
	int frac = 0, scaleDiv = 1;
	if (*p == '.') {
		++p;
		if (*p < '0' || *p > '9')
			return false;
		while (*p >= '0' && *p <= '9') {
			if (scaleDiv < 10000) {
				frac = frac * 10 + (*p - '0');
				scaleDiv *= 10;
			}
			++p;
		}
	}
	if (*p)
		return false;
	const int v = whole * unit + (frac * unit * 2 + scaleDiv) / (2 * scaleDiv);
	if (v > maxUnits)
		return false;
	out = v;
	return true;
}

/**
 * A length in output pixels, to the nearest quarter: "1", "1.5", "0.75".
 * The result is in quarters, so "1.5" gives 6.
 */
bool parseQuarterPixels(const Common::String &value, int maxQuarters, int &out) {
	return parseFixedPoint(value, 4, maxQuarters, out);
}

/// A decimal to the nearest hundredth: "2.2" gives 220, "1" gives 100.
bool parseHundredths(const Common::String &value, int maxHundredths, int &out) {
	return parseFixedPoint(value, 100, maxHundredths, out);
}

/// "dx,dy" with optional signs and spaces: "-1,1", "2, 2".
bool parseSignedPair(const Common::String &value, int maxAbs, int &a, int &b) {
	const char *p = value.c_str();
	int v[2];
	for (int i = 0; i < 2; ++i) {
		while (*p == ' ')
			++p;
		bool neg = false;
		if (*p == '-' || *p == '+') {
			neg = (*p == '-');
			++p;
		}
		int n;
		if (!parseNumber(p, maxAbs, n))
			return false;
		v[i] = neg ? -n : n;
		while (*p == ' ')
			++p;
		if (i == 0) {
			if (*p != ',')
				return false;
			++p;
		}
	}
	if (*p)
		return false;
	a = v[0];
	b = v[1];
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
 * Split a [glyphs] range key, "<code>-<code>", at its first '-'.
 *
 * Each half is trimmed ("0x21 - 0x7E") and read by parseCodeValue(). An empty
 * half or a second '-' makes it no range at all.
 */
bool parseGlyphRange(const Common::String &key, uint32 &start, uint32 &end) {
	const size_t dash = key.findFirstOf('-');
	if (dash == Common::String::npos)
		return false;
	Common::String first(key.c_str(), dash);
	Common::String second(key.c_str() + dash + 1);
	if (second.findFirstOf('-') != Common::String::npos)
		return false;
	first.trim();
	second.trim();
	return parseCodeValue(first, start) && parseCodeValue(second, end);
}

// A face size in pixels, for size= and pixel=; the caller applies the
// key's own range.
bool parseFaceSize(const Common::String &value, int &out) {
	int size;
	if (!parseInteger(value, 4096, size) || size <= 0)
		return false;
	out = size;
	return true;
}

bool qualifierListed(const Common::Array<Common::String> &qualifiers, const Common::String &q) {
	for (uint i = 0; i < qualifiers.size(); ++i) {
		if (!qualifiers[i].empty() && qualifiers[i].equalsIgnoreCase(q))
			return true;
	}
	return false;
}

bool parseOnOff(const Common::String &value, bool &out) {
	if (value.equalsIgnoreCase("on") || value.equalsIgnoreCase("true"))
		out = true;
	else if (value.equalsIgnoreCase("off") || value.equalsIgnoreCase("false"))
		out = false;
	else
		return false;
	return true;
}

} // End of anonymous namespace

// =====================================================================
// The version-2 map loader (design
// docs/superpowers/specs/2026-09-30-hires-config-unify-design.md).
// =====================================================================

HiResFontScope::HiResFontScope()
	: faceSet(false), size(0), sizeSet(false), pixel(0), pixelSet(false), shift(0), shiftSet(false),
	  cell(kHiResCellGame), cellSet(false), align(kHiResAlignGame), alignSet(false),
	  missing(0), missingSet(false), advance(kHiResAdvanceEngine), advanceSet(false),
	  origin(kHiResOriginGame), originSet(false), mirror(kHiResMirrorNone), mirrorSet(false) {
}

HiResMap::HiResMap() {
	clear();
}

void HiResMap::clear() {
	version = 0;

	target = kHiResTargetAuto;
	targetSet = false;
	blend = kHiResBlendAuto;
	blendSet = false;
	scale = 0;
	scaleSet = false;
	coverageGamma = 100;

	encoding = Common::kCodePageInvalid;
	encodingSet = false;

	layout = HiResLayoutSettings();

	faces.clear();
	font = HiResFontScope();
	fontIds.clear();

	glyphs.clear();
	glyphIds.clear();

	shadowMode = kHiResShadowGame;
	shadowOffset = -1;
	shadowColor = 0;
	shadowColorSet = false;
	shadowWidthQ = -1;
	shadowStyle = kHiResOutlineRound;
	shadowShiftSet = false;
	shadowDx = 0;
	shadowDy = 0;
	shadowShiftColor = 0;
	shadowShiftColorSet = false;
	shadowAlpha = 255;

	warnings.clear();
	loadedFor = kHiResTargetAuto;
	quietLoad = false;
}

HiResMapLoadOptions::HiResMapLoadOptions() : target(kHiResTargetAuto), quiet(false) {
}

const HiResFontScope *HiResMap::fontIdScope(int id) const {
	Common::HashMap<int, HiResFontScope>::const_iterator it = fontIds.find(id);
	if (it == fontIds.end())
		return nullptr;
	return &it->_value;
}

// design section 3.2's "Read by" columns, decoded to HiResKeyFlag bits.
const HiResEngineKeys kHiResKeysSci = {
	"SCI",
	kHiResKeyTarget | kHiResKeyBlend | kHiResKeyScale | kHiResKeyGamma | kHiResKeyLayout |
	kHiResKeyShift | kHiResKeyCell | kHiResKeyAlign | kHiResKeyMissing | kHiResKeyAdvance |
	kHiResKeyRange | kHiResKeyGlyphs
};

const HiResEngineKeys kHiResKeysScumm = {
	"SCUMM",
	kHiResKeyTarget | kHiResKeyBlend | kHiResKeyScale | kHiResKeyGamma | kHiResKeyTextEncoding |
	kHiResKeyLayout | kHiResKeyMissing | kHiResKeyAdvance | kHiResKeyOrigin | kHiResKeyRange |
	kHiResKeyMirror | kHiResKeyGlyphs | kHiResKeyShadow
};

const HiResEngineKeys kHiResKeysAgs = {
	"AGS",
	kHiResKeyBlend | kHiResKeyScale | kHiResKeyGamma | kHiResKeyLayout
};

namespace {

// The default map's own name, for every version-2 warning text (design
// section 10): the file is looked up as "HIRESTXT.MAP" (8.3, DOS), matched
// case-insensitively; the ini key that names it, hires_text_map, is
// unaffected.
const char *const kHiResMapName = "HIRESTXT.MAP";

// The old sections scanned, in this fixed order, for the "(found [x])" part
// of the version-gate warning (design 10.1): only these four, not [sizes] or
// [translation].
const char *const kHiResOldSections[] = { "hires", "latin", "bitmap", "encoding" };

void hiResWarn(HiResMap &out, const Common::String &message) {
	if (!out.quietLoad)
		warning("%s", message.c_str());
	out.warnings.push_back(message);
}

// ---- Section classification (design 3.2, 3.3) ----------------------------

enum HiResSectionFamily {
	kHSecUnknown = 0,
	kHSecRemoved,
	kHSecOldGlyphsCs, ///< [glyphs:csN]: the removed old per-charset form (design 3.3)
	kHSecReserved,   ///< [translation.<lang>]: a later plan's, skipped whole
	kHSecMap,
	kHSecRender,
	kHSecText,
	kHSecLayout,
	kHSecFonts,
	kHSecFont,
	kHSecFontId,
	kHSecGlyphs,
	kHSecGlyphsId,
	kHSecShadow
};

struct HiResSectionInfo {
	HiResSectionFamily family;
	int id;                    ///< for kHSecFontId/kHSecGlyphsId/kHSecOldGlyphsCs
	Common::String base;       ///< section name before ':' ("font.4")
	Common::String qualifier;  ///< after the first ':' (may itself contain ':'); empty if bare
	Common::Array<Common::String> qualifierParts; ///< @ref qualifier split at every ':' (design 3.4)
};

/// @ref HiResSectionInfo::qualifier split at every ':' (design 3.4: "[S]",
/// "[S:e]", "[S:t]", "[S:e:t]" - the engine qualifier first, the target last).
Common::Array<Common::String> splitQualifierParts(const Common::String &qualifier) {
	Common::Array<Common::String> parts;
	if (qualifier.empty())
		return parts;
	size_t start = 0;
	for (;;) {
		const size_t colon = qualifier.findFirstOf(':', start);
		if (colon == Common::String::npos) {
			parts.push_back(Common::String(qualifier.c_str() + start));
			break;
		}
		parts.push_back(Common::String(qualifier.c_str() + start, (uint32)(colon - start)));
		start = colon + 1;
	}
	return parts;
}

/// The old per-charset qualifier convention [glyphs:csN] (design 3.3: removed,
/// replaced by [glyphs.N]): "cs" followed by one or more digits, case-insensitive.
/// Looks at the first qualifier only (design 3.4's "[S:e:t]" form is never this).
bool isOldGlyphsCsQualifier(const Common::String &qualifier, int &id) {
	if (!qualifier.hasPrefixIgnoreCase("cs") || qualifier.size() <= 2)
		return false;
	return parseInteger(Common::String(qualifier.c_str() + 2), 65535, id);
}

void classifyHiResSection(const Common::String &name, HiResSectionInfo &info) {
	const size_t colon = name.findFirstOf(':');
	if (colon == Common::String::npos) {
		info.base = name;
		info.qualifier.clear();
	} else {
		info.base = Common::String(name.c_str(), colon);
		info.qualifier = Common::String(name.c_str() + colon + 1);
	}
	info.qualifierParts = splitQualifierParts(info.qualifier);
	info.id = -1;

	if (info.base.equalsIgnoreCase("map")) { info.family = kHSecMap; return; }
	if (info.base.equalsIgnoreCase("render")) { info.family = kHSecRender; return; }
	if (info.base.equalsIgnoreCase("text")) { info.family = kHSecText; return; }
	if (info.base.equalsIgnoreCase("layout")) { info.family = kHSecLayout; return; }
	if (info.base.equalsIgnoreCase("fonts")) { info.family = kHSecFonts; return; }
	if (info.base.equalsIgnoreCase("font")) { info.family = kHSecFont; return; }
	if (info.base.equalsIgnoreCase("glyphs")) {
		int csId;
		if (!info.qualifierParts.empty() && isOldGlyphsCsQualifier(info.qualifierParts[0], csId)) {
			info.family = kHSecOldGlyphsCs;
			info.id = csId;
			return;
		}
		info.family = kHSecGlyphs;
		return;
	}
	if (info.base.equalsIgnoreCase("shadow")) { info.family = kHSecShadow; return; }
	// Reserved for later use: [translation.<lang>], lang an
	// ISO 639-1 code. The bare [translation] below is still the removed
	// section (design 3.3); only the dotted form is reserved.
	if (info.base.hasPrefixIgnoreCase("translation.") && info.base.size() > 12) {
		info.family = kHSecReserved;
		return;
	}
	if (info.base.equalsIgnoreCase("hires") || info.base.equalsIgnoreCase("latin") ||
		info.base.equalsIgnoreCase("bitmap") || info.base.equalsIgnoreCase("encoding") ||
		info.base.equalsIgnoreCase("sizes") || info.base.equalsIgnoreCase("translation")) {
		info.family = kHSecRemoved;
		return;
	}
	if (info.base.hasPrefixIgnoreCase("font.")) {
		int id;
		if (parseSectionId(Common::String(info.base.c_str() + 5), id)) {
			info.family = kHSecFontId;
			info.id = id;
			return;
		}
		info.family = kHSecUnknown;
		return;
	}
	if (info.base.hasPrefixIgnoreCase("glyphs.")) {
		int id;
		if (parseSectionId(Common::String(info.base.c_str() + 7), id)) {
			info.family = kHSecGlyphsId;
			info.id = id;
			return;
		}
		info.family = kHSecUnknown;
		return;
	}
	info.family = kHSecUnknown;
}

/// The "not read any more" pointer for one removed section (design 3.3):
/// [latin]'s wording is pinned by a test; the others follow the same style.
const char *hiResRemovedSectionPointer(const Common::String &base) {
	if (base.equalsIgnoreCase("latin"))
		return "see \"Ranges\" in graphics/hires_text/README.md";
	return "see \"The map file\" in graphics/hires_text/README.md";
}

/// Known keys of [render]/[text]/[layout]/[shadow] and the HiResKeyFlag each
/// needs (design 3.2); 0 means every engine reads it (no "does not use").
bool classifyRenderKey(const Common::String &key, uint32 &flag) {
	static const struct { const char *key; uint32 flag; } kKeys[] = {
		{ "target", kHiResKeyTarget }, { "blend", kHiResKeyBlend },
		{ "scale", kHiResKeyScale }, { "gamma", kHiResKeyGamma }
	};
	for (uint i = 0; i < ARRAYSIZE(kKeys); ++i) {
		if (key.equalsIgnoreCase(kKeys[i].key)) {
			flag = kKeys[i].flag;
			return true;
		}
	}
	return false;
}

/// Known keys of [font]/[font.N], including the dynamic range./advance./
/// origin.<spec> families.
bool classifyFontKey(const Common::String &key, uint32 &flag) {
	static const struct { const char *key; uint32 flag; } kExact[] = {
		{ "face", 0 }, { "size", 0 }, { "pixel", 0 },
		{ "shift", kHiResKeyShift }, { "cell", kHiResKeyCell }, { "align", kHiResKeyAlign },
		{ "missing", kHiResKeyMissing }, { "advance", kHiResKeyAdvance }, { "origin", kHiResKeyOrigin },
		{ "mirror", kHiResKeyMirror }
	};
	for (uint i = 0; i < ARRAYSIZE(kExact); ++i) {
		if (key.equalsIgnoreCase(kExact[i].key)) {
			flag = kExact[i].flag;
			return true;
		}
	}
	if (key.hasPrefixIgnoreCase("range.") && key.size() > 6) { flag = kHiResKeyRange; return true; }
	if (key.hasPrefixIgnoreCase("advance.") && key.size() > 8) { flag = kHiResKeyAdvance; return true; }
	if (key.hasPrefixIgnoreCase("origin.") && key.size() > 7) { flag = kHiResKeyOrigin; return true; }
	return false;
}

bool classifyShadowKey(const Common::String &key, uint32 &flag) {
	static const char *const kKeys[] = {
		"mode", "offset", "color", "width", "style", "shadow", "shadow_color", "shadow_alpha"
	};
	for (uint i = 0; i < ARRAYSIZE(kKeys); ++i) {
		if (key.equalsIgnoreCase(kKeys[i])) {
			flag = kHiResKeyShadow;
			return true;
		}
	}
	return false;
}

/**
 * Design section 3.4's qualifier syntax check, run for every section (in
 * this fixed order, first match only): three or more qualifiers; `auto` as
 * either qualifier; a target first with a second qualifier after it; a
 * second qualifier that is not a target; a target qualifier on
 * [text]/[layout]. Returns the empty string for a well-formed section (a
 * plain engine qualifier, or the valid `[S:e:t]`/`[S:t]` forms); otherwise
 * the full warning text (design 10.2) - the section is then skipped
 * entirely (it can never match a generated qualifier candidate anyway).
 */
Common::String hiResQualifierProblem(const HiResSectionInfo &info, const Common::String &sectionName) {
	const Common::Array<Common::String> &p = info.qualifierParts;
	Common::String reason;

	if (p.size() >= 3) {
		reason = "a section takes at most two qualifiers";
	} else {
		bool sawAuto = false;
		for (uint i = 0; i < p.size() && !sawAuto; ++i)
			sawAuto = p[i].equalsIgnoreCase("auto");
		if (sawAuto) {
			reason = "auto is not a render-target qualifier";
		} else if (p.size() == 2) {
			HiResRenderTarget t;
			const bool firstIsTarget = parseRenderTargetQualifier(p[0], t);
			const bool secondIsTarget = parseRenderTargetQualifier(p[1], t);
			if (firstIsTarget) {
				reason = Common::String::format("the render target goes last ([%s:%s:%s])",
												 info.base.c_str(), p[1].c_str(), p[0].c_str());
			} else if (!secondIsTarget) {
				reason = "the second qualifier must be clut8, rgb565 or rgb888";
			}
		} else if (p.size() == 1 && (info.family == kHSecText || info.family == kHSecLayout)) {
			HiResRenderTarget t;
			if (parseRenderTargetQualifier(p[0], t))
				reason = Common::String::format("[%s] cannot depend on the render target", info.base.c_str());
		}
	}

	if (reason.empty())
		return Common::String();
	return Common::String::format("%s: [%s]: %s; ignoring the section", kHiResMapName, sectionName.c_str(), reason.c_str());
}

/// Whether a render-qualified section's target is fixed by its own
/// qualifiers - the last qualifier ([S:t]) or second ([S:e:t]) is a render
/// target - so a `target` key inside it would be circular (design 3.4's
/// "[render:t] target").
bool hiResSectionEndsInTarget(const HiResSectionInfo &info) {
	HiResRenderTarget t;
	if (info.qualifierParts.size() == 1)
		return parseRenderTargetQualifier(info.qualifierParts[0], t);
	if (info.qualifierParts.size() == 2)
		return parseRenderTargetQualifier(info.qualifierParts[1], t);
	return false;
}

/**
 * Pass 1 (design 10.2/10.3): walk every physical section and key once,
 * warning about unknown sections/keys, removed sections, and keys the
 * engine does not honour. Values themselves are parsed separately (pass 2,
 * buildHiResMap()); this pass only ever appends to @ref HiResMap::warnings.
 *
 * The "<engine> does not use" warning fires only for a bare
 * section or one whose qualifier is in @p qualifiers; unknown-section,
 * unknown-key and removed-section warnings fire for every section
 * regardless of qualifier. @p qualifiers is the render-target-expanded list
 * (qualifiersForTarget()): qualifierRelevant accepts a
 * section whose qualifier is in it.
 */
void scanHiResSections(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
						const HiResEngineKeys &engine, HiResMap &out) {
	const Common::INIFile::SectionList sections = ini.getSections();
	for (Common::INIFile::SectionList::const_iterator sec = sections.begin(); sec != sections.end(); ++sec) {
		HiResSectionInfo info;
		classifyHiResSection(sec->name, info);

		if (info.family == kHSecReserved)
			continue; // [translation.<lang>]: a later plan's; skip silently
		if (info.family == kHSecUnknown) {
			hiResWarn(out, Common::String::format("%s: unknown section [%s]", kHiResMapName, sec->name.c_str()));
			continue;
		}
		if (info.family == kHSecRemoved) {
			hiResWarn(out, Common::String::format("%s: [%s] is not read any more; %s",
													kHiResMapName, sec->name.c_str(), hiResRemovedSectionPointer(info.base)));
			continue;
		}
		if (info.family == kHSecOldGlyphsCs) {
			hiResWarn(out, Common::String::format("%s: [%s] is not read any more; use [glyphs.%d] instead",
													kHiResMapName, sec->name.c_str(), info.id));
			continue;
		}

		const Common::String qualifierProblem = hiResQualifierProblem(info, sec->name);
		if (!qualifierProblem.empty()) {
			hiResWarn(out, qualifierProblem);
			continue;
		}

		if (info.family == kHSecFonts)
			continue; // a name table: buildFaceNames() warns about a bad name itself

		const bool qualifierRelevant = info.qualifier.empty() || qualifierListed(qualifiers, info.qualifier);

		const Common::INIFile::SectionKeyList keys = sec->getKeys();
		for (Common::INIFile::SectionKeyList::const_iterator k = keys.begin(); k != keys.end(); ++k) {
			uint32 flag = 0;
			bool known = false;
			switch (info.family) {
			case kHSecMap:
				known = k->key.equalsIgnoreCase("version");
				break;
			case kHSecRender:
				known = classifyRenderKey(k->key, flag);
				break;
			case kHSecText:
				known = k->key.equalsIgnoreCase("encoding");
				flag = kHiResKeyTextEncoding;
				break;
			case kHSecLayout:
				known = k->key.equalsIgnoreCase("hangul") || k->key.equalsIgnoreCase("kinsoku") ||
						k->key.equalsIgnoreCase("thai");
				flag = kHiResKeyLayout;
				break;
			case kHSecFont:
			case kHSecFontId:
				known = classifyFontKey(k->key, flag);
				break;
			case kHSecGlyphs:
			case kHSecGlyphsId:
				known = true;
				flag = kHiResKeyGlyphs;
				break;
			case kHSecShadow:
				known = classifyShadowKey(k->key, flag);
				break;
			default:
				break;
			}

			if (!known) {
				hiResWarn(out, Common::String::format("%s: unknown key [%s] %s",
														kHiResMapName, sec->name.c_str(), k->key.c_str()));
				continue;
			}
			// design 3.4: [render:t]/[render:e:t] target is circular - the
			// target is chosen before target-qualified sections are read.
			if (info.family == kHSecRender && k->key.equalsIgnoreCase("target") && hiResSectionEndsInTarget(info)) {
				hiResWarn(out, Common::String::format("%s: [%s] %s cannot depend on the render target; ignoring it",
														kHiResMapName, sec->name.c_str(), k->key.c_str()));
				continue;
			}
			if (flag && qualifierRelevant && !(engine.honoured & flag)) {
				hiResWarn(out, Common::String::format("%s does not use [%s] %s",
														engine.engine, sec->name.c_str(), k->key.c_str()));
			}
		}
	}
}

// ---- Pass 2: building the merged values -----------------------------------

/// [fonts]: the name table, isValidFaceName()-filtered (design 3.2), least
/// specific merged first so a qualified entry refines a bare one.
void collectFaceNames(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers, HiResMap &out) {
	Common::Array<Common::String> names;
	names.push_back("fonts");
	for (int i = (int)qualifiers.size() - 1; i >= 0; --i) {
		if (!qualifiers[i].empty())
			names.push_back(Common::String::format("fonts:%s", qualifiers[i].c_str()));
	}

	for (uint s = 0; s < names.size(); ++s) {
		if (!ini.hasSection(names[s]))
			continue;
		const Common::INIFile::SectionKeyList keys = ini.getKeys(names[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin(); it != keys.end(); ++it) {
			if (!isValidFaceName(it->key)) {
				hiResWarn(out, Common::String::format("%s: [%s] '%s' is not a valid font name; ignoring it",
														kHiResMapName, names[s].c_str(), it->key.c_str()));
				continue;
			}
			out.faces[it->key] = stripInlineComment(it->value);
		}
	}
}

/**
 * design 3.4: [render] target= is read with @p engineQualifiers only (never
 * a target-qualified section - the target is chosen before those are read);
 * blend=/scale=/gamma= are read with @p qualifiers, the render-target-expanded
 * list, so they may hold a `:clut8` variant.
 */
void buildRenderSection(const Common::INIFile &ini, const Common::Array<Common::String> &engineQualifiers,
						 const Common::Array<Common::String> &qualifiers, HiResMap &out) {
	Common::String value;
	if (getKey(ini, engineQualifiers, "render", "target", value)) {
		HiResRenderTarget t;
		if (parseRenderTarget(value, t)) {
			out.target = t;
			out.targetSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [render] target '%s' is not auto, clut8, rgb565 or rgb888; ignoring it", kHiResMapName, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, "render", "blend", value)) {
		HiResBlend b;
		if (parseBlend(value, b)) {
			out.blend = b;
			out.blendSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [render] blend '%s' is not auto, on or off; ignoring it", kHiResMapName, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, "render", "scale", value)) {
		int scale;
		if (parseInteger(value, kMaxScale, scale) && scale >= 1) {
			out.scale = scale;
			out.scaleSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [render] scale '%s' is not 1, 2 or 3; ignoring it", kHiResMapName, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, "render", "gamma", value)) {
		int gamma;
		if (parseHundredths(value, kMaxCoverageGamma, gamma) && gamma >= kMinCoverageGamma) {
			out.coverageGamma = gamma;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [render] gamma '%s' is not 0.5..4.0; ignoring it", kHiResMapName, value.c_str()));
		}
	}
}

/// design 3.4: [text] is never target-qualifiable, so it is read with
/// @p engineQualifiers only (a target-qualified [text:t] is warned about and
/// skipped by hiResQualifierProblem() before this is reached).
void buildTextSection(const Common::INIFile &ini, const Common::Array<Common::String> &engineQualifiers, HiResMap &out) {
	Common::String value;
	if (getKey(ini, engineQualifiers, "text", "encoding", value)) {
		const Common::CodePage page = HiResFontMap::parseCodePage(value);
		if (page != Common::kCodePageInvalid) {
			out.encoding = page;
			out.encodingSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [text] encoding '%s' is not known; ignoring it", kHiResMapName, value.c_str()));
		}
	}
}

/// design 3.4: [layout] is never target-qualifiable; read with
/// @p engineQualifiers only, as [text] above.
void buildLayoutSectionV2(const Common::INIFile &ini, const Common::Array<Common::String> &engineQualifiers, HiResMap &out) {
	Common::String value;
	if (getKey(ini, engineQualifiers, "layout", "hangul", value)) {
		if (value.equalsIgnoreCase("word")) {
			out.layout.hangul = kHangulBreakWord;
			out.layout.hangulSet = true;
		} else if (value.equalsIgnoreCase("any")) {
			out.layout.hangul = kHangulBreakAny;
			out.layout.hangulSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [layout] hangul '%s' is not word or any; ignoring it", kHiResMapName, value.c_str()));
		}
	}
	if (getKey(ini, engineQualifiers, "layout", "kinsoku", value)) {
		if (parseOnOff(value, out.layout.kinsoku))
			out.layout.kinsokuSet = true;
		else
			hiResWarn(out, Common::String::format(
				"%s: [layout] kinsoku '%s' is not on or off; ignoring it", kHiResMapName, value.c_str()));
	}
	if (getKey(ini, engineQualifiers, "layout", "thai", value)) {
		if (parseOnOff(value, out.layout.thai))
			out.layout.thaiSet = true;
		else
			hiResWarn(out, Common::String::format(
				"%s: [layout] thai '%s' is not on or off; ignoring it", kHiResMapName, value.c_str()));
	}
}

// A signed integer within -maxAbs..maxAbs ("shift=-4").
bool parseSignedInt(const Common::String &value, int maxAbs, int &out) {
	const char *p = value.c_str();
	int sign = 1;
	if (*p == '-' || *p == '+') {
		sign = (*p == '-') ? -1 : 1;
		++p;
	}
	int n;
	if (!parseNumber(p, maxAbs, n) || *p != 0)
		return false;
	out = sign * n;
	return true;
}

/// face=: `same` in a bare [font] has nothing to inherit (a warning,
/// once per load, since only one merged bare face value ever exists); `same`
/// in [font.N] means "inherit", i.e. leave faceSet false (design 5.2).
void applyFaceKey(HiResFontScope &scope, const Common::String &value, bool isBareFont,
				   const Common::Path &mapDir, HiResMap &out) {
	if (value.equalsIgnoreCase("same")) {
		if (isBareFont)
			hiResWarn(out, Common::String::format("%s: [font] face=same has nothing to inherit; ignoring it", kHiResMapName));
		return;
	}
	HiResFontValue fv;
	Common::Array<Common::String> localWarnings;
	const bool any = parseFontValue(value, out.faces, mapDir, mapDir, fv, localWarnings);
	for (uint i = 0; i < localWarnings.size(); ++i)
		hiResWarn(out, localWarnings[i]);
	if (any) {
		scope.face = fv;
		scope.faceSet = true;
		scope.faceText = value;
	}
}

void applyFontScalarKeys(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
						  const char *section, bool isBareFont, const Common::Path &mapDir,
						  HiResMap &out, HiResFontScope &scope) {
	Common::String value;

	if (getKey(ini, qualifiers, section, "face", value))
		applyFaceKey(scope, value, isBareFont, mapDir, out);

	if (getKey(ini, qualifiers, section, "size", value)) {
		int v;
		if (parseFaceSize(value, v) && v >= 8 && v <= 64) {
			scope.size = v;
			scope.sizeSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [%s] size '%s' is not 8..64; ignoring it", kHiResMapName, section, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, section, "pixel", value)) {
		int v;
		if (parseFaceSize(value, v) && v >= 1 && v <= 64) {
			scope.pixel = v;
			scope.pixelSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [%s] pixel '%s' is not 1..64; ignoring it", kHiResMapName, section, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, section, "shift", value)) {
		int v;
		if (parseSignedInt(value, 32, v)) {
			scope.shift = v;
			scope.shiftSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [%s] shift '%s' is not -32..32; ignoring it", kHiResMapName, section, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, section, "cell", value)) {
		if (parseCellMode(value, scope.cell))
			scope.cellSet = true;
		else
			hiResWarn(out, Common::String::format(
				"%s: [%s] cell '%s' is not game or glyph; ignoring it", kHiResMapName, section, value.c_str()));
	}
	if (getKey(ini, qualifiers, section, "align", value)) {
		if (parseAlign(value, scope.align))
			scope.alignSet = true;
		else
			hiResWarn(out, Common::String::format(
				"%s: [%s] align '%s' is not game, cell or font; ignoring it", kHiResMapName, section, value.c_str()));
	}
	if (getKey(ini, qualifiers, section, "missing", value)) {
		if (value.equalsIgnoreCase("off")) {
			scope.missing = 0;
			scope.missingSet = true;
		} else {
			uint32 cp;
			if (parseCodePointValue(value, cp) && cp) {
				scope.missing = cp;
				scope.missingSet = true;
			} else {
				hiResWarn(out, Common::String::format(
					"%s: [%s] missing '%s' is not a code point or off; ignoring it", kHiResMapName, section, value.c_str()));
			}
		}
	}
	if (getKey(ini, qualifiers, section, "advance", value)) {
		HiResAdvance a;
		if (parseAdvance(value, a)) {
			scope.advance = a;
			scope.advanceSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [%s] advance '%s' is not game, font or cell; ignoring it", kHiResMapName, section, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, section, "origin", value)) {
		HiResOrigin o;
		if (parseOrigin(value, o)) {
			scope.origin = o;
			scope.originSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [%s] origin '%s' is not game or face; ignoring it", kHiResMapName, section, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, section, "mirror", value)) {
		if (parseMirror(value, scope.mirror))
			scope.mirrorSet = true;
		else
			hiResWarn(out, Common::String::format(
				"%s: [%s] mirror '%s' is not off, horizontal, vertical or both; ignoring it", kHiResMapName, section, value.c_str()));
	}
}

// ---- range./advance./origin.<spec> families (design 6.2.1) ---------------

/// One physical section's own entries of one family, kept apart from the
/// scope-level arrays so the duplicate-spelling and equal-width-overlap
/// checks (design 6.2.1) apply only within the section that wrote them.
template<typename ValueT>
struct HiResSpecBucket {
	Common::Array<HiResRangeSpec> specs;
	Common::Array<ValueT> values;
	Common::Array<Common::String> keys; ///< the key exactly as written, for warnings
};

bool hiResSpecSameSpan(const HiResRangeSpec &a, const HiResRangeSpec &b) {
	if (a.kind != b.kind)
		return false;
	if (a.kind == kHiResSpecWide)
		return true; // wide has no numeric span; two "wide" specs are one span
	return a.span.lo == b.span.lo && a.span.hi == b.span.hi;
}

template<typename ValueT>
void addHiResSpecEntry(HiResSpecBucket<ValueT> &bucket, const HiResRangeSpec &spec, const ValueT &value,
						const Common::String &keyText, const Common::String &sectionDisplay, HiResMap &out) {
	for (uint i = 0; i < bucket.specs.size(); ++i) {
		if (hiResSpecSameSpan(spec, bucket.specs[i])) {
			hiResWarn(out, Common::String::format("%s: [%s] %s repeats %s; ignoring it",
													kHiResMapName, sectionDisplay.c_str(), keyText.c_str(), bucket.keys[i].c_str()));
			return;
		}
		if (spec.kind == kHiResSpecSpan && bucket.specs[i].kind == kHiResSpecSpan) {
			const uint32 w1 = spec.span.hi - spec.span.lo;
			const uint32 w2 = bucket.specs[i].span.hi - bucket.specs[i].span.lo;
			if (w1 == w2 && spec.span.lo <= bucket.specs[i].span.hi && bucket.specs[i].span.lo <= spec.span.hi) {
				hiResWarn(out, Common::String::format("%s: [%s] %s overlaps %s at the same width; ignoring it",
														kHiResMapName, sectionDisplay.c_str(), keyText.c_str(), bucket.keys[i].c_str()));
				return;
			}
		}
	}
	bucket.specs.push_back(spec);
	bucket.values.push_back(value);
	bucket.keys.push_back(keyText);
}

/// Merge one physical section's bucket into the scope's arrays: a span
/// already present is replaced (silently: design 6.2.1's cross-section rule
/// carries no warning), any other span is appended.
template<typename ValueT>
void mergeHiResSpecBucket(Common::Array<HiResRangeSpec> &scopeSpecs, Common::Array<ValueT> &scopeValues,
						   const HiResSpecBucket<ValueT> &section) {
	for (uint i = 0; i < section.specs.size(); ++i) {
		int found = -1;
		for (uint j = 0; j < scopeSpecs.size(); ++j) {
			if (hiResSpecSameSpan(section.specs[i], scopeSpecs[j])) {
				found = (int)j;
				break;
			}
		}
		if (found >= 0) {
			scopeSpecs[found] = section.specs[i];
			scopeValues[found] = section.values[i];
		} else {
			scopeSpecs.push_back(section.specs[i]);
			scopeValues.push_back(section.values[i]);
		}
	}
}

/// The section names to merge for one scope: @p base itself, then each
/// qualifier from least to most specific (so the most specific - the first
/// of @p qualifiers - is applied last and wins).
void hiResScopeSectionNames(const Common::Array<Common::String> &qualifiers, const Common::String &base,
							 Common::Array<Common::String> &names) {
	names.push_back(base);
	for (int i = (int)qualifiers.size() - 1; i >= 0; --i) {
		if (!qualifiers[i].empty())
			names.push_back(Common::String::format("%s:%s", base.c_str(), qualifiers[i].c_str()));
	}
}

void collectRangeFamily(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
						 const Common::String &base, const Common::Path &mapDir, HiResFontScope &scope, HiResMap &out) {
	Common::Array<Common::String> names;
	hiResScopeSectionNames(qualifiers, base, names);

	for (uint s = 0; s < names.size(); ++s) {
		if (!ini.hasSection(names[s]))
			continue;
		HiResSpecBucket<HiResFontValue> bucket;
		const Common::INIFile::SectionKeyList keys = ini.getKeys(names[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin(); it != keys.end(); ++it) {
			if (!it->key.hasPrefixIgnoreCase("range.") || it->key.size() <= 6)
				continue;
			const Common::String specText(it->key.c_str() + 6);
			HiResRangeSpec spec;
			Common::String error;
			if (!parseRangeSpec(specText, spec, error)) {
				hiResWarn(out, Common::String::format("%s: [%s] %s: %s",
														kHiResMapName, names[s].c_str(), it->key.c_str(), error.c_str()));
				continue;
			}
			HiResFontValue fv;
			Common::Array<Common::String> w;
			const Common::String rhs = stripInlineComment(it->value);
			const bool any = parseFontValue(rhs, out.faces, mapDir, mapDir, fv, w);
			for (uint i = 0; i < w.size(); ++i)
				hiResWarn(out, w[i]);
			if (!any)
				continue;
			addHiResSpecEntry(bucket, spec, fv, it->key, names[s], out);
		}
		mergeHiResSpecBucket(scope.rangeSpecs, scope.rangeValues, bucket);
	}
}

void collectAdvanceFamily(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
						   const Common::String &base, HiResFontScope &scope, HiResMap &out) {
	Common::Array<Common::String> names;
	hiResScopeSectionNames(qualifiers, base, names);

	for (uint s = 0; s < names.size(); ++s) {
		if (!ini.hasSection(names[s]))
			continue;
		HiResSpecBucket<HiResAdvance> bucket;
		const Common::INIFile::SectionKeyList keys = ini.getKeys(names[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin(); it != keys.end(); ++it) {
			if (!it->key.hasPrefixIgnoreCase("advance.") || it->key.size() <= 8)
				continue;
			const Common::String specText(it->key.c_str() + 8);
			HiResRangeSpec spec;
			Common::String error;
			if (!parseRangeSpec(specText, spec, error)) {
				hiResWarn(out, Common::String::format("%s: [%s] %s: %s",
														kHiResMapName, names[s].c_str(), it->key.c_str(), error.c_str()));
				continue;
			}
			HiResAdvance value;
			const Common::String rhs = stripInlineComment(it->value);
			if (!parseAdvance(rhs, value)) {
				hiResWarn(out, Common::String::format("%s: [%s] %s '%s' is not game, font or cell; ignoring it",
														kHiResMapName, names[s].c_str(), it->key.c_str(), rhs.c_str()));
				continue;
			}
			addHiResSpecEntry(bucket, spec, value, it->key, names[s], out);
		}
		mergeHiResSpecBucket(scope.advanceSpecs, scope.advanceValues, bucket);
	}
}

void collectOriginFamily(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
						  const Common::String &base, HiResFontScope &scope, HiResMap &out) {
	Common::Array<Common::String> names;
	hiResScopeSectionNames(qualifiers, base, names);

	for (uint s = 0; s < names.size(); ++s) {
		if (!ini.hasSection(names[s]))
			continue;
		HiResSpecBucket<HiResOrigin> bucket;
		const Common::INIFile::SectionKeyList keys = ini.getKeys(names[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin(); it != keys.end(); ++it) {
			if (!it->key.hasPrefixIgnoreCase("origin.") || it->key.size() <= 7)
				continue;
			const Common::String specText(it->key.c_str() + 7);
			HiResRangeSpec spec;
			Common::String error;
			if (!parseRangeSpec(specText, spec, error)) {
				hiResWarn(out, Common::String::format("%s: [%s] %s: %s",
														kHiResMapName, names[s].c_str(), it->key.c_str(), error.c_str()));
				continue;
			}
			HiResOrigin value;
			const Common::String rhs = stripInlineComment(it->value);
			if (!parseOrigin(rhs, value)) {
				hiResWarn(out, Common::String::format("%s: [%s] %s '%s' is not game or face; ignoring it",
														kHiResMapName, names[s].c_str(), it->key.c_str(), rhs.c_str()));
				continue;
			}
			addHiResSpecEntry(bucket, spec, value, it->key, names[s], out);
		}
		mergeHiResSpecBucket(scope.originSpecs, scope.originValues, bucket);
	}
}

HiResFontScope buildFontScope(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
							   const Common::String &base, bool isBareFont, const Common::Path &mapDir, HiResMap &out) {
	HiResFontScope scope;
	applyFontScalarKeys(ini, qualifiers, base.c_str(), isBareFont, mapDir, out, scope);
	collectRangeFamily(ini, qualifiers, base, mapDir, scope, out);
	collectAdvanceFamily(ini, qualifiers, base, scope, out);
	collectOriginFamily(ini, qualifiers, base, scope, out);
	return scope;
}

/// The ids named by "<prefix>N" or "<prefix>N:<q>" sections (q in
/// @p qualifiers, or bare), each once, in first-seen order.
void collectRelevantIds(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
						 const char *prefix, Common::Array<int> &ids) {
	const size_t prefixLen = strlen(prefix);
	const Common::INIFile::SectionList sections = ini.getSections();
	for (Common::INIFile::SectionList::const_iterator sec = sections.begin(); sec != sections.end(); ++sec) {
		if (!sec->name.hasPrefixIgnoreCase(prefix))
			continue;
		const Common::String rest(sec->name.c_str() + prefixLen);
		const size_t colon = rest.findFirstOf(':');
		const Common::String idText = (colon == Common::String::npos) ? rest : Common::String(rest.c_str(), colon);
		const Common::String qualifier = (colon == Common::String::npos) ? Common::String() : Common::String(rest.c_str() + colon + 1);
		if (!qualifier.empty() && !qualifierListed(qualifiers, qualifier))
			continue;
		int id;
		if (!parseSectionId(idText, id))
			continue;
		bool seen = false;
		for (uint i = 0; i < ids.size() && !seen; ++i)
			seen = ids[i] == id;
		if (!seen)
			ids.push_back(id);
	}
}

// ---- [glyphs] / [glyphs.N] --------------------------------------------------

/// Expand one [glyphs]/[glyphs.N] key (a single code or a range) into
/// @p table; the value is parsed once per key (design: every expanded code
/// shares the one parsed rule).
void applyGlyphKey(const Common::String &key, const Common::String &rawValue, const Common::String &sectionDisplay,
					const Common::Path &mapDir, HiResGlyphTable &table, HiResMap &out) {
	const Common::String value = stripInlineComment(rawValue);
	const bool isRange = key.findFirstOf('-') != Common::String::npos;

	uint32 start, end;
	if (isRange) {
		if (!parseGlyphRange(key, start, end) || end < start) {
			hiResWarn(out, Common::String::format("%s: [%s] %s is not a character code or range; ignoring it",
													kHiResMapName, sectionDisplay.c_str(), key.c_str()));
			return;
		}
	} else {
		if (!parseCodeValue(key, start)) {
			hiResWarn(out, Common::String::format("%s: [%s] %s is not a character code or range; ignoring it",
													kHiResMapName, sectionDisplay.c_str(), key.c_str()));
			return;
		}
		end = start;
	}

	const uint32 count = end - start + 1;
	if (count > 0x10000) {
		hiResWarn(out, Common::String::format("%s: [%s] %s spans more than 0x10000 codes; ignoring it",
												kHiResMapName, sectionDisplay.c_str(), key.c_str()));
		return;
	}

	HiResGlyphRule rule;
	Common::String error;
	if (!parseGlyphRule(value, out.faces, mapDir, rule, error)) {
		hiResWarn(out, Common::String::format("%s: [%s] %s = '%s': %s",
												kHiResMapName, sectionDisplay.c_str(), key.c_str(), value.c_str(), error.c_str()));
		return;
	}

	for (uint32 code = start; code <= end; ++code)
		table[code] = rule;
}

/// One [glyphs]/[glyphs.N] table, bare merged with each matching qualifier
/// (least to most specific): within a section ranges are applied before
/// single codes, so a single code punches a hole in a range whatever the
/// line order; across sections a later (more specific) one wins key by key.
void collectGlyphSection(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers,
						  const Common::String &base, const Common::Path &mapDir,
						  HiResGlyphTable &table, HiResMap &out) {
	Common::Array<Common::String> names;
	hiResScopeSectionNames(qualifiers, base, names);

	for (uint s = 0; s < names.size(); ++s) {
		if (!ini.hasSection(names[s]))
			continue;
		const Common::INIFile::SectionKeyList keys = ini.getKeys(names[s]);
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin(); it != keys.end(); ++it) {
			if (it->key.findFirstOf('-') == Common::String::npos)
				continue;
			applyGlyphKey(it->key, it->value, names[s], mapDir, table, out);
		}
		for (Common::INIFile::SectionKeyList::const_iterator it = keys.begin(); it != keys.end(); ++it) {
			if (it->key.findFirstOf('-') != Common::String::npos)
				continue;
			applyGlyphKey(it->key, it->value, names[s], mapDir, table, out);
		}
	}
}

// ---- [shadow] --------------------------------------------------------------

/// mode=: an unknown mode is a warning (the key is ignored), not a silent
/// `game`.
bool parseHiResShadowMode(const Common::String &value, HiResShadowMode &out) {
	if (value.equalsIgnoreCase("game")) { out = kHiResShadowGame; return true; }
	if (value.equalsIgnoreCase("none")) { out = kHiResShadowNone; return true; }
	if (value.equalsIgnoreCase("drop")) { out = kHiResShadowDrop; return true; }
	if (value.equalsIgnoreCase("outline")) { out = kHiResShadowOutline; return true; }
	if (value.equalsIgnoreCase("stroke")) { out = kHiResShadowStroke; return true; }
	return false;
}

void buildShadowSection(const Common::INIFile &ini, const Common::Array<Common::String> &qualifiers, HiResMap &out) {
	Common::String value;
	if (getKey(ini, qualifiers, "shadow", "mode", value)) {
		HiResShadowMode m;
		if (parseHiResShadowMode(value, m))
			out.shadowMode = m;
		else
			hiResWarn(out, Common::String::format(
				"%s: [shadow] mode '%s' is not game, none, drop, outline or stroke; ignoring it", kHiResMapName, value.c_str()));
	}
	if (getKey(ini, qualifiers, "shadow", "offset", value)) {
		int offset;
		if (value == "-1")
			out.shadowOffset = -1;
		else if (parseInteger(value, 4096, offset))
			out.shadowOffset = offset;
		else
			hiResWarn(out, Common::String::format(
				"%s: [shadow] offset '%s' is invalid; ignoring it", kHiResMapName, value.c_str()));
	}
	if (getKey(ini, qualifiers, "shadow", "color", value)) {
		int color;
		if (parseInteger(value, 255, color)) {
			out.shadowColor = color;
			out.shadowColorSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [shadow] color '%s' is invalid; ignoring it", kHiResMapName, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, "shadow", "width", value)) {
		int q;
		if (parseQuarterPixels(value, kMaxShadowWidthQ, q))
			out.shadowWidthQ = q;
		else
			hiResWarn(out, Common::String::format(
				"%s: [shadow] width '%s' is invalid; ignoring it", kHiResMapName, value.c_str()));
	}
	if (getKey(ini, qualifiers, "shadow", "style", value)) {
		if (value.equalsIgnoreCase("round"))
			out.shadowStyle = kHiResOutlineRound;
		else if (value.equalsIgnoreCase("square"))
			out.shadowStyle = kHiResOutlineSquare;
		else if (value.equalsIgnoreCase("legacy"))
			out.shadowStyle = kHiResOutlineLegacy;
		else
			hiResWarn(out, Common::String::format(
				"%s: [shadow] style '%s' is invalid; ignoring it", kHiResMapName, value.c_str()));
	}
	if (getKey(ini, qualifiers, "shadow", "shadow", value)) {
		int dx, dy;
		if (value.equalsIgnoreCase("none")) {
			out.shadowShiftSet = true;
			out.shadowDx = out.shadowDy = 0;
		} else if (parseSignedPair(value, kMaxShadowShift, dx, dy)) {
			out.shadowShiftSet = true;
			out.shadowDx = dx;
			out.shadowDy = dy;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [shadow] shadow '%s' is invalid; ignoring it", kHiResMapName, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, "shadow", "shadow_color", value)) {
		int color;
		if (parseInteger(value, 255, color)) {
			out.shadowShiftColor = color;
			out.shadowShiftColorSet = true;
		} else {
			hiResWarn(out, Common::String::format(
				"%s: [shadow] shadow_color '%s' is invalid; ignoring it", kHiResMapName, value.c_str()));
		}
	}
	if (getKey(ini, qualifiers, "shadow", "shadow_alpha", value)) {
		int percent;
		if (parseInteger(value, 100, percent))
			out.shadowAlpha = (byte)((percent * 255 + 50) / 100);
		else
			hiResWarn(out, Common::String::format(
				"%s: [shadow] shadow_alpha '%s' is invalid; ignoring it", kHiResMapName, value.c_str()));
	}
}

/**
 * @p engineQualifiers: the caller's own list, used for [render] target=,
 * [text] and [layout] (design 3.4: not target-qualifiable). @p qualifiers:
 * qualifiersForTarget(engineQualifiers, options.target) - every other
 * qualifiable section ([fonts], [render] blend=/scale=/gamma=, [font],
 * [font.N], [glyphs], [glyphs.N], [shadow]).
 */
void buildHiResMap(const Common::INIFile &ini, const Common::Array<Common::String> &engineQualifiers,
					const Common::Array<Common::String> &qualifiers, const Common::Path &mapDir, HiResMap &out) {
	collectFaceNames(ini, qualifiers, out);

	buildRenderSection(ini, engineQualifiers, qualifiers, out);
	buildTextSection(ini, engineQualifiers, out);
	buildLayoutSectionV2(ini, engineQualifiers, out);

	out.font = buildFontScope(ini, qualifiers, "font", /* isBareFont */ true, mapDir, out);

	Common::Array<int> fontIds;
	collectRelevantIds(ini, qualifiers, "font.", fontIds);
	for (uint i = 0; i < fontIds.size(); ++i) {
		const Common::String base = Common::String::format("font.%d", fontIds[i]);
		out.fontIds[fontIds[i]] = buildFontScope(ini, qualifiers, base, /* isBareFont */ false, mapDir, out);
	}

	collectGlyphSection(ini, qualifiers, "glyphs", mapDir, out.glyphs, out);
	Common::Array<int> glyphIds;
	collectRelevantIds(ini, qualifiers, "glyphs.", glyphIds);
	for (uint i = 0; i < glyphIds.size(); ++i) {
		const Common::String base = Common::String::format("glyphs.%d", glyphIds[i]);
		HiResGlyphTable table;
		collectGlyphSection(ini, qualifiers, base, mapDir, table, out);
		out.glyphIds[glyphIds[i]] = table;
	}

	buildShadowSection(ini, qualifiers, out);
}

} // End of anonymous namespace

bool HiResFontMap::loadMap(Common::SeekableReadStream &stream, const Common::Path &mapDir,
						   const Common::Array<Common::String> &qualifiers, const HiResEngineKeys &engine,
						   HiResMap &out) {
	return loadMap(stream, mapDir, qualifiers, engine, out, HiResMapLoadOptions());
}

bool HiResFontMap::loadMap(Common::SeekableReadStream &stream, const Common::Path &mapDir,
						   const Common::Array<Common::String> &qualifiers, const HiResEngineKeys &engine,
						   HiResMap &out, const HiResMapLoadOptions &options) {
	out.clear();
	out.quietLoad = options.quiet;

	Common::INIFile ini;
	ini.requireKeyValueDelimiter();
	ini.allowNonEnglishCharacters();
	const bool parsed = ini.loadFromStream(stream) && !stream.err();

	int version = 0;
	if (parsed) {
		Common::String value;
		if (ini.getKey("version", "map", value))
			parseInteger(stripInlineComment(value), 0x7fffffff, version);
	}

	if (!parsed || version != 2) {
		const char *found = nullptr;
		if (parsed) {
			for (uint i = 0; i < ARRAYSIZE(kHiResOldSections) && !found; ++i) {
				if (ini.hasSection(kHiResOldSections[i]))
					found = kHiResOldSections[i];
			}
		}
		Common::String message = Common::String::format(
			"%s %s: not a version 2 map; regenerate it (graphics/hires_text/README.md)",
			kHiResMapName, mapDir.toString('/').c_str());
		if (found)
			message += Common::String::format(" (found [%s])", found);
		hiResWarn(out, message);
		return false;
	}

	out.version = version;
	out.loadedFor = options.target;

	const Common::Array<Common::String> expanded = qualifiersForTarget(qualifiers, options.target);
	scanHiResSections(ini, expanded, engine, out);
	buildHiResMap(ini, qualifiers, expanded, mapDir, out);

	return true;
}

bool HiResFontMap::loadMapFile(const Common::Path &mapPath, const Common::Array<Common::String> &qualifiers,
							   const HiResEngineKeys &engine, HiResMap &out) {
	return loadMapFile(mapPath, qualifiers, engine, out, HiResMapLoadOptions());
}

bool HiResFontMap::loadMapFile(const Common::Path &mapPath, const Common::Array<Common::String> &qualifiers,
							   const HiResEngineKeys &engine, HiResMap &out, const HiResMapLoadOptions &options) {
	out.clear();
	out.quietLoad = options.quiet;

	Common::FSNode mapNode(mapPath);
	Common::SeekableReadStream *stream = mapNode.createReadStream();
	if (!stream) {
		hiResWarn(out, Common::String::format(
			"%s %s: not a version 2 map; regenerate it (graphics/hires_text/README.md)",
			kHiResMapName, mapPath.toString('/').c_str()));
		return false;
	}

	const bool ok = loadMap(*stream, mapPath.getParent(), qualifiers, engine, out, options);
	delete stream;
	return ok;
}

} // End of namespace Graphics
