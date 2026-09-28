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

#ifndef GRAPHICS_KORFONT_H
#define GRAPHICS_KORFONT_H

#include "common/str.h"
#include "common/stream.h"
#include "graphics/surface.h"

namespace Graphics {

/**
 * @defgroup graphics_fontman Korean font
 * @ingroup graphics
 *
 * @brief FontKorean class used to handle Korean characters.
 *
 * @{
 */

/**
 * A font that is able to draw Korean encoded characters.
 */
class FontKorean {
public:
	virtual ~FontKorean() {}

	/**
	 * Creates the first Korean font, which ROM/font file is present.
	 * It will also call loadData, so the user can just start
	 * using the font.
	 *
	 * The last file tried is ScummVM's Korean.FNT file.
	 */
	static FontKorean *createFont(const char * fontFile);

	/**
	 * Load the font data.
	 */
	virtual bool loadData(const char *fontFile) = 0;

	/**
	 * Enable drawing with outline or shadow if supported by the Font.
	 *
	 * After changing outline state, getFontHeight and getMaxFontWidth / getCharWidth might return
	 * different values!
	 */
	enum DrawingMode {
		kDefaultMode,
		kOutlineMode,
		kShadowMode
	};

	virtual void setDrawingMode(DrawingMode mode) {}

	/**
	 * Enable flipped character drawing if supported by the Font (e.g. in the MI1 circus scene after Guybrush gets shot out of the cannon).
	 */
	virtual void toggleFlippedMode(bool enable) {}

	/**
	 * Set spacing between characters and lines. This affects font height / char width
	 */
	virtual void setCharSpacing(int spacing) {}
	virtual void setLineSpacing(int spacing) {}

	/**
	 * Returns the height of the font.
	 */
	virtual uint getFontHeight() const = 0;

	/**
	 * Returns the max. width of the font.
	 */
	virtual uint getMaxFontWidth() const = 0;

	/**
	 * Returns the width of a specific character.
	 */
	virtual uint getCharWidth(uint16 ch) const = 0;

	/**
	 * Draws a Korean encoded character on the given surface.
	 */
	void drawChar(Graphics::Surface &dst, uint16 ch, int x, int y, uint32 c1, uint32 c2) const;

	/**
	 * Draws a Korean char on the given raw buffer.
	 *
	 * @param dst   pointer to the destination
	 * @param ch    character to draw (in little endian)
	 * @param pitch pitch of the destination buffer (size in *bytes*)
	 * @param bpp   bytes per pixel of the destination buffer
	 * @param c1    forground color
	 * @param c2    outline color
	 * @param maxW  max draw width (to ensure that character drawing takes place within surface boundaries), -1 = no check
	 * @param maxH  max draw height (to ensure that character drawing takes place within surface boundaries), -1 = no check
	 */
	virtual void drawChar(void *dst, uint16 ch, int pitch, int bpp, uint32 c1, uint32 c2, int maxW, int maxH) const = 0;
};

/**
 * A base class to render monochrome Korean fonts.
 */
class FontKoreanBase : public FontKorean {
public:
	FontKoreanBase();

	void setDrawingMode(DrawingMode mode) override;

	void toggleFlippedMode(bool enable) override;

	uint getFontHeight() const override;

	uint getMaxFontWidth() const override;

	uint getCharWidth(uint16 ch) const override;

	void drawChar(void *dst, uint16 ch, int pitch, int bpp, uint32 c1, uint32 c2, int maxW, int maxH) const override;
protected:
	/**
	 * Width in pixels of the stored bitmap of an ASCII glyph (the width the
	 * glyph is blitted at, not its advance). Half the Hangul cell unless a
	 * font stores wider Latin glyphs.
	 */
	virtual int getASCIIGlyphWidth() const { return _fontWidth / 2; }

