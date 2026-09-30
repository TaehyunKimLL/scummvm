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

#ifndef SCUMM_HIRES_TEXT_H
#define SCUMM_HIRES_TEXT_H

#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/language.h"
#include "common/path.h"
#include "common/rect.h"
#include "graphics/hires_text/glyph_renderer.h"
#include "graphics/pixelformat.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/surface.h"

#include "scumm/hires_overlay.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {
class UnicodeGlyphSource;
class TtfGlyphSource;
}

namespace Scumm {

/**
 * The engine's side of the hi-res text layer.
 *
 * Everything that knows about SCUMM lives here; everything that does not lives
 * in graphics/hires_text. The split is what lets a second engine reuse the
 * font handling without inheriting SCUMM's screen model.
 *
 * Since docs/superpowers/plans/2026-09-30-hires-config-unify.md Task 7, a
 * loaded map is version 2 (design docs/superpowers/specs/2026-09-30-hires-
 * config-unify-design.md): faces are named by Unicode range
 * (`range.<spec>=`), `[glyphs]` can target an exact face and code point, and
 * every character - CJK or ASCII alike - goes through one compiled
 * Graphics::HiResIdPlan per charset (Graphics::compileIdPlan(),
 * Graphics::pickGlyph()). A map-less configuration (fonts found by
 * conventional name, probeSimpleFonts()) keeps the older, simpler per-
 * charset/per-Latin-companion lookup: it has no map to compile a plan from.
 */
struct ScummHiResText {
	ScummHiResText();
	~ScummHiResText();

	// The layer owns its glyph sources, so it is not copied.
	ScummHiResText(const ScummHiResText &) = delete;
	ScummHiResText &operator=(const ScummHiResText &) = delete;

	/**
	 * Read the font map and the related config keys.
	 *
	 * @param gameDir     the game's own folder, for relative paths
	 * @param gameId      identifies the game, e.g. "monkey2"
	 * @param version     SCUMM version, for the "v5" style qualifier
	 * @param language    what the game was detected as
	 */
	void loadConfig(const Common::Path &gameDir, const Common::String &gameId,
					int version, Common::Language language);

	/**
	 * Settle the scale once the game's own font size is known.
	 *
	 * Only the simple, map-less form needs this: its fonts name no scale,
	 * so it is read off the smallest font's cell against the game's font.
	 * With a map this is a no-op. A scale the user named is kept as it
	 * stands, but a set that does not fit it is warned about.
	 *
	 * @param gameFontHeight  the height of the game's own CJK font, in game
	 *                        pixels; 0 when it has none
	 */
	void resolveScale(int gameFontHeight);

	/**
	 * Whether some font in a map-less set is exactly this multiple of the
	 * game's own cell.
	 *
	 * Public and static so the scale rule can be tested directly: it is the
	 * one question both the automatic scale and a user-named scale ask, and
	 * reaching it through resolveScale() needs a set of font files on disk.
	 *
	 * @param cells           every cell height found in the set
	 * @param count           how many
	 * @param gameFontHeight  the game's own cell, in game pixels
	 * @param scale           the multiple being tested
	 */
	static bool cellMatchesScale(const int *cells, int count,
								 int gameFontHeight, int scale);

	/**
	 * Whether resolveScale() will use the game's font height at all.
	 *
	 * Only a map-less set on a layer that is on reads it. Everything else -
	 * above all a game with no hi-res fonts - must not pay for finding it:
	 * the height is peeked at engine init, before the game's own subsystems
	 * exist, and the original path never reads the index twice.
	 */
	bool wantsGameFontHeight() const { return _enabled && _simpleFonts; }

	/**
	 * Whether the game's font height can be peeked at for this SCUMM
	 * version before the engine is set up.
	 *
	 * v0-v3 need no index, and nothing in a v4-v6 index needs a subsystem
	 * that is not built yet, so it can be read early. v7 and v8 are refused: their index
	 * hands the audio names to iMuse Digital (the ANAM block), which is only
	 * built later, in setupMusic(), and Rebel Assault has no index at all.
	 * Their screen is never enlarged for hi-res text either (see init()),
	 * so the height would decide nothing but a warning.
	 */
	static bool canPeekGameFontHeight(int version) { return version <= 6; }

	/**
	 * The scale glyphs can be drawn at, given how the screen is enlarged.
	 *
	 * A glyph opened at the requested scale on a text surface enlarged by a
	 * different factor lands on a layout made for the other one: FT at scale
	 * 2 drew 18px glyphs on its 9px grid, each running into the next.
	 * @p screenEnlarged is false where the screen is blitted without the
	 * compositing step that enlarges it (v7 and v8, see init()), and then
	 * only 1 fits. Otherwise a surface a platform enlarges by a factor of
	 * its own (@p surfaceMultiplier, FM-Towns) sets the size of a scaled
	 * layer; a scale of 1 is left as asked.
	 */
	static int drawableScale(int requested, int surfaceMultiplier, bool screenEnlarged) {
		if (requested <= 1)
			return requested;
		if (!screenEnlarged)
			return 1;
		if (surfaceMultiplier > 1 && requested != surfaceMultiplier)
			return surfaceMultiplier;
		return requested;
	}

	/// Draw at @p scale from now on; see drawableScale(). Call before loadFonts().
	void limitScale(int scale) { _scale = scale; }

	/**
	 * Tell the layer the cell of the game's own CJK font for one charset.
	 *
	 * Needed by the TrueType path, which bakes a face to the size the game
	 * draws each charset at. Call once per loaded game font, before
	 * loadFonts().
	 */
	void setGameFontCell(int charsetId, int width, int height);

