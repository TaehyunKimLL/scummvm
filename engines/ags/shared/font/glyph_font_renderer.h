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


#ifndef AGS_SHARED_FONT_GLYPH_FONT_RENDERER_H
#define AGS_SHARED_FONT_GLYPH_FONT_RENDERER_H

#include "common/array.h"
#include "common/std/map.h"
#include "ags/lib/std.h"
#include "ags/shared/font/ags_font_renderer.h"
#include "ags/shared/font/glyph_font_draw.h"
#include "ags/shared/font/hires_font_config.h"

namespace Graphics {
class UnicodeGlyphSource;
}

namespace AGS3 {

/**
 * An AGS font renderer over any Graphics::UnicodeGlyphSource: a TrueType
 * chain from hires_text.map (FallbackGlyphSource of TtfGlyphSources) or an
 * SVFN bitmap font (SvfnGlyphSource). Glyphs are placed per glyph with
 * zero-advance combining marks and drawn with coverage alpha
 * (GlyphTextDrawer). A code point the map's fonts lack is drawn by the
 * game's own renderer of the same font, which stays loaded under the same
 * font number (I18N_TEXT_DESIGN.md section 4.4: "next map font, then the
 * game's own renderer").
 */
class GlyphFontRenderer : public IAGSFontRendererInternal {
public:
	virtual ~GlyphFontRenderer();

	/**
	 * Take font N over from @p game (which has loaded it) as @p plan says.
	 * @p gameHeight is the game font's height, the size when the plan names
	 * none. False (with one warning) when no face or file of the plan can
	 * be used: the game's renderer then keeps the font.
	 */
	bool Attach(int fontNumber, const HiResFontPlan &plan, int gameHeight, bool alpha,
				IAGSFontRendererInternal *game, const FontRenderParams &params);

	/** Whether the game's own renderer of font N draws a bitmap (WFN)
	 *  font: the outline and anti-aliasing rules keep following the game's
	 *  font (is_bitmap_font()). */
	bool IsGameBitmapFont(int fontNumber);

	/** A UTF-8 translation was loaded: fit each TrueType chain to its code
	 *  points and warn, once per face, about what a face lacks (coverage.h). */
	void SetTranslationSample(const Common::Array<uint32> &sample);

	// IAGSFontRenderer implementation
	bool LoadFromDisk(int fontNumber, int fontSize) override { return false; }
	void FreeMemory(int fontNumber) override;
	bool SupportsExtendedCharacters(int fontNumber) override { return true; }
	int GetTextWidth(const char *text, int fontNumber) override;
	int GetTextHeight(const char *text, int fontNumber) override;
	void RenderText(const char *text, int fontNumber, BITMAP *destination, int x, int y, int colour) override;
	void AdjustYCoordinateForFont(int *ycoord, int fontNumber) override {}
	void EnsureTextValidForFont(char *text, int fontNumber) override {}

	// IAGSFontRenderer2 implementation
	int GetVersion() override { return 26; }
	const char *GetRendererName() override { return "GlyphFontRenderer"; }
	const char *GetFontName(int fontNumber) override;
	int GetFontHeight(int fontNumber) override;
	int GetLineSpacing(int fontNumber) override { return 0; }

	// IAGSFontRendererInternal implementation
	bool IsBitmapFont() override { return false; }
	bool LoadFromDiskEx(int fontNumber, int fontSize, AGS::Shared::String *src_filename,
						const FontRenderParams *params, FontMetrics *metrics) override { return false; }
	void GetFontMetrics(int fontNumber, FontMetrics *metrics) override;
	void AdjustFontForAntiAlias(int fontNumber, bool aa_mode) override;

private:
	struct FontData;
	/** The game's own renderer, one character at a time. */
	class GameFallback : public GlyphFallback {
	public:
		GameFallback() : _game(nullptr), _font(0), _dst(nullptr) {}
		void set(IAGSFontRenderer *game, int font) { _game = game; _font = font; }
		void target(BITMAP *dst) { _dst = dst; }
		int charWidth(uint32 cp) override;
		void drawChar(uint32 cp, int x, int y, uint32 colour) override;
	private:
		IAGSFontRenderer *_game;
		int _font;
		BITMAP *_dst;
	};

	struct FontData {
		FontData() : Source(nullptr), Game(nullptr), Size(0) {}
		Graphics::UnicodeGlyphSource *Source;	///< owned; the chain's FallbackGlyphSource, or the one source
		Common::Array<Graphics::UnicodeGlyphSource *> Chain;	///< not owned: the faces, in order
		Common::Array<AGS::Shared::String> Names;
		HiResFontPlan Plan;
		IAGSFontRendererInternal *Game;
		FontRenderParams Params;
		int Size;								///< pixels the faces were opened at
		GlyphTextDrawer Drawer;
		GameFallback Fallback;
		AGS::Shared::String Name;
	};

	bool Build(FontData &fd, const Common::Array<uint32> &fitProbes, bool warn);
	static void FreeSources(FontData &fd);
	void Decode(const char *text);

	std::map<int, FontData *> _fontData;
	Common::Array<uint32> _cps;				///< the text being measured or drawn, as code points
};

} // namespace AGS3

#endif
