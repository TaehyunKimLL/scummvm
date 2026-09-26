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

#include "common/util.h"
#include "common/stack.h"
#include "graphics/primitives.h"

#include "sci/sci.h"
#include "sci/engine/state.h"
#include "sci/engine/selector.h"
#include "sci/graphics/cache.h"
#include "sci/graphics/scifont.h"
#include "sci/graphics/fontsjis.h"
#include "sci/graphics/fontkorean.h"
#include "sci/graphics/fontset.h"
#include "sci/graphics/fontunicode.h"
#include "sci/graphics/glyphsource_ttf.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "sci/graphics/view.h"

namespace Sci {

namespace {

// hires_text_font is honoured only for SCI16 games in a CJK code page. The
// key is the face the Korean/Japanese/Chinese hi-res text is drawn in; a
// Western or SCI32 game has no such text, and a TTF face there would replace
// fonts it was never meant to. Interim: once the hires_text master switch
// (HIRES_COMPOSITOR_DESIGN.md, step 3) exists, that switch decides instead.
bool hiresTextFontApplies(Common::String &why) {
	if (getSciVersion() >= SCI_VERSION_2) {
		why = "SCI32 games do not support it yet";
		return false;
	}
	switch (g_sci->getSciLanguageCodePage()) {
	case Common::kWindows949:
	case Common::kWindows932:
	case Common::kWindows936:
	case Common::kWindows950:
		return true;
	default:
		why = "the game's language has no hi-res CJK text";
		return false;
	}
}

// The default cell size, and the range hires_text_font_size may ask for.
const int kHiresTextFontDefaultSize = 16;
const int kHiresTextFontMinSize = 8;
const int kHiresTextFontMaxSize = 64;

} // End of anonymous namespace

GfxCache::GfxCache(ResourceManager *resMan, GfxScreen *screen, GfxPalette *palette)
	: _resMan(resMan), _screen(screen), _palette(palette),
	  _unicodeFont(nullptr), _unicodeFontTried(false),
	  _hiresTextFontResolved(false), _hiresTextFontSize(kHiresTextFontDefaultSize) {
}

void GfxCache::resolveHiresTextFont() {
	if (_hiresTextFontResolved)
		return;
	_hiresTextFontResolved = true;

	// The game's own domain only: ConfMan.hasKey(key) also finds a key set
	// in [scummvm], which would turn the face on for every game.
	const Common::String &domain = ConfMan.getActiveDomainName();
	if (!ConfMan.hasKey("hires_text_font", domain))
		return;

	Common::String why;
	if (!hiresTextFontApplies(why)) {
		warning("hires_text_font is ignored: %s", why.c_str());
		return;
	}

	// Parsed by hand: ConfMan.getInt() calls error() on non-numeric text.
	if (ConfMan.hasKey("hires_text_font_size", domain)) {
		const Common::String &value = ConfMan.get("hires_text_font_size", domain);
		char *end = nullptr;
		const long size = strtol(value.c_str(), &end, 10);
		if (value.empty() || *end != '\0' ||
			size < kHiresTextFontMinSize || size > kHiresTextFontMaxSize) {
			warning("hires_text_font_size '%s' is not a number from %d to %d; using %d",
					value.c_str(), kHiresTextFontMinSize, kHiresTextFontMaxSize,
					kHiresTextFontDefaultSize);
		} else {
			_hiresTextFontSize = (int)size;
		}
	}

	_hiresTextFontPath = ConfMan.get("hires_text_font", domain);
	if (_hiresTextFontPath.empty()) {
		// An empty value names nothing; FSNode would warn about it itself.
		warning("hires_text_font: empty path; using the .uni fonts");
	}
}

GfxCache::~GfxCache() {
	purgeFontCache();
	purgeViewCache();
}

void GfxCache::purgeFontCache() {
	for (FontCache::iterator iter = _cachedFonts.begin(); iter != _cachedFonts.end(); ++iter) {
		delete iter->_value;
		iter->_value = 0;
	}

	_cachedFonts.clear();

	// Fonts wrapped by an adapter are not in _cachedFonts, so they would
	// otherwise leak - and must be deleted AFTER the adapters that point at
	// them.
	for (uint i = 0; i < _ownedFonts.size(); i++)
		delete _ownedFonts[i];
	_ownedFonts.clear();

	// The shared bundle outlives individual adapters but not the cache.
	delete _unicodeFont;
	_unicodeFont = nullptr;
	_unicodeFontTried = false;
}

void GfxCache::purgeViewCache() {
	for (ViewCache::iterator iter = _cachedViews.begin(); iter != _cachedViews.end(); ++iter) {
		delete iter->_value;
		iter->_value = 0;
	}

	_cachedViews.clear();
}

bool GfxCache::fontIsSet(GuiResourceId fontId) {
	return dynamic_cast<GfxFontSet *>(getFont(fontId)) != nullptr;
}

GfxFont *GfxCache::createFontSet(GuiResourceId fontId) {
	// Stage 2 of docs/i18n/M10_FONTSET.md: the face the script asked for,
	// followed by whatever else can cover characters it cannot. Order is the
	// contract - the game's own face is first, so single-byte text is drawn
	// by the glyphs the game shipped and keeps its metrics.
	const bool haveResource = _resMan->testResource(ResourceId(kResourceTypeFont, fontId));
	if (!haveResource)
		return nullptr;

	GfxFontSet *set = new GfxFontSet(fontId, g_sci->getSciLanguageCodePage());
	set->addFace(new GfxFontFromResource(_resMan, _screen, fontId), GfxFontSet::kFaceResource);

	// The legacy double-byte faces, when the game ships their font file.
	// GfxFontKorean and GfxFontSjis call error() on a missing file, so
	// existence is checked rather than assumed.
	switch (g_sci->getLanguage()) {
	case Common::KO_KOR:
		if (Common::File::exists(Common::Path("korean.fnt")))
			// GfxFontKorean and GfxFontSjis already halve their own metrics below
		// SCI2, so they must NOT be marked as hires-plane faces here - doing
		// so halves twice and the glyphs pile up on top of each other.
		set->addFace(new GfxFontKorean(_screen, 1001), GfxFontSet::kFaceLegacyDbcs);
		break;
	case Common::JA_JPN:
		if (Common::File::exists(Common::Path("SJIS.FNT")))
			set->addFace(new GfxFontSjis(_screen, 900), GfxFontSet::kFaceLegacyDbcs);
		break;
	default:
		break;
	}

	// The Unicode bundle last: it is the widest, and being last means it only
	// answers for characters nothing else covers.
	if (GfxFontUnicode *uni = loadUnicodeFont())
		set->addFace(uni, GfxFontSet::kFaceCodePoint, false, true);

	return set;
}

GfxFontUnicode *GfxCache::loadUnicodeFont() {
	// The bundle is loaded once and shared by every set that uses it: it is
	// several hundred kilobytes and identical for all font ids. Ownership
	// stays here, which is why sets take it as an unowned face.
	if (!_unicodeFontTried) {
		_unicodeFontTried = true;
		GfxFontUnicode *f = new GfxFontUnicode(_screen, 0);
		bool ok = false;

		// hires_text_font names a TrueType face on disk, tried before the
		// bundled .uni fonts: a live face is preferred when the player asked
		// for one, and the .uni names remain the fallback, both when the key
		// is absent (identical to before) and when it names a face that
		// fails to load (one warning, never a hard error - the game must
		// still start, per the Task 3 harness check).
		resolveHiresTextFont();
		if (!_hiresTextFontPath.empty()) {
			const Common::String &path = _hiresTextFontPath;
			const int pixelSize = _hiresTextFontSize;

			Common::String error;
			Common::FSNode node(Common::Path(path, Common::Path::kNativeSeparator));
			// Checked with exists() and isDirectory() before
			// createReadStream(): that call emits its own "FSNode::
			// createReadStream: ..." warning for an absent node or a
			// directory, which would give run.log two warnings for one bad
			// path. A node that still fails to open (permissions, ...) goes
			// through createReadStream() and gets our single warning below.
			if (!node.exists()) {
				error = "does not exist";
			} else if (node.isDirectory()) {
				error = "is a directory";
			} else if (Common::SeekableReadStream *stream = node.createReadStream()) {
				// A Korean game needs Hangul from the face: one that has none
				// is refused, and the .uni fonts serve instead.
				const bool requireHangul = g_sci->getSciLanguageCodePage() == Common::kWindows949;
				const uint32 startMs = g_system->getMillis();
				TtfGlyphSource *src = TtfGlyphSource::create(stream, DisposeAfterUse::YES, pixelSize, error,
															 requireHangul);
				const uint32 elapsedMs = g_system->getMillis() - startMs;
				if (src) {
					f->setSource(src, path);
					ok = true;
					debug(1, "SCI: hires_text_font %s opened at %dpx in %u ms",
						  path.c_str(), pixelSize, elapsedMs);
				}
			} else {
				error = "could not open the file";
			}

			if (!ok) {
				warning("hires_text_font %s: %s; using the .uni fonts", path.c_str(), error.c_str());
				// Not retried when purgeFontCache() reloads the bundle, so
				// the warning is given once.
				_hiresTextFontPath.clear();
			}
		}

		static const char *const names[] = { "sci.uni", "korean.uni", "towns.uni" };
		for (uint i = 0; i < ARRAYSIZE(names) && !ok; i++)
			ok = f->load(names[i]);
		if (ok) {
			_unicodeFont = f;
		} else {
			delete f;
			_unicodeFont = nullptr;
		}
	}
	return _unicodeFont;
}

GfxFont *GfxCache::createUnicodeFont(GuiResourceId fontId) {
	if (!loadUnicodeFont())
		return nullptr;

	// The legacy CJK font, where the language has one, remains the fallback so
	// a bundle with partial coverage degrades to the old rendering instead of
	// to blank space. The resource font is the fallback otherwise, which also
	// keeps single-byte text pixel-identical to before.
	// The legacy CJK fonts abort the engine when their font file is absent
	// (GfxFontSjis calls error() on a missing SJIS.FNT), so they are only used
	// as the fallback when that file is actually present. Otherwise the
	// resource font serves, which is also what keeps single-byte text
	// identical to the unmodified engine.
	GfxFont *fallback = nullptr;
	if (fontId == 1001 && g_sci->getLanguage() == Common::KO_KOR &&
		Common::File::exists(Common::Path("korean.fnt")))
		fallback = new GfxFontKorean(_screen, fontId);
	else if (fontId == 900 && g_sci->getLanguage() == Common::JA_JPN &&
			 Common::File::exists(Common::Path("SJIS.FNT")))
		fallback = new GfxFontSjis(_screen, fontId);
	else if (_resMan->testResource(ResourceId(kResourceTypeFont, fontId)))
		fallback = new GfxFontFromResource(_resMan, _screen, fontId);
	// No fallback at all when the id names no resource. GfxText16 switches to
	// the legacy CJK font id (1001/900) to get double-byte glyphs, and most
	// games have no such resource - GfxFontFromResource would abort with
	// "font resource N not found". The adapter copes with a null fallback;
	// single-byte characters then have no glyph, which is correct, because
	// the game only ever switches to that id for double-byte text.
	if (fallback)
		_ownedFonts.push_back(fallback);

	return new GfxFontUnicodeAdapter(_unicodeFont, g_sci->getSciLanguageCodePage(),
									 fallback, fontId);
}

GfxFont *GfxCache::getFont(GuiResourceId fontId) {
	if (_cachedFonts.size() >= MAX_CACHED_FONTS)
		purgeFontCache();

	if (!_cachedFonts.contains(fontId)) {
		// A SCVMUNI bundle, when present, serves ANY font the game asks for.
		//
		// The legacy CJK fonts are reachable only through one hard-coded id
		// each - 1001 for Korean, 900 for Shift-JIS - and a game that does not
		// request that id never gets them. Measured: KQ5's Japanese FM-TOWNS
		// release contains fonts 0, 1, 4, 8, 9, 69, 600 and 999 and NO font
		// 900, all of them 128-glyph single-byte fonts, so the SJIS path is
		// unreachable there by construction. Keying the Unicode font on the
		// bundle rather than on a font number avoids that trap.
		// A set is preferred whenever the id names a real font resource: it
		// keeps the face the script chose and adds the others behind it.
		GfxFont *font = createFontSet(fontId);

		// Ids that name no resource are the legacy CJK ones the renderer
		// switches to. They get the adapter, which copes with having no
		// resource face at all.
		if (!font)
			font = createUnicodeFont(fontId);

		// Create special Korean font in korean games, when font 1001 is selected
		if (!font && (fontId == 1001) && (g_sci->getLanguage() == Common::KO_KOR))
			font = new GfxFontKorean(_screen, fontId);
		// Create special SJIS font in japanese games, when font 900 is selected
		if (!font && (fontId == 900) && (g_sci->getLanguage() == Common::JA_JPN))
			font = new GfxFontSjis(_screen, fontId);

		if (!font)
			font = new GfxFontFromResource(_resMan, _screen, fontId);

		_cachedFonts[fontId] = font;
	}

	return _cachedFonts[fontId];
}

GfxView *GfxCache::getView(GuiResourceId viewId) {
	if (_cachedViews.size() >= MAX_CACHED_VIEWS)
		purgeViewCache();

	if (!_cachedViews.contains(viewId))
		_cachedViews[viewId] = new GfxView(_resMan, _screen, _palette, viewId);

	return _cachedViews[viewId];
}

int16 GfxCache::kernelViewGetCelWidth(GuiResourceId viewId, int16 loopNo, int16 celNo) {
	return getView(viewId)->getCelInfo(loopNo, celNo)->scriptWidth;
}

int16 GfxCache::kernelViewGetCelHeight(GuiResourceId viewId, int16 loopNo, int16 celNo) {
	return getView(viewId)->getCelInfo(loopNo, celNo)->scriptHeight;
}

int16 GfxCache::kernelViewGetLoopCount(GuiResourceId viewId) {
#ifdef ENABLE_SCI32
	if (getSciVersion() >= SCI_VERSION_2) {
		return CelObjView::getNumLoops(viewId);
	}
#endif
	return getView(viewId)->getLoopCount();
}

int16 GfxCache::kernelViewGetCelCount(GuiResourceId viewId, int16 loopNo) {
#ifdef ENABLE_SCI32
	if (getSciVersion() >= SCI_VERSION_2) {
		return CelObjView::getNumCels(viewId, loopNo);
	}
#endif
	return getView(viewId)->getCelCount(loopNo);
}

} // End of namespace Sci