	/**
	 * Whether Latin may step by the face at all (latinStepsByFace()). The
	 * engine clears it for charset renderers whose getCharWidth() measures
	 * single-byte text with the game's own widths and never asks
	 * advanceFor() (FM-Towns TownsClassic/TownsV3, the fixed 8-px V2), so
	 * that what they measure is what printChar() steps by (C36).
	 */
	void setLatinFaceStepAllowed(bool allowed) { _latinFaceStepAllowed = allowed; }
	void reset();

	/**
	 * Whether the hi-res path should be used at all.
	 *
	 * False here has to leave the engine on exactly its original path: this
	 * is the switch that keeps every game we do not touch untouched.
	 */
	bool enabled() const { return _enabled; }

	int scale() const { return _scale; }

	/**
	 * Whether the resolved blend setting wants a coverage (alpha) surface.
	 *
	 * `blend == on || (blend == auto && some named face has coverage)`
	 * (design section 7.2, evaluated once at load - Task 8 replaces this
	 * with render_target()/blendActive() per glyph). Distinct from
	 * alphaActive(): the map may ask for blending and not get it, because
	 * the backend could not provide a true-colour screen.
	 */
	bool wantsAlpha() const { return _wantsAlpha; }

	/// The pure rule wantsAlpha() applies, exposed for tests.
	static bool wantsAlphaFor(Graphics::HiResBlend blend, bool anyFaceHasCoverage) {
		return blend == Graphics::kHiResBlendOn || (blend == Graphics::kHiResBlendAuto && anyFaceHasCoverage);
	}

	/**
	 * design section 7.1.1's phase-1 coverage question
	 * (Graphics::HiResCoverageFn, passed to Graphics::mapHasCoverage()): a 2
	 * or 8 bpp SVFN has coverage; a 1 bpp SVFN does not; anything else that
	 * opens at all (a TrueType face) does - there is no cheap way to peek
	 * whether it is monochrome, and every shipped one is anti-aliased.
	 * Exposed (rather than a file-local static) so the rule can be tested
	 * against a real file on disk.
	 */
	static bool faceHasCoverage(const Common::Path &face, void *ctx);

	/**
	 * Whether glyphs are being blended into a true-colour screen.
	 *
	 * Distinct from wantsAlpha(): the map may ask for blending and not get it,
	 * because the backend could not provide a 32bpp screen. Only this says
	 * what is actually happening.
	 */
	bool alphaActive() const { return _alphaActive; }

	/**
	 * Record whether the negotiated screen can carry blended text.
	 *
	 * Called once the backend has answered, so a map asking for alpha on a
	 * paletted-only display quietly falls back instead of drawing nothing.
	 */
	void setAlphaActive(bool active) { _alphaActive = active; }

	/**
	 * Refresh the cached true-colour palette.
	 *
	 * In alpha mode the engine stops handing the backend a palette, so the
	 * lookup happens here instead: the game's graphics stay paletted and are
	 * converted when the composite buffer is built. Every palette change has
	 * to reach this cache or the screen and the table disagree.
	 *
	 * @param format  the negotiated screen format
	 * @param rgb     RGB triples, @p num of them
	 * @param first   first palette entry the triples describe
	 * @param num     how many entries
	 */
	void updatePaletteCache(const Graphics::PixelFormat &format, const byte *rgb,
							uint first, uint num);

	/// The cached colour for a palette index; valid only in alpha mode.
	uint32 paletteColor(byte index) const { return _paletteCache[index]; }

	/**
	 * The whole index-to-colour table, for a compositor that resolves runs.
	 *
	 * Handing out the table rather than the colours keeps the promise the
	 * overlay is built on: indices are what is stored, and a palette change
	 * re-colours text that was drawn long before.
	 */
	const uint32 *paletteCache() const { return _paletteCache; }

	/// The cached palette as RGB triples, for the cursor, which stays paletted.
	const byte *paletteRGB() const { return _paletteRGB; }

	/**
	 * How far the pen should move after drawing a character, in game pixels.
	 *
	 * The game decides line breaks and speech-bubble sizes from the widths of
	 * its own font, so a replacement that advances differently can push text
	 * out of a bubble or wrap it in the wrong place. The id's resolved
	 * `advance=` (design sections 6.2, 6.3) chooses: `game`/`font` keep or
	 * replace the game's own width (Graphics::advanceGamePx()); `cell` and
	 * the engine default (nothing set an advance rule) fall to the legacy
	 * grid rule (C31).
	 *
	 * A proportional replacement font measures in the scaled surface's pixels
	 * while the engine positions text in game pixels, so the division is lossy:
	 * at 2x an advance of 5 has to become 2 or 3. Rounding every character up
	 * costs up to (scale - 1) pixels each and visibly loosens a line.
	 *
	 * Pass @p carry - zeroed at the start of each run - and the remainder is
	 * spent on the following characters instead, so the run as a whole keeps
	 * the font's own metrics and only its last character can be short.
	 *
	 * @param chr        the character, in the game's own encoding
	 * @param charsetId  the game's current charset number
	 * @param gameWidth  what the engine's own font would have advanced
	 * @return the advance to use, in unscaled game pixels
	 */
	int advanceFor(int chr, int charsetId, int gameWidth,
				   int *carry = nullptr) const;

	/**
	 * Whether the ASCII character @p chr steps by the replacement face's own
	 * advance rather than the game's Latin width. C34 did this inside CJK
	 * text and UTF-8 translations; since C36 it is the default for any
	 * text, the game's own English included. Kept as a thin wrapper over
	 * advanceFor()'s own resolution: true exactly when advanceFor() would
	 * draw @p chr from a TrueType face and step by that face's own advance.
	 */
	bool latinStepsByFace(int chr, int charsetId) const;

