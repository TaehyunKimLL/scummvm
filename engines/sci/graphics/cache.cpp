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
#include "common/file.h"
#include "sci/graphics/view.h"

namespace Sci {

GfxCache::GfxCache(ResourceManager *resMan, GfxScreen *screen, GfxPalette *palette)
	: _resMan(resMan), _screen(screen), _palette(palette),
	  _unicodeFont(nullptr), _unicodeFontTried(false) {
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
		static const char *const names[] = { "sci.uni", "korean.uni", "towns.uni" };
		bool ok = false;
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