	/** Extra advance the current drawing mode adds to a glyph. */
	int drawModeExtraWidth() const;
private:
	template<typename Color>
	void blitCharacter(const uint8 *glyph, const int w, const int h, uint8 *dst, int pitch, Color c) const;
	void createOutline(uint8 *outline, const uint8 *glyph, const int w, const int h) const;

protected:
	DrawingMode _drawMode;
	bool _flippedMode;
	int _fontWidth, _fontHeight;
	uint8 _bitPosNewLineMask;

	bool isASCII(uint16 ch) const;

	virtual const uint8 *getCharData(uint16 c) const = 0;

	enum DrawingFeature {
		kFeatDefault        = 1 << 0,
		kFeatOutline        = 1 << 1,
		kFeatShadow         = 1 << 2,
		kFeatFMTownsShadow  = 1 << 3,
		kFeatFlipped        = 1 << 4
	};

	virtual bool hasFeature(int feat) const = 0;
};

/**
 * Our custom Korean FNT.
 */
class FontKoreanSVM : public FontKoreanBase {
public:
	FontKoreanSVM();
	~FontKoreanSVM();
	/**
	 * Load the font data from "KOREAN.FNT".
	 */
	bool loadData(const char *fontFile) override;

	/**
	 * Load the font data from a stream (does not take ownership). Accepts
	 * format versions 3 and 4:
	 *  - v3: 18-byte header, 16x16 Hangul glyphs, then 8x16 Latin glyphs
	 *    stored at 2 bytes per row of which only the first is used (8 px,
	 *    fixed advance of half a cell), then 8x8 glyphs.
	 *  - v4: the same header, then a 128-byte table of per-ASCII-code
	 *    advances in pixels, then the same glyph blocks - except that the
	 *    Latin glyphs use both bytes of each row (up to 16 px wide,
	 *    proportional, advanced by the table).
	 */
	bool loadFromStream(Common::SeekableReadStream &data);

	uint getCharWidth(uint16 ch) const override;

	/** The format version of the loaded file (3 or 4), 0 before a load. */
	uint32 getVersion() const { return _version; }

	/** True when the font carries proportional Latin glyphs (version 4). */
	bool hasProportionalLatin() const { return _version >= 4; }

	/** The advance a v4 font's table gives an ASCII code, in font pixels (0 for v3). */
	uint8 getLatinAdvance(uint16 ch) const { return (_version >= 4 && ch < 128) ? _latinAdvance[ch] : 0; }

protected:
	int getASCIIGlyphWidth() const override;

private:
	uint32 _version;
	uint8 _latinAdvance[128];
	uint _latinRowBytes;	///< bytes kept per Latin glyph row: 1 (v3) or 2 (v4)

	uint8 *_fontData16x16;
	uint _fontData16x16Size;

	uint8 *_fontData8x16;
	uint _fontData8x16Size;

	uint8 *_fontData8x8;
	uint _fontData8x8Size;

	const uint8 *getCharData(uint16 c) const override;

	bool hasFeature(int feat) const override;

	const uint8 *getCharDataPCE(uint16 c) const;
	const uint8 *getCharDataDefault(uint16 c) const;

	enum {
		kKoreanFontVersion = 3,	///< the original format
		kKoreanFontVersionProportional = 4	///< adds proportional Latin (width table, 16 px rows)
	};
};

/**
 * Korean Wansung compatible font.
 */
class FontKoreanWansung : public FontKoreanBase {
public:
	FontKoreanWansung();
	~FontKoreanWansung();
	/**
	 * Loads the ROM data from "KOREAN#.FNT".
	 */
	bool loadData(const char *fontFile) override;
private:
	enum {
		eFontNumChars = 256,
		kFontNumChars = 2530
	};

	int _fontShadow;
	uint8 *_fontData;
	uint _fontDataSize;

	int _englishFontWidth;
	int _englishFontHeight;
	uint8 *_englishFontData;
	uint _englishFontDataSize;


	const uint8 *getCharData(uint16 c) const override;

	bool hasFeature(int feat) const override;

	bool englishLoadData(const char *fontFile);
};
 /** @} */
} // End of namespace Graphics

#endif
