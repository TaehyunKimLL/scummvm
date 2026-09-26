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

#ifndef GRIM_FONT_H
#define GRIM_FONT_H

#include "engines/grim/pool.h"

#include "common/array.h"
#include "common/ustr.h"

#include "graphics/font.h"
#include "graphics/pixelformat.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {
class SvfnGlyphSource;
}

namespace Grim {

class SaveGame;

class Font {
public:
	virtual ~Font() {}

	virtual int32 getKernedHeight() const = 0;
	virtual int32 getFontWidth() const = 0;
	virtual int getKernedStringLength(const Common::String &text) const = 0;
	virtual int32 getBaseOffsetY() const = 0;
	virtual void render(Graphics::Surface &buf, const Common::String &currentLine, const Graphics::PixelFormat &pixelFormat, uint32 blackColor, uint32 color, uint32 colorKey) const = 0;
	virtual int32 getCharKernedWidth(uint32 c) const = 0;
	virtual int getPoolId() const = 0;
	virtual int32 getPoolTag() const = 0;
	virtual bool is8Bit() const = 0;
	const Common::String &getFilename() const { return _filename; }

	/**
	 * Render a line as colour plus coverage: RGB = (r, g, b), alpha = how much
	 * of the pixel the text covers, in @p format (4 bytes per pixel, 8-bit
	 * alpha). The TinyGL and shader renderers blend such a line instead of
	 * colour-keying it. Returns false for a font that has no coverage (the
	 * bitmap .laf fonts); the caller then uses render().
	 */
	virtual bool renderAlpha(Graphics::Surface &buf, const Common::String &currentLine, const Graphics::PixelFormat &format, byte r, byte g, byte b) const { return false; }

	// for Korean Translate
	int32 getWCharKernedWidth(byte hi, byte lo) const { return getCharKernedWidth(hi) + getCharKernedWidth(lo); }
	bool isKoreanChar(const byte hi, const byte lo) const { return (hi >= 0xB0 && hi <= 0xC8 && lo >= 0xA1 && lo <= 0xFE); }

	static Font *getByFileName(const Common::String& fileName);
	static Font *getFirstFont();
	static void save(const Font *font, SaveGame *state);
	static Font *load(SaveGame *state);

protected:
	Common::String _filename;
};

class BitmapFont : public Font, public PoolObject<BitmapFont> {
public:
	BitmapFont();
	~BitmapFont();

	static int32 getStaticTag() { return MKTAG('F', 'O', 'N', 'T'); }
	int getPoolId() const override { return getId(); }
	int32 getPoolTag() const override { return getStaticTag(); }

	void load(const Common::String &filename, Common::SeekableReadStream *data);
	void loadTGA(const Common::String &filename, Common::SeekableReadStream *index, Common::SeekableReadStream *image);

	const Common::String &getFilename() const { return _filename; }
	int32 getKernedHeight() const override { return _kernedHeight; }
	int32 getFontWidth() const override { return getCharKernedWidth('w'); }
	int32 getBaseOffsetY() const override { return _baseOffsetY; }
	void render(Graphics::Surface &buf, const Common::String &currentLine, const Graphics::PixelFormat &pixelFormat, uint32 blackColor, uint32 color, uint32 colorKey) const override;
	int32 getCharBitmapWidth(uint32 c) const { return _charHeaders[getCharIndex(c)].bitmapWidth; }
	int32 getCharBitmapPitch(uint32 c) const { return _charHeaders[getCharIndex(c)].bitmapPitch; }
	int32 getCharBitmapHeight(uint32 c) const { return _charHeaders[getCharIndex(c)].bitmapHeight; }
	int32 getCharKernedWidth(uint32 c) const override { return _charHeaders[getCharIndex(c)].kernedWidth; }
	int32 getCharStartingCol(uint32 c) const { return _charHeaders[getCharIndex(c)].startingCol; }
	int32 getCharStartingLine(uint32 c) const { return _charHeaders[getCharIndex(c)].startingLine; }
	int32 getCharOffset(uint32 c) const { return _charHeaders[getCharIndex(c)].offset; }
	const byte *getCharData(uint32 c) const { return _fontData + (_charHeaders[getCharIndex(c)].offset); }