	/**
	 * Whether printChar() leaves the game glyph's offsX/offsY out for @p chr:
	 * the id's resolved `origin=`/`origin.<spec>=` for @p chr's code point is
	 * `face` (design section 8's `origin`, replacing the old ASCII-only
	 * `[latin] baseline=face`) and a replacement face actually draws it.
	 * Off (false) for every map that does not ask, and for a renderer that
	 * switched the face step off (setLatinFaceStepAllowed(false): FM-Towns, V2).
	 */
	bool latinBaselineByFace(int chr, int charsetId) const;

	/**
	 * Whether printChar() should have this layer draw @p chr although the
	 * game's charset has no glyph for it (prepareDraw() failed): a UTF-8
	 * code point, whose '?' stand-in is missing, or ASCII that steps by the
	 * face (latinStepsByFace()). getCharWidth() measures such a character
	 * by advanceFor(chr, cs, 0), which is then the step it is drawn with;
	 * any other missing code measures 0 and is not drawn, as before.
	 */
	bool drawsMissingGameGlyph(int chr, int charsetId, bool utf8Text) const {
		return (utf8Text && chr >= 0x80) || latinStepsByFace(chr, charsetId);
	}

	/**
	 * Whether drawChar() would draw @p chr in @p charsetId rather than
	 * decline it: a face has an inked glyph for it and the map does not keep
	 * the game's own. Lets a UTF-8 layout give a code point the patch
	 * font's cell only when this layer is what draws it (C31).
	 */
	bool drawsCode(int chr, int charsetId) const;

	/**
	 * Whether glyphs are placed by their own metrics (I18N_TEXT_DESIGN.md
	 * section 4.2): true whenever a plan is in effect (a map was loaded, or
	 * the ini named a face directly). The map-less, name-only form
	 * (probeSimpleFonts()) has no plan and keeps the original per-charset
	 * bitmap-font lookup.
	 */
	bool perGlyphMetrics() const { return _perGlyph; }

	/**
	 * Add one translated string to the code points the faces are checked
	 * against (design section 4.4). Decoded with this layer's encoding;
	 * SCUMM's escapes (0xFF/0xFE + code + escapeArgBytes(code) argument
	 * bytes) and '@' are skipped. Stops at a NUL outside an
	 * escape or after @p maxLen bytes. Call before loadFonts().
	 */
	void noteTranslatedString(const byte *s, uint32 maxLen);

	/// The code points noteTranslatedString() has collected so far.
	const Graphics::CodePointSet &translationCodePoints() const { return _translationCps; }

	/// The coverage warnings printed so far, one per face that lacks some of
	/// the translation's sampled characters or spaces its combining marks.
	const Common::Array<Common::String> &coverageWarnings() const { return _coverageWarnings; }

	/**
	 * The format to declare a cursor in.
	 *
	 * Cursor data is palette indices whatever the screen is. When blending is
	 * active the screen is true colour, and declaring the cursor in the screen
	 * format would have the backend read one index byte per channel - the
	 * cursor comes out as noise smeared across four times its width. Keep
	 * saying CLUT8, and let the cursor palette carry the colours.
	 *
	 * @param screenFormat  what the backend reports for the screen
	 */
	Graphics::PixelFormat cursorFormat(const Graphics::PixelFormat &screenFormat) const;


	Common::CodePage encoding() const { return _encoding; }

	/**
	 * The translation is UTF-8 (a .trs bundle with a body BOM, or ini
	 * text_encoding=utf8): it outranks the language's default code page
	 * and the map's [text] encoding, and the engine hands drawChar() and
	 * advanceFor() code points instead of code-page bytes. Call after
	 * loadConfig() and before loadFonts().
	 */
	void useUtf8Text();

	/**
	 * How SCUMM breaks UTF-8 lines: Hangul anywhere (the Korean patches'
	 * own rule, so a UTF-8 ko.trs breaks where the CP949 korean.trs does),
	 * kinsoku and the Thai fallback on; the map's [layout] overrides each.
	 */
	/// @param centred  the text is centred: with this layer on, Hangul then
	///                 breaks at spaces only by default, as the Korean patches
	///                 break it (C31); with it off nothing changes
	Graphics::BreakRules breakRules(bool centred = false) const;

	/// The parsed map (design section 3); shadow/layout/encoding readers use
	/// it. Empty (HiResMap::clear()'s default) when no map is loaded.
	const Graphics::HiResMap &map() const { return _map; }

	/**
	 * Decode the next character of a game string.
	 *
	 * This is the only place that knows how many bytes a character takes, so
	 * everything past it works on Unicode code points and no longer has to
	 * assume "one byte is Latin, two bytes are CJK". That assumption is false
	 * in both directions: an accented Latin letter is multi-byte in UTF-8,
	 * and half-width katakana is single-byte in Shift-JIS.
	 *
	 * The caller is expected to have dealt with the engine's own control
	 * codes already. This must not be handed a byte that SCUMM treats as an
	 * escape, because a trail byte can have the same value as one.
	 *
	 * @param p    read pointer, advanced past the character consumed
	 * @param end  one past the last readable byte
	 * @return the code point, or 0 when nothing could be decoded
	 */
	uint32 decodeNext(const byte *&p, const byte *end) const;

	/**
	 * Load the replacement fonts the map named.
	 *
	 * @param gameDir  where a relative font name is looked for
	 * @return false when nothing usable loaded, leaving the engine on its
	 *         original path
	 */
	bool loadFonts(const Common::Path &gameDir);

