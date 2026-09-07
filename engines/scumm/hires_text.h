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

#include "common/language.h"
#include "common/path.h"
#include "common/rect.h"
#include "graphics/hires_text/glyph_renderer.h"
#include "graphics/pixelformat.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

namespace Scumm {

/**
 * The engine's side of the hi-res text layer.
 *
 * Everything that knows about SCUMM lives here; everything that does not lives
 * in graphics/hires_text. The split is what lets a second engine reuse the
 * font handling without inheriting SCUMM's screen model.
 *
 * This is state and policy only. Loading fonts, decoding strings and drawing
 * are added in later steps; until then nothing here changes what reaches the
 * screen.
 */
struct ScummHiResText {
	ScummHiResText();

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
	 * With a map, or a user setting, this is a no-op.
	 *
	 * @param gameFontHeight  the height of the game's own CJK font, in game
	 *                        pixels; 0 when it has none
	 */
	void resolveScale(int gameFontHeight);

	/**
	 * Tell the layer the cell of the game's own CJK font for one charset.
	 *
	 * Needed by the TrueType path, which bakes a face to the size the game
	 * draws each charset at. Call once per loaded game font, before
	 * loadFonts().
	 */
	void setGameFontCell(int charsetId, int width, int height);

	void reset();

	/**
	 * Whether the hi-res path should be used at all.
	 *
	 * False here has to leave the engine on exactly its original path: this
	 * is the switch that keeps every game we do not touch untouched.
	 */
	bool enabled() const { return _enabled; }

	int scale() const { return _config.scale; }
	bool wantsAlpha() const { return _config.alpha; }

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
	 * How far the pen should move after drawing a character.
	 *
	 * The game decides line breaks and speech-bubble sizes from the widths of
	 * its own font, so a replacement that advances differently can push text
	 * out of a bubble or wrap it in the wrong place. The map therefore chooses:
	 * metrics=game keeps the original advance and merely draws a better glyph
	 * in the same box, metrics=font lets a proportional replacement space
	 * itself properly.
	 *
	 * @param chr        the character, in the game's own encoding
	 * @param charsetId  the game's current charset number
	 * @param gameWidth  what the engine's own font would have advanced
	 * @return the advance to use, in unscaled game pixels
	 */
	/**
	 * How far to step after drawing a character, in game pixels.
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
	 * @param gameWidth  the game's own advance, returned unchanged when there
	 *                   is no replacement glyph or metrics=game leaves the
	 *                   layout alone
	 */
	int advanceFor(int chr, int charsetId, int gameWidth,
				   int *carry = nullptr) const;

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


	Common::CodePage encoding() const { return _config.encoding; }

	const Graphics::HiResTextConfig &config() const { return _config; }

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
	 * Allocate the coverage surface, if this configuration wants one.
	 *
	 * The colour of a glyph goes to the engine's own text surface; a paletted
	 * surface has nowhere to put the coverage that makes it anti-aliased, so
	 * that goes here and the compositing step blends the two.
	 *
	 * @param w, h  size of the text surface it accompanies
	 */
	/**
	 * Load the replacement fonts the map named.
	 *
	 * @param gameDir  where a relative font name is looked for
	 * @return false when nothing usable loaded, leaving the engine on its
	 *         original path
	 */
	bool loadFonts(const Common::Path &gameDir);

	/**
	 * The replacement font for one of the game's charsets.
	 *
	 * A game swaps charset in the middle of a scene - dialogue, the verb
	 * line and a title card are different sizes - so the map names a
	 * numbered set and each entry is baked for one of them.
	 *
	 * @param charsetId  the game's own charset number
	 * @return null when nothing covers it, i.e. draw it the original way
	 */
	const Graphics::HiResBitmapFont *fontFor(int charsetId, bool latin = false) const;

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
	 * @return false when nothing was drawn and the caller must fall back
	 */
	bool drawChar(Graphics::Surface &dest, int chr, int charsetId,
				  int x, int y, byte color, byte shadowColor,
				  int gameShadow, Common::Rect *dirty = nullptr);

	void createCoverage(int w, int h);

	void freeCoverage();

	/// The coverage surface, or null when this configuration has none.
	Graphics::Surface *coverage() { return _coverage.getPixels() ? &_coverage : nullptr; }
	const Graphics::Surface *coverage() const { return _coverage.getPixels() ? &_coverage : nullptr; }

	/// Wipe the coverage, so nothing of the previous frame's text blends in.
	/**
	 * Clear the coverage, or one horizontal band of it.
	 *
	 * The band matches a scoped clearTextSurface(): the verb strip and the
	 * dialogue live on the same surface but are retired at different times.
	 */
	void clearCoverage(int top = 0, int height = -1);

	/**
	 * How the engine's own strings are encoded.
	 *
	 * Set from the detected language unless a map overrides it. Invalid means
	 * the game's text is single byte in an encoding nothing has named, which
	 * is the safe assumption for the games we do not touch.
	 */
	void setEncoding(Common::CodePage page) { _config.encoding = page; }

private:
	bool _enabled;
	bool _simpleFonts = false;      ///< fonts found by name, with no map
	int _simpleCellHeight = 0;      ///< smallest cell among them, for the scale
	bool _scaleFromUser = false;
	Graphics::HiResTextConfig _config;
	Graphics::Surface _coverage;

	// The numbered set the map names, indexed by the game's charset id, plus
	// the single one used when no numbered file matched.
	static const int kMaxFonts = 20;
	Graphics::HiResBitmapFont _fonts[kMaxFonts];
	Graphics::HiResBitmapFont _singleFont;

	// Latin text goes through the same printChar() path as CJK, so it can have
	// a hi-res font too - the game's own 8px letters look coarse next to a
	// scaled replacement. A separate font because the CJK sets index by a
	// double-byte code page and carry no Latin glyphs.
	Graphics::HiResBitmapFont _latinFont;

	// Per-charset Latin faces, when the map names a pattern. A game can use a
	// different cell per charset - MI2 has five - and a Latin face at the
	// wrong cell sits on a different baseline from the Hangul beside it.
	Graphics::HiResBitmapFont _latinFonts[kMaxFonts];

	// The grid the engine settled on per charset, so a charset with no
	// replacement of its own can fall back to a font that fits it.
	int _charsetWidths[kMaxFonts] = {};
	int _charsetHeights[kMaxFonts] = {};

	int nearestFont(int charsetId) const;

	// Optional running log of what is being drawn, for working out which
	// scenes exercise which fonts. Off unless hires_text_log is set.
	bool _logText = false;

	bool probeSimpleFonts(const Common::Path &gameDir);
	bool bakeTtfFonts(const Common::Path &gameDir);

	Common::Path _ttfPath;          ///< face to bake at run time, if any
	int _gameFontW[kMaxFonts] = {};
	int _gameFontH[kMaxFonts] = {};
	void noteDrawn(int charsetId, const Graphics::HiResBitmapFont *font, int chr) const;
	void flushTextLog() const;

	mutable Common::String _logRun;
	mutable int _logCharset = -1;
	mutable int _logFont = -1;
	bool _fontsLoaded;

	/// Decode one of the game's characters and look it up; -1 when absent.
	int glyphIndexFor(const Graphics::HiResBitmapFont &font, int chr) const;

	// In alpha mode the backend is given no palette, so we keep our own: the
	// packed colour for compositing, and the RGB triples the cursor needs.
	bool _alphaActive;
	uint32 _paletteCache[256];
	byte _paletteRGB[3 * 256];
};

} // End of namespace Scumm

#endif