	const byte *getFontData() const { return _fontData; }
	uint32 getDataSize() const { return _dataSize; }
	bool is8Bit() const override;

	int getKernedStringLength(const Common::String &text) const override;
	int getBitmapStringLength(const Common::String &text) const;
	int getStringHeight(const Common::String &text) const;

	const void *getUserData() const { return _userData; }
	void setUserData(void *data) { _userData = data; }

	void saveState(SaveGame *state) const;
	void restoreState(SaveGame *state);

	static const uint8 emerFont[][13];
private:
	uint32 getNextChar(const Common::String &text, uint32 &i) const;
	uint16 getCharIndex(uint32 c) const;
	struct CharHeader {
		int32 offset;
		int8  kernedWidth;
		int32 startingCol;
		int32 startingLine;
		int32 bitmapWidth;
		int32 bitmapPitch;
		int32 bitmapHeight;
	};

	uint32 _numChars;
	uint32 _dataSize;
	uint32 _kernedHeight, _baseOffsetY;
	uint32 _firstChar, _lastChar;
	Common::Array<int> _fwdCharIndex;
	CharHeader *_charHeaders;
	byte *_fontData;
	void *_userData;
	bool _isDBCS, _isUnicode;
};

class FontTTF : public Font, public PoolObject<FontTTF> {
public:
	FontTTF();
	~FontTTF();

	void loadTTF(const Common::String &filename, Common::SeekableReadStream *data, int size);
	void loadTTFFromArchive(const Common::String &filename, int size);

	/**
	 * Load the face a Korean `<font>.laf.txt` names: "face.ttf 17px" (a
	 * TrueType face at that pixel size) or "face.svfn 16px" (an SVFN bitmap
	 * font; its own cell size wins). Returns false after one warning when the
	 * descriptor names nothing this build can draw - no FreeType for a TTF,
	 * a missing or corrupt file - and the caller then uses the bitmap .laf.
	 */
	bool loadFromDescriptor(const Common::String &filename, Common::SeekableReadStream *descriptor);

	static int32 getStaticTag() { return MKTAG('T', 'T', 'F', ' '); }
	int getPoolId() const override { return getId(); }
	int32 getPoolTag() const override { return getStaticTag(); }

	int32 getKernedHeight() const override;
	int32 getBaseOffsetY() const override;
	int32 getCharKernedWidth(uint32 c) const override;
	int32 getFontWidth() const override { return getCharKernedWidth('w'); }

	int getKernedStringLength(const Common::String &text) const override;
	void render(Graphics::Surface &buf, const Common::String &currentLine, const Graphics::PixelFormat &pixelFormat, uint32 blackColor, uint32 color, uint32 colorKey) const override;
	bool renderAlpha(Graphics::Surface &buf, const Common::String &currentLine, const Graphics::PixelFormat &format, byte r, byte g, byte b) const override;
	bool is8Bit() const override { return false; }

	void saveState(SaveGame *state) const;
	void restoreState(SaveGame *state);

	// for Korean Translate
	int32 getWCharKernedWidth(byte hi, byte lo) const { return getCharKernedWidth(Common::convertUHCToUCS(hi, lo)); }

private:
	/** Korean lines are CP949, or UTF-8 when grim.ko.tab carries a UTF-8 BOM. */
	Common::U32String decodeKorean(const Common::String &text) const;
	/** Coverage of a line drawn from the SVFN font, w*h bytes. */
	void svfnCoverage(const Common::U32String &text, Common::Array<byte> &coverage, int &w, int &h) const;
	int32 svfnAdvance(uint32 cp) const;
	/** Draw with the bitmap font @p filename (restoring a save whose face cannot be loaded). */
	void useBitmapFallback(const Common::String &filename);

	Graphics::Font *_font;
	Graphics::SvfnGlyphSource *_svfn;
	BitmapFont::Ptr _fallback;
	bool _isUnicode;
	int _size;
};

} // end of namespace Grim

#endif