	/**
	 * The glyph source for one of the game's charsets, map-less form only
	 * (probeSimpleFonts(): no plan, so no per-glyph routing).
	 *
	 * @param charsetId  the game's own charset number
	 * @param latin      a single-byte character, which a double-byte set
	 *                   indexed by a CJK code page cannot hold
	 * @return null when nothing covers it, i.e. draw it the original way
	 */
	Graphics::UnicodeGlyphSource *sourceFor(int charsetId, bool latin = false) const;

	/// How many distinct glyph sources are open, for tests and logs.
	int sourceCount() const;

	/// Face @p index of the TrueType chain charset @p charsetId draws with,
	/// map-less form; or null (no chain, or fewer faces); for tests.
	Graphics::TtfGlyphSource *ttfChainFace(int charsetId, uint index) const;

	/**
	 * The source that will draw @p cp in @p charsetId under a compiled plan,
	 * or null when nothing does (the game's own font draws it); for tests,
	 * and the same resolution drawChar()/advanceFor() use. @p cp is updated
	 * to the code point actually drawn (a [glyphs] remap, a range rule's
	 * substitution, or the missing= mark).
	 */
	Graphics::UnicodeGlyphSource *perGlyphSourceFor(int charsetId, uint32 &cp) const;

	/**
	 * Take an already parsed version-2 map and switch the layer on.
	 *
	 * For tests and tools, which have neither ConfMan nor a game folder;
	 * the engine goes through loadConfig().
	 */
	void adoptMap(const Graphics::HiResMap &map, const Graphics::HiResIniOverrides &ini = Graphics::HiResIniOverrides());

	/**
	 * Add one face - an SVFN bitmap font, sniffed by its magic - opened from
	 * @p stream, keyed by @p resolvedPath (design section 5.4: one entry per
	 * distinct path). Once every face a charset's compiled plan names has
	 * been added (or failed to add), that charset's load-time checks run
	 * once: an SVF whose cell height differs from the first SVF of its id's
	 * chain is refused (design section 5.4); a [glyphs] target lacking its
	 * code point, and missing= naming a code point no face of the id's chain
	 * has, are each warned about once (design section 10.4).
	 *
	 * @return false when the stream is not a usable SVFN font
	 */
	bool addFace(const Common::String &resolvedPath, Common::SeekableReadStream &stream) const;

	/// The glyph source addFace() opened for @p resolvedPath, or null; for tests.
	Graphics::UnicodeGlyphSource *sourceForFace(const Common::String &resolvedPath) const;

	/// The compiled plan for @p charsetId (design sections 5, 6, 8); for tests
	/// and Task 8. Charset ids outside 0..19 answer id 0's plan.
	const Graphics::HiResIdPlan &planFor(int charsetId) const;

	/**
	 * SCUMM's engine scope (design section 8, the defaults consulted below
	 * [font]): range.basic-latin=same (ASCII from the charset's own face,
	 * the old font=same default), advance.basic-latin=game.
	 */
	static Graphics::HiResFontScope engineScope();

	/**
	 * The decoration [shadow] mode=game asks for, from the game's shadow byte.
	 *
	 * A mode named in the map wins. Otherwise 1 is none, 2 a drop, 3 stroke
	 * and anything else the outline, as the Korean patches' own renderer
	 * reads byte 1 of korean%02d.fnt - except that 0 is an outline only when
	 * such a font set it (@p korPatchShadow); an unset 0 draws nothing.
	 */
	static Graphics::HiResShadowMode resolveShadow(Graphics::HiResShadowMode fromMap,
												   int gameShadow, bool korPatchShadow);

	/**
	 * The colours and decoration a replacement glyph is drawn with (C19).
	 *
	 * The mode comes from resolveShadow(); its geometry from the map's
	 * [shadow] keys at the map's scale (HiResGlyphRenderer::applyMap()). For
	 * the Korean patch bytes at 2x that gives: 0 (patch) and 4+ a round
	 * outline 1.5 px wide, 2 a drop of the glyph by (1, 1), 3 that outline
	 * plus a copy of it moved (-1, +1) in place of the old stroke table.
	 *
	 * Unchanged since before Task 7 (graphics/hires_text is not touched by
	 * it): @p config carries just the [shadow] fields, built from the map by
	 * legacyShadowConfig().
	 */
	static Graphics::GlyphStyle glyphStyle(const Graphics::HiResTextConfig &config,
										   int gameShadow, bool korPatchShadow,
										   byte color, byte shadowColor);

	/**
	 * The game pixels an area of the overlay touches: @p area divided by the
	 * scale @p m, rounded outwards. A decorated glyph reaches beyond the game
	 * cell the engine marks dirty - an outline to the left and above, a
	 * stroke's shadow further - and the part outside would only reach the
	 * screen when something else redrew it.
	 */
	static Common::Rect gameRectFor(const Common::Rect &area, int m);

	/**
	 * A rect of a virtual screen, in that screen's own rows, as overlay
	 * pixels: moved down by @p topOffset (the screen's topline less
	 * _screenTop) and scaled by @p m.
	 */
	static Common::Rect overlayRectFor(const Common::Rect &rect, int topOffset, int m);

	/**
	 * A hi-res glyph drawn on a single-buffered virtual screen (C32), in
	 * overlay pixels: @p cell is the game cell it stands for, @p area what it
	 * inked, decoration included.
	 */
	struct TracedGlyph {
		Common::Rect cell;
		Common::Rect area;
		bool inBackBuffer = false; ///< drawn with _blitAlso: the game's back buffer has it too
	};

	/**
	 * The game painted @p painted (overlay pixels) of a screen whose text it
	 * draws into its own buffer, so the text there is gone. Append to
	 * @p clear what the overlay must lose: the painted area itself, then the
	 * whole of each glyph whose cell the paint touched - its decoration too,
	 * which would otherwise stay as slivers - and drop those glyphs from
	 * @p glyphs. A glyph whose cell is outside the paint stays whole even when
	 * its decoration reaches in: the game did not erase it.
	 */
	static void retireTracedGlyphs(const Common::Rect &painted, Common::Array<TracedGlyph> &glyphs,
								   Common::Array<Common::Rect> &clear);

	/**
	 * The rule under retireTracedGlyphs(), for either kind of record: drop
	 * from @p glyphs every glyph whose cell @p painted meets and append its
	 * whole area to @p clear.
	 *
	 * @param clearPainted    put @p painted itself first in @p clear (a
	 *                        single-buffered screen, where all its text is
	 *                        the game's buffer), and skip glyphs it holds
	 * @param keepBackBuffer  leave glyphs drawn into the back buffer too: the
	 *                        paint reached the front only, and the game blits
	 *                        that copy back later
	 */
	static void retireGlyphsByCell(const Common::Rect &painted, Common::Array<TracedGlyph> &glyphs,
								   Common::Array<Common::Rect> &clear, bool clearPainted, bool keepBackBuffer);

	/**
	 * Say whether the game's shadow byte comes from the Korean patch fonts,
	 * drawn by drawBits1Kor() (a kor-trs v1-v6 target). v7 draws its own
	 * shadow in draw2byte() and is left out.
	 */
	void setKorPatchShadow(bool on) { _korPatchShadow = on; }

	/**
	 * Whether a decoration may get a layer of its own below the text (C19):
	 * true only where the compositor reads one. The overlay's under planes
	 * are then made on the first decorated glyph drawn into it.
	 */
	void setLayeredDecorations(bool on) { _layeredDecorations = on; }

	/**
	 * Whether this SCUMM version can blend hi-res text at all. v7 and v8
	 * leave the backend palette to SMUSH, so init() keeps their screen
	 * paletted and draws the text keyed (see scumm.cpp).
	 */
	static bool canBlendText(int version) { return version < 7; }

	/**
	 * How a game's own charset is flipped (C27): an explicit table, as no
	 * heuristic on the glyphs is needed for three games. MI1 (v4/v5), MI2
	 * and Loom CD (v4) have a charset 3 whose glyphs are turned half a turn;
	 * MI1 draws the dazed dialogue choices in the Fettucini brothers' tent
	 * (room 51) with it, their strings stored reversed.
	 *
	 * @param gameId     the game's id, e.g. "monkey2"
	 * @param version    its SCUMM version
	 * @param charsetId  the charset number ([font.N])
	 */
	static Graphics::HiResMirror gameMirror(const Common::String &gameId, int version, int charsetId);

	/// Fill the per-charset table from gameMirror(). loadConfig() calls it.
	void setGameMirror(const Common::String &gameId, int version);

	/**
	 * How a charset's replacement glyphs are flipped: [font.N] mirror= when
	 * set - true meaning "as the game's font", or horizontal for a charset
	 * @p game does not know - else @p game.
	 *
	 * @param mirrorSet  the plan named mirror= for this id (HiResIdPlan::mirrorSet)
	 * @param mirror     that value (HiResIdPlan::mirror); meaningless unless @p mirrorSet
	 */
	static Graphics::HiResMirror resolveMirror(bool mirrorSet, Graphics::HiResMirror mirror,
											   Graphics::HiResMirror game);

	/**
	 * Whether a character of a flipped charset stays on the game's own font
	 * (C27): a replacement would draw the face's upright glyphs where the
	 * game shows turned ones. It does unless the id names its own
	 * replacement at all (a face, or mirror= itself), and only for what the
	 * game can draw: UTF-8 text beyond ASCII has no game glyph, so the
	 * replacement draws it, flipped as the game's are.
	 *
	 * @param named  the id has its own configuration: a face (or `original`)
	 *               or mirror= (HiResIdPlan::mirrorSet, or a non-empty/
	 *               original idChain)
	 * @param game   gameMirror() for the charset
	 * @param utf8   the text is UTF-8 (chr is a code point)
	 * @param chr    the character, as drawChar() is given it
	 */
	static bool keepsGameFont(bool named, Graphics::HiResMirror game, bool utf8, int chr);

	/**
	 * Whether the CJK conversion tables (encoding.dat) can be read.
	 *
	 * Without them no double-byte string decodes, so the layer draws no
	 * CJK glyph at all; loadConfig() warns once when that is the case.
	 */
	static bool cjkTablesPresent(Common::CodePage page = Common::kWindows949);

	/**
	 * Tell the layer which grid the engine settled on for this charset.
	 *
	 * Call it after the engine has resolved its own font, so the values are
	 * the ones text is actually laid out on. They may not be the charset's
	 * nominal size: upstream remaps charset 6 to font 0 to work around a data
	 * error in MI1 CD, MI2 and DOTT, so charset 6 asks for a 14px font and is
	 * given an 11x12 one.
	 *
	 * The hi-res layer needs this to choose a stand-in that fits the same
	 * grid; guessing from the charset's nominal height picks a font that is
	 * too big and the glyphs overlap.
	 */
	void setCharsetGrid(int charsetId, int width, int height);

	/**
	 * Tell the layer the cell the game's own font uses for this charset.
	 *
	 * Called when a charset is selected, which is the first moment the size
	 * is knowable: the resources are read long after the hi-res layer is set
	 * up. With a TrueType face this decides the pixel size the face is
	 * opened at for this charset, the first time it draws. The first cell
	 * recorded for a charset stands: the double-byte font's cell, when the
	 * game has one, is the grid the text is laid out on.
	 */
	void noteGameCharset(int charsetId, int width, int height);

	/**
	 * A new string starts: a combining mark at its start has no base
	 * before it. Call when the renderer begins a string.
	 */
	void beginString() { _anchorValid = false; }

	/** Finish and print any partially accumulated text-log line. */
	void endTextRun() const { if (_logText) flushTextLog(); }

	/** Whether HRTEXT diagnostics are on, for callers that log too. */
	bool logText() const { return _logText; }

	/// Whether any replacement font is loaded.
	bool hasFonts() const;

	/**
	 * Draw one character with the replacement font.
	 *
	 * @param dest       the surface the engine would have drawn to
	 * @param chr        the character, in the game's own encoding
	 * @param charsetId  the game's current charset number
	 * @param x, y       where the glyph goes, in destination pixels
	 * @param color      palette index for the glyph body
	 * @param shadowColor  palette index for the decoration
	 * @param gameShadow the engine's own shadow style, followed when the map
	 *                   did not override it
	 * @param dirty      if not null, extended by the area written
	 * @param withCoverage  whether to record per-pixel coverage alongside the
	 *                   glyph. Only a caller whose surface is later composited
	 *                   through the coverage plane wants this. v7 draws
	 *                   straight into the VirtScreen, which the backend blits
	 *                   out as it stands, so coverage recorded for it would
	 *                   never be read - and never cleared either, so it would
	 *                   go on suppressing later strokes at those pixels.
	 * @param gameAdvance  the advance, in game pixels, the caller steps by
	 *                   after this character (what advanceFor() returned);
	 *                   0 when not known. With perGlyphMetrics(), a glyph
	 *                   under advance=game is centred in that cell.
	 * @return false when nothing was drawn and the caller must fall back
	 *
	 * A combining mark is drawn against the pen after the previous base,
	 * kept here in overlay pixels (not re-derived from the engine's pen,
	 * which is rounded to game pixels), and moves nothing.
	 */
	bool drawChar(Graphics::Surface &dest, int chr, int charsetId,
				  int x, int y, byte color, byte shadowColor,
				  int gameShadow, Common::Rect *dirty = nullptr,
				  bool withCoverage = true, int gameAdvance = 0);

	/// Point the layer at the engine's overlay. Must precede any drawing.
	void useOverlay(HiResOverlay *overlay) { _overlay = overlay; }

	void createCoverage(int w, int h);

	void freeCoverage();

	/// The coverage surface, or null when this configuration has none.
	Graphics::Surface *coverage() { return _overlay ? _overlay->coverage() : nullptr; }
	const Graphics::Surface *coverage() const { return _overlay ? _overlay->coverage() : nullptr; }

private:
	bool _enabled;
	bool _simpleFonts = false;      ///< fonts found by name, with no map
	int _simpleCellHeight = 0;      ///< smallest cell among them, for the scale
	bool _scaleFromUser = false;
	HiResOverlay *_overlay = nullptr;
	bool _layeredDecorations = false;

	static const int kMaxFonts = 20;

	// Every cell in a map-less set, one per font found. The scale is worked
	// out against these rather than against the smallest alone: a set holds
	// one font per charset at that charset's own size, so the smallest
	// belongs to a different charset from the one game height that is known.
	int _simpleCells[kMaxFonts] = {};
	int _simpleCellCount = 0;

	/**
	 * One open glyph source and what the SCUMM side needs besides it.
	 *
	 * @p ttf, @p pixelSize and @p lineFit are set only for a TrueType face;
	 * they drive the wide-glyph cell clipping and the C31 face-step rule.
	 * An SVFN face's ink and baseline are read off @p source generically
	 * (UnicodeGlyphSource::metrics()/row()/baselineRow()), so no bitmap-
	 * specific pointer is kept here any more.
	 */
	struct Face {
		Graphics::UnicodeGlyphSource *source = nullptr;      ///< owned
		Graphics::TtfGlyphSource *ttf = nullptr;             ///< TrueType: source, typed
		int pixelSize = 0;         ///< TrueType: the size it was opened at
		/// TrueType: sized to the game cell (the start-up bake's rule), so a
		/// glyph and its ink reach are clipped to that cell.
		bool lineFit = true;
		/// One past the rightmost inked column, per code point (pixel-scan
		/// cache; see glyphInk()).
		Common::HashMap<uint32, int16> inkRight;
		/// Map-less form only (openTtfChain()): the whole fallback list
		/// merged into @p source, each face in order and its name, for
		/// checkCoverage().
		Common::Array<Graphics::UnicodeGlyphSource *> chain;
		Common::Array<Common::String> chainNames;
	};

	/// Every open source, keyed by its resolved path (an SVFN face) or
	/// "ttf:<path>@<px>[c][p<grid>]" (a TrueType face at one pixel size). A
	/// face that failed to open is kept as a null entry, so it is tried (and
	/// warned about) once.
	mutable Common::HashMap<Common::String, Face *> _sources;
	/// Paths that failed to open at all, or were not a usable font; tried
	/// (and warned about) once.
	mutable Common::HashMap<Common::String, bool> _failedFaces;

	/// The map-less form's whole fallback chain, merged into one source
	/// (FallbackGlyphSource) - unlike the plan-based path below, which
	/// leaves each chain entry a separate source for pickGlyph() to search.
	Face *openTtfChain(const Common::Array<Common::Path> &chain, int pixelSize, bool lineFit,
					   int pixelGrid = 0) const;
	/// One plan chain entry, opened (and cached in _sources) on its own:
	/// an SVFN shares the entry addFace() made (or opens it from disk, for
	/// a path loadFonts() did not already add); a TrueType face is opened
	/// (or reused) at @p pixelSize, @p pixelGrid only for the id chain's
	/// first face (design section 5.4).
	Graphics::UnicodeGlyphSource *openPlanFace(const Common::Path &path, int pixelSize, bool lineFit,
											   int pixelGrid) const;
	int ttfCellWidth(int charsetId) const;

	// --- map-less, name-only form (probeSimpleFonts()) -------------------
	Face *_simpleCjkFaces[kMaxFonts] = {};     ///< by charset id
	Face *_simpleLatinFaces[kMaxFonts] = {};   ///< Latin companion, by charset id
	mutable Face *_ttfFaces[kMaxFonts] = {};
	mutable int _ttfFacePx[kMaxFonts] = {};
	Common::Path _ttfPathSimple;                ///< the one face this form names, if any
	Face *faceForSimple(int charsetId, bool latin) const;
	Face *ttfFaceForSimple(int charsetId) const;
	bool ttfSizeForSimple(int charsetId, int &pixelSize, bool &lineFit) const;
	bool probeSimpleFonts(const Common::Path &gameDir, Common::Language language);
	bool loadSimpleBitmapFile(const Common::Path &gameDir, const Common::String &name, int charsetId, bool latin);
	Common::String _simpleBitmapPattern;    ///< legacy template, e.g. "hrkor%02d.fnt"
	Common::String _simpleBitmapSingle;     ///< single file used when no numbered one matches
	Common::String _simpleLatinBitmapName;  ///< Latin companion, plain name or pattern

	// --- version-2 map / compiled per-id plans ----------------------------
	mutable Graphics::HiResMap _map;
	Graphics::HiResIniOverrides _ini;
	bool _haveMap = false;
	bool _perGlyph = false;
	bool _wantsAlpha = false;
	int _scale = 1;
	Common::CodePage _encoding = Common::kCodePageInvalid;
	Common::Path _mapDir;
	Common::Path _gameDir;
	Graphics::HiResRenderTarget _target = Graphics::kHiResTargetAuto;
	Graphics::HiResIdPlan _plans[kMaxFonts];
	mutable bool _idBound[kMaxFonts] = {};       ///< checkIdOnceReady() ran its load-time checks once
	/// Paths checkIdOnceReady() refused for this id (an SVF whose cell
	/// height differs from the id's first SVF, design 5.4): ensureChainSources()
	/// must never re-resolve one of these back into the id's chain, since a
	/// null chain slot on its own is not sticky - the next call would just
	/// look the still-valid source up again.
	mutable Common::HashMap<Common::String, bool> _excludedForId[kMaxFonts];

	/// Sources for one id's compiled plan, built - and extended as faces
	/// arrive - by ensureChainSources(): chainSources[id][0] parallels
	/// _plans[id].idChain.faces, chainSources[id][i+1] parallels
	/// _plans[id].ruleChains[i].faces; targetSources[id] parallels
	/// _plans[id].targets; borrowed[id] is the nearest charset's idChain
	/// sources (chainSources[nearest][0]), for pickGlyph()'s `borrowed`.
	mutable Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > _chainSources[kMaxFonts];
	mutable Common::Array<Graphics::UnicodeGlyphSource *> _targetSources[kMaxFonts];
	mutable Common::Array<Graphics::UnicodeGlyphSource *> _borrowed[kMaxFonts];

	/// Idempotent: (re)builds _chainSources[id]/_targetSources[id]/
	/// _borrowed[id] from whatever is in _sources now, opening a TrueType
	/// entry when its pixel size can be worked out (openPlanFace()); a path
	/// still unresolved (an SVF not yet added, or a TTF whose size is not
	/// known yet) is retried on the next call. Runs checkIdOnceReady() when
	/// every path the plan names has at least been attempted. Called from
	/// addFace() (for every id) and from every draw/advance/perGlyphSourceFor
	/// entry point (for the one id being asked about).
	/// @p allowDiskOpen false (addFace()'s own eager sweep over every id)
	/// only wires up paths already known in _sources (added, or failed,
	/// via addFace()/a prior allowDiskOpen=true call): it never itself
	/// attempts to open a path from disk. That matters for a plan whose id
	/// chain names several faces added one addFace() call at a time (tests;
	/// loadFonts()'s own proactive scan): resolving a *later* chain entry
	/// against disk before it has been added would both open the wrong
	/// (real on-disk) file for a test's in-memory-only path and record a
	/// bogus permanent failure, letting checkIdOnceReady() run its once-only
	/// checks before every face is actually in.
	void ensureChainSources(int id, bool allowDiskOpen = true) const;
	/// design section 10.4's load-time checks (cell height, [glyphs] target
	/// coverage, missing=), run once per id, when ensureChainSources() finds
	/// every path the plan names has been attempted (S16b).
	void checkIdOnceReady(int id) const;
	/// Every distinct resolved path _plans[id] names (idChain, every
	/// ruleChain, every [glyphs] target face), in plan order.
	Common::Array<Common::String> collectFacePaths(int id) const;
	int nearestPlanCharset(int charsetId) const;

	/// Resolve @p code (the game's own character/charset byte) for id
	/// @p charsetId: [glyphs] (design 6.5 step 2), then the range/coverage
	/// chain and SCUMM's nearest-charset borrowing (pickGlyph(), B6). @p cp
	/// is updated to the code point that would be drawn; @p declined is set
	/// when the game's own font should draw @p code instead.
	Graphics::UnicodeGlyphSource *faceForCodePoint(int charsetId, uint32 code, uint32 &cp, bool &declined) const;

	/// Rows to move a drawn glyph down so it shares the id's own baseline
	/// (design section 8's origin=face, generalised from ASCII-only
	/// latin=baseline=face to any code point, replacing latinBaselineShift()).
	int originShiftFor(Graphics::UnicodeGlyphSource *drawn, int charsetId) const;
	/// The id's own primary source (chainSources[id][0]'s first opened
	/// entry): what originShiftFor() and nearestFont() measure against.
	Graphics::UnicodeGlyphSource *primarySourceFor(int charsetId) const;
	/// Whether @p src is a TrueType face (as opposed to an SVFN one), found
	/// by scanning _sources for the Face that owns it.
	bool isSourceTtf(Graphics::UnicodeGlyphSource *src) const;

	int advancePlaced(uint32 cp, Graphics::UnicodeGlyphSource *src, int charsetId, int gameWidth, int *carry) const;
	/// The C31 legacy grid rule (kHiResAdvanceEngine: nothing set an advance
	/// key), generalised off UnicodeGlyphSource: a bitmap face steps on the
	/// game's grid, a wide TrueType glyph by the face.
	int cellRuleAdvance(Graphics::UnicodeGlyphSource *src, uint32 cp, int charsetId, int gameWidth,
						int *carry, bool faceFit) const;

	bool drawGlyphPlaced(Graphics::Surface &dest, int chr, int charsetId,
						 int x, int y, byte color, byte shadowColor, int gameShadow,
						 Common::Rect *dirty, bool withCoverage, int gameAdvance);
	/// @p mirror flips the glyph; across, about [@p axisLeft, @p axisRight).
	bool drawRows(Graphics::Surface &dest, Graphics::UnicodeGlyphSource &src, uint32 cp, int width,
				  int x, int y, byte color, byte shadowColor, int gameShadow,
				  Common::Rect *dirty, bool withCoverage,
				  Graphics::HiResMirror mirror = Graphics::kHiResMirrorNone,
				  int axisLeft = 0, int axisRight = 0);

	/// The [shadow] fields of _map, as a throwaway HiResTextConfig for the
	/// unchanged glyphStyle()/HiResGlyphRenderer::applyMap().
	Graphics::HiResTextConfig legacyShadowConfig() const;

	// The pen after the last base glyph drawn, in overlay pixels, for a
	// combining mark that follows it.
	int _anchorX = 0;
	int _anchorY = 0;
	bool _anchorValid = false;
	// That base's box across (pen to pen plus advance): a mark of a flipped
	// charset is flipped about it, so it stays on its base.
	int _anchorAxisLeft = 0;
	int _anchorAxisRight = 0;

	// --- flipped charsets (C27) ----------------------------------------
	Graphics::HiResMirror _gameMirror[kMaxFonts];
	/// resolveMirror() for one charset of this game and map.
	Graphics::HiResMirror mirrorFor(int charsetId) const;
	/// keepsGameFont() for one charset of this game and map.
	bool keepsGameFont(int charsetId, int chr) const;
	/// Whether the id names its own replacement at all (a face chain, an
	/// `original` id, or mirror=): keepsGameFont(int,int)'s `named`.
	bool idNamesOwnFont(int charsetId) const;

	// Coverage (design section 4.4).
	Graphics::CodePointSet _translationCps;
	Common::Array<uint32> _coverageSample;
	Common::Array<uint32> _fitProbes;	///< _translationCps.fitProbes(): the TrueType faces' fit
	mutable Common::HashMap<Common::String, bool> _coverageChecked;
	mutable Common::Array<Common::String> _coverageWarnings;
	/// Map-less form only: the translation coverage warning for one merged
	/// TrueType chain (openTtfChain()).
	void checkCoverage(const Face *face, const Common::String &key) const;

	void freeFaces();

	/// The code point of one of the game's characters; 0 when it has none.
	uint32 codePointFor(int chr) const;

	/**
	 * Whether @p src has an inked glyph for @p cp: a stored glyph (cells() >
	 * 0) that is not entirely blank. A blank-but-present glyph (a control
	 * code SCUMM stores a small picture at, or a space) must be declined,
	 * not drawn as an empty box, so that the game's own picture still shows;
	 * see the older comment kept in hires_text.cpp.
	 *
	 * @param inkRight  if not null, set to the width to draw: one past the
	 *                  last inked column
	 */
	bool glyphInk(Graphics::UnicodeGlyphSource *src, uint32 cp, int *inkRight) const;

	// The grid the engine settled on per charset, so a charset with no
	// replacement of its own can fall back to a font that fits it.
	int _charsetWidths[kMaxFonts] = {};
	int _charsetHeights[kMaxFonts] = {};

	int nearestFont(int charsetId) const;
	int nearestTtfCharset(int charsetId) const;

	// Optional running log of what is being drawn, for working out which
	// scenes exercise which fonts. Off unless hires_text_log is set.
	bool _logText = false;
	/// The encoding.dat warning was given; once per engine run, so not reset().
	bool _warnedTables = false;

	int _gameFontW[kMaxFonts] = {};
	int _gameFontH[kMaxFonts] = {};
	// setGameFontCell() was given a CJK font's cell: text is laid out on it,
	// and the space is the word gap of that script (C36: kept at the game's).
	bool _cjkCells = false;
	// The charset renderer measures single-byte text through advanceFor()
	// (setLatinFaceStepAllowed()).
	bool _latinFaceStepAllowed = true;
	void noteDrawn(int charsetId, const Face *face, int chr) const;
	void flushTextLog() const;

	mutable Common::String _logRun;
	mutable int _logCharset = -1;
	mutable const Face *_logFace = nullptr;
	mutable bool _fontsLoaded;

	// In alpha mode the backend is given no palette, so we keep our own: the
	// packed colour for compositing, and the RGB triples the cursor needs.
	bool _alphaActive;
	bool _korPatchShadow;
	uint32 _paletteCache[256];
	byte _paletteRGB[3 * 256];
};

} // End of namespace Scumm

#endif
