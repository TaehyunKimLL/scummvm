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

#include "common/endian.h"

#include "graphics/fonts/ttf.h"
#include "graphics/font.h"
#include "graphics/surface.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/font_descriptor.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/text_compose.h"

#include "image/tga.h"

#include "engines/grim/debug.h"
#include "engines/grim/grim.h"
#include "engines/grim/savegame.h"
#include "engines/grim/font.h"
#include "engines/grim/resource.h"
#include "engines/grim/gfx_base.h"

namespace Grim {

void Font::save(const Font *font, SaveGame *state) {
	const FontTTF *ttf = dynamic_cast<const FontTTF *>(font);
	if (ttf) {
		state->writeLESint32(-2);
		state->writeLESint32(ttf->getId());
		return;
	}
	const BitmapFont *bitmapFont = dynamic_cast<const BitmapFont *>(font);
	if (bitmapFont) {
		state->writeLESint32(bitmapFont->getId());
		return;
	}
	state->writeLESint32(-1);
}

Font *Font::load(SaveGame *state) {
	int32 fontId = state->readLESint32();
	if (fontId == -1) {
		return nullptr;
	}
	if (fontId == -2) {
		fontId = state->readLESint32();
		return FontTTF::getPool().getObject(fontId);
	}

	return BitmapFont::getPool().getObject(fontId);
}

Font *Font::getByFileName(const Common::String& fontName) {
	for (Font *f : BitmapFont::getPool()) {
		if (f->getFilename() == fontName) {
			return f;
		}
	}
	for (Font *f : FontTTF::getPool()) {
		if (f->getFilename() == fontName) {
			return f;
		}
	}
	return nullptr;
}

Font *Font::getFirstFont() {
	if (BitmapFont::getPool().begin() != BitmapFont::getPool().end())
		return *BitmapFont::getPool().begin();
	if (FontTTF::getPool().begin() != FontTTF::getPool().end())
		return *FontTTF::getPool().begin();
	return nullptr;
}

bool BitmapFont::is8Bit() const {
	return !_isDBCS && !_isUnicode;
}

BitmapFont::BitmapFont() :
		_userData(nullptr),
		_fontData(nullptr), _charHeaders(nullptr),
		_numChars(0), _dataSize(0), _kernedHeight(0), _baseOffsetY(0),
		_firstChar(0), _lastChar(0), _isUnicode(false), _isDBCS(false) {
}

BitmapFont::~BitmapFont() {
	_fwdCharIndex.clear();
	delete[] _charHeaders;
	delete[] _fontData;
	g_driver->destroyFont(this);
}

void BitmapFont::load(const Common::String &filename, Common::SeekableReadStream *data) {
	_filename = filename;
	_numChars = data->readUint32LE();
	_dataSize = data->readUint32LE();
	_kernedHeight = data->readUint32LE();
	_baseOffsetY = data->readUint32LE();
	data->seek(24, SEEK_SET);
	_firstChar = data->readUint32LE();
	_lastChar = data->readUint32LE();

	_isDBCS = g_grim->getGameLanguage() == Common::ZH_CHN && _numChars > 0xff;

	if (_isDBCS) {
		// Read character indexes - are the key/value reversed?
		_fwdCharIndex.resize(_numChars);
		for (uint i = 0; i < _numChars; ++i) {
			uint16 point = data->readUint16LE();
			_fwdCharIndex[i] = point ? point : -1;
		}
	} else {
		// Read character indexes - are the key/value reversed?
		Common::Array<uint16> revCharIndex;
		revCharIndex.resize(_numChars);
		uint16 maxPoint = 0;
		for (uint i = 0; i < _numChars; ++i) {
			uint16 point = data->readUint16LE();
			revCharIndex[i] = point;
			if (point > maxPoint)
				maxPoint = point;
		}
		_fwdCharIndex.resize(maxPoint + 1, -1);
		for (uint i = 0; i < _numChars; ++i) {
			_fwdCharIndex[revCharIndex[i]] = i;
		}

		// In order to ensure the correct character codes for
		// accented characters it is necessary to check the
		// requested code against the index of characters for
		// the font.  Previously, signed characters were
		// causing the problem but it might be possible for
		// an invalid character to be called for other reasons.
		//
		// Example: Without this fix when Manny greets Eva
		// for the first time and he says "Buenos Días" the
		// 'í' character will either show up as a different
		// character or it crashes the game.
		for (uint i = 0; i < _numChars; ++i) {
			if (revCharIndex[i] == i)
				_fwdCharIndex[revCharIndex[i]] = i;
		}
	}

	// Read character headers
	_charHeaders = new CharHeader[_numChars];
	for (uint i = 0; i < _numChars; ++i) {
		_charHeaders[i].offset = data->readUint32LE();
		// Kerned character size
		_charHeaders[i].kernedWidth = data->readSByte();
		_charHeaders[i].startingCol = data->readSByte();
		_charHeaders[i].startingLine = data->readSByte();
		data->seek(1, SEEK_CUR);
		// Character bitmap size
		_charHeaders[i].bitmapPitch = _charHeaders[i].bitmapWidth = data->readUint32LE();
		_charHeaders[i].bitmapHeight = data->readUint32LE();
	}
	// Read font data
	_fontData = new byte[_dataSize];

	data->read(_fontData, _dataSize);

	g_driver->createFont(this);
}

void BitmapFont::loadTGA(const Common::String &filename, Common::SeekableReadStream *index, Common::SeekableReadStream *image) {
  	Image::TGADecoder dec;
	bool success = dec.loadStream(*image);

	if (!success)
		return;

	const Graphics::Surface *surf = dec.getSurface();

	const int MAX_16BIT_CHARACTER = 0xffff;
	const int INDEX_ENTRY_SIZE = 10;

	_filename = filename;
	_numChars = index->size() / INDEX_ENTRY_SIZE;
	_dataSize = surf->w * surf->h;
	_kernedHeight = 16;
	_baseOffsetY = 0;
	_firstChar = 0;
	_lastChar = MAX_16BIT_CHARACTER;

	_isDBCS = false;
	_isUnicode = true;

	// Read character headers
	_charHeaders = new CharHeader[_numChars];

	_fwdCharIndex.resize(MAX_16BIT_CHARACTER + 1, -1);
	for (uint i = 0; i < _numChars; ++i) {
		uint16 point = index->readUint16LE();
		uint32 x = index->readUint32LE();
		uint32 y = index->readUint32LE();
		_fwdCharIndex[point] = i;
		_charHeaders[i].offset = x + y * surf->w;
		_charHeaders[i].kernedWidth = 16;
		_charHeaders[i].startingCol = 0;
		_charHeaders[i].startingLine = 0;
		_charHeaders[i].bitmapWidth = 16;
		_charHeaders[i].bitmapHeight = 16;
		_charHeaders[i].bitmapPitch = surf->w;
	}
	// Read font data
	_fontData = new byte[_dataSize];

	for (int y = 0; y < surf->h; y++)
		for (int x = 0; x < surf->w; x++)
			_fontData[y * surf->w + x] = surf->getPixel(x, y) ? 0 : 0xff;

	g_driver->createFont(this);
}

uint16 BitmapFont::getCharIndex(uint32 c) const {
	int res = c < _fwdCharIndex.size() ? _fwdCharIndex[c] : -1;
	if (res >= 0)
		return res;
	Debug::warning(Debug::Fonts, "The requested character (code 0x%x) does not correspond to anything in the font data!", c);
	// If we couldn't find the character then default to
	// the first character in the font so that something
	// gets loaded to prevent the game from crashing
	return 0;
}

uint32 BitmapFont::getNextChar(const Common::String &text, uint32 &i) const {
	if (_isUnicode) {
		uint32 chr = 0;
		uint num = 1;

		if ((text[i] & 0xF8) == 0xF0) {
			num = 4;
		} else if ((text[i] & 0xF0) == 0xE0) {
			num = 3;
		} else if ((text[i] & 0xE0) == 0xC0) {
			num = 2;
		}

		if (text.size() - i < num) {
			i = text.size();
			return '?';
		}

		switch (num) {
		case 4:
			chr |= (text[i++] & 0x07) << 18;
			chr |= (text[i++] & 0x3F) << 12;
			chr |= (text[i++] & 0x3F) << 6;
			chr |= (text[i++] & 0x3F);
			break;

		case 3:
			chr |= (text[i++] & 0x0F) << 12;
			chr |= (text[i++] & 0x3F) << 6;
			chr |= (text[i++] & 0x3F);
			break;

		case 2:
			chr |= (text[i++] & 0x1F) << 6;
			chr |= (text[i++] & 0x3F);
			break;

		default:
			chr = (text[i++] & 0x7F);
			break;
		}

		return chr;
	}
	uint16 ch = uint8(text[i]);
	if (_isDBCS && i + 1 < text.size() && (ch & 0x80)) {
		ch = (ch << 8) | (text[++i] & 0xff);
	}
	i++;
	return ch;
}

int BitmapFont::getKernedStringLength(const Common::String &text) const {
	int result = 0;
	for (uint32 i = 0; i < text.size(); ) {
		result += getCharKernedWidth(getNextChar(text, i));
	}
	return result;
}

int BitmapFont::getBitmapStringLength(const Common::String &text) const {
	int result = 0;
	const uint size = text.size();
	for (uint32 i = 0; i < size; ) {
		const uint32 ch = getNextChar(text, i);
		result += getCharStartingCol(ch);
		result += (i >= size) ? getCharBitmapWidth(ch) : getCharKernedWidth(ch);
	}
	return result;
}

int BitmapFont::getStringHeight(const Common::String &text) const {
	int result = 0;
	for (uint32 i = 0; i < text.size(); ) {
		uint32 ch = getNextChar(text, i);

		int verticalOffset = getCharStartingLine(ch) + getBaseOffsetY();
		int charHeight = verticalOffset + getCharBitmapHeight(ch);
		if (charHeight > result)
			result = charHeight;
	}
	return result;
}

void BitmapFont::saveState(SaveGame *state) const {
	state->writeString(getFilename());
}

void BitmapFont::restoreState(SaveGame *state) {
	Common::String fname = state->readString();
	Common::SeekableReadStream *stream;

	g_driver->destroyFont(this);
	delete[] _fontData;
	_fontData = nullptr;
	_fwdCharIndex.clear();
	delete[] _charHeaders;
	_charHeaders = nullptr;

	stream = g_resourceloader->openNewStreamFile(fname.c_str(), true);
	load(fname, stream);
	delete stream;
}

void FontTTF::saveState(SaveGame *state) const {
	state->writeString(getFilename());
	state->writeLESint32(_size);
}

void FontTTF::restoreState(SaveGame *state) {
	Common::String fname = state->readString();
	int size = state->readLESint32();
	Common::SeekableReadStream *stream;

	g_driver->destroyFont(this);
	delete _font;
	_font = nullptr;
	delete _svfn;
	_svfn = nullptr;
	_fallback = nullptr;

	if (g_grim->getGameType() == GType_GRIM && g_grim->getGameLanguage() == Common::KO_KOR) {
		Common::String name = fname + ".txt";
		stream = g_resourceloader->openNewStreamFile(name, true);
		if (!stream)
			warning("Grim: %s is missing; using the bitmap font", name.c_str());
		const bool loaded = stream && loadFromDescriptor(fname, stream);
		delete stream;
		if (!loaded)
			useBitmapFallback(fname);
	} else {
		stream = g_resourceloader->openNewStreamFile(fname.c_str(), true);
		loadTTF(fname, stream, size);
	}
}

void BitmapFont::render(Graphics::Surface &buf, const Common::String &currentLine,
			const Graphics::PixelFormat &pixelFormat, uint32 blackColor, uint32 color, uint32 colorKey) const {
	int width = getBitmapStringLength(currentLine) + 1;
	int height = getStringHeight(currentLine) + 1;

	int startColumn = 0;

	buf.create(width, height, pixelFormat);
	buf.fillRect(Common::Rect(0, 0, width, height), colorKey);

	for (uint32 d = 0; d < currentLine.size(); ) {
		uint32 ch = getNextChar(currentLine, d);
		int32 charBitmapWidth = getCharBitmapWidth(ch);
		int32 charBitmapPitch = getCharBitmapPitch(ch);
		int32 charBitmapHeight = getCharBitmapHeight(ch);
		int8 fontRow = getCharStartingLine(ch) + getBaseOffsetY();
		int8 fontCol = getCharStartingCol(ch);

		for (int line = 0; line < charBitmapHeight; line++) {
			int lineOffset = (fontRow + line);
			int columnOffset = startColumn + fontCol;
			int fontOffset = (charBitmapPitch * line);
			for (int bitmapCol = 0; bitmapCol < charBitmapWidth; bitmapCol++, columnOffset++, fontOffset++) {
				byte pixel = getCharData(ch)[fontOffset];
				if (pixel == 0x80) {
					buf.setPixel(columnOffset, lineOffset, blackColor);
				} else if (pixel == 0xFF) {
					buf.setPixel(columnOffset, lineOffset, color);
				}
			}
		}
		startColumn += getCharKernedWidth(ch);
	}
}

FontTTF::FontTTF() : _font(nullptr), _svfn(nullptr), _isUnicode(false), _size(0) {
}

void FontTTF::useBitmapFallback(const Common::String &filename) {
	// A save made with a face this build cannot draw (a TrueType face in a
	// build without FreeType, or one since removed): draw with the game's own
	// bitmap font of that name. It lives in the BitmapFont pool like any
	// other, so it is saved and freed with it.
	_filename = filename;
	for (BitmapFont *f : BitmapFont::getPool()) {
		if (f->getFilename() == filename) {
			_fallback = f;
			return;
		}
	}
	Common::SeekableReadStream *stream = g_resourceloader->openNewStreamFile(filename.c_str(), true);
	if (!stream)
		error("Could not find font file %s", filename.c_str());
	BitmapFont *bitmap = new BitmapFont();
	bitmap->load(filename, stream);
	delete stream;
	_fallback = bitmap;
}

FontTTF::~FontTTF() {
	delete _font;
	delete _svfn;
}

void FontTTF::loadTTF(const Common::String &filename, Common::SeekableReadStream *data, int size) {
	_filename = filename;
	_size = size;
#ifdef USE_FREETYPE2
	_font = Graphics::loadTTFFont(data, DisposeAfterUse::YES, size);
#else
	_font = nullptr;
#endif
	_isUnicode = false;
}

void FontTTF::loadTTFFromArchive(const Common::String &filename, int size) {
	_filename = filename;
	_size = size;
#ifdef USE_FREETYPE2
	_font = Graphics::loadTTFFontFromArchive(filename, size, Graphics::kTTFSizeModeCharacter, 0, Graphics::kTTFRenderModeLight);
#else
	_font = nullptr;
#endif
	_isUnicode = true;
}

bool FontTTF::loadFromDescriptor(const Common::String &filename, Common::SeekableReadStream *descriptor) {
	// "<face file> <size>px" on the first line, as the Korean patch ships it.
	const Common::String line = descriptor->readLine();
	Common::String face;
	int s = 0;
	if (!Graphics::parseFontDescriptor(line, face, s)) {
		warning("Grim: %s.txt: \"%s\" is not \"<font file> <size>px\" with a size of 1 to %d; using the bitmap font",
		        filename.c_str(), line.c_str(), (int)Graphics::kFontDescriptorMaxPx);
		return false;
	}

	Common::SeekableReadStream *stream = g_resourceloader->openNewStreamFile(face.c_str(), true);
	if (!stream) {
		warning("Grim: %s.txt names %s, which is missing; using the bitmap font", filename.c_str(), face.c_str());
		return false;
	}

	if (face.hasSuffixIgnoreCase(".svfn")) {
		Graphics::HiResBitmapFont *bitmap = new Graphics::HiResBitmapFont();
		const bool ok = bitmap->load(*stream);
		delete stream;
		if (!ok || bitmap->cellWidth() <= 0 || bitmap->cellHeight() <= 0) {
			delete bitmap;
			warning("Grim: %s is not a valid SVFN font; using the bitmap font", face.c_str());
			return false;
		}
		_filename = filename;
		_size = s;
		_isUnicode = false;
		_svfn = new Graphics::SvfnGlyphSource(bitmap, DisposeAfterUse::YES);
		return true;
	}

#ifdef USE_FREETYPE2
	loadTTF(filename, stream, s);
	if (!_font) {
		// TTFFont leaves a stream it failed on to the caller.
		delete stream;
		warning("Grim: %s is not a usable TrueType font; using the bitmap font", face.c_str());
		return false;
	}
	// The font now owns the stream and reads glyphs from it on demand.
	return true;
#else
	delete stream;
	warning("Grim: %s needs FreeType, which this build lacks; using the bitmap font", face.c_str());
	return false;
#endif
}

Common::U32String FontTTF::decodeKorean(const Common::String &text) const {
	if (g_grim->_isUtf8)
		return text.decode(Common::CodePage::kUtf8);
	return Common::U32String(text, Common::kWindows949);
}

int32 FontTTF::svfnAdvance(uint32 cp) const {
	const int a = _svfn->advance(cp);
	if (a > 0)
		return a;
	return _svfn->cells(cp) == 2 ? _svfn->advanceWide() : _svfn->advanceNarrow();
}

void FontTTF::svfnCoverage(const Common::U32String &text, Common::Array<byte> &coverage, int &w, int &h) const {
	w = 0;
	for (uint i = 0; i < text.size(); i++)
		w += svfnAdvance(text[i]);
	h = _svfn->cellHeight();
	coverage.clear();
	coverage.resize(w * h, 0);

	const int bpp = _svfn->bitsPerPixel();
	int pen = 0;
	for (uint i = 0; i < text.size(); i++) {
		const uint32 cp = text[i];
		const int cells = _svfn->cells(cp);
		const int glyphW = cells * _svfn->cellWidth();
		for (int y = 0; cells > 0 && y < h; y++) {
			const byte *row = _svfn->row(cp, y);
			for (int x = 0; x < glyphW && pen + x < w; x++) {
				const byte c = Graphics::TextCompose::expandCoverage(row, x, bpp);
				byte &dst = coverage[y * w + pen + x];
				if (c > dst)
					dst = c;
			}
		}
		pen += svfnAdvance(cp);
	}
}

int32 FontTTF::getKernedHeight() const {
	if (_fallback)
		return _fallback->getKernedHeight();
	if (_svfn)
		return _svfn->cellHeight();
	return _font->getFontHeight();
}

int32 FontTTF::getBaseOffsetY() const {
	if (_fallback)
		return _fallback->getBaseOffsetY();
	return 0;
}

int32 FontTTF::getCharKernedWidth(uint32 c) const {
	if (_fallback)
		return _fallback->getCharKernedWidth(c);
	if (_svfn)
		return svfnAdvance(c);
	return _font->getCharWidth(c);
}

int FontTTF::getKernedStringLength(const Common::String &text) const {
	if (_fallback)
		return _fallback->getKernedStringLength(text);
	if (_svfn) {
		const Common::U32String u = g_grim->getGameLanguage() == Common::KO_KOR ? decodeKorean(text) : text.decode(Common::CodePage::kUtf8);
		int w = 0;
		for (uint i = 0; i < u.size(); i++)
			w += svfnAdvance(u[i]);
		return w;
	}
	if (g_grim->getGameLanguage() == Common::KO_KOR) {
		return _font->getStringWidth(decodeKorean(text));
	}
	if (_isUnicode) {
		return _font->getStringWidth(text.decode(Common::CodePage::kUtf8));
	}
	return _font->getStringWidth(text);
}

void FontTTF::render(Graphics::Surface &surface, const Common::String &currentLine, const Graphics::PixelFormat &pixelFormat, uint32 blackColor, uint32 color, uint32 colorKey) const {
	if (_fallback) {
		_fallback->render(surface, currentLine, pixelFormat, blackColor, color, colorKey);
		return;
	}
	if (_svfn) {
		// The legacy OpenGL renderer (colour key = transparent black): white
		// with alpha = coverage, what TTFFont::drawString leaves there too.
		Common::Array<byte> coverage;
		int width, height;
		svfnCoverage(g_grim->getGameLanguage() == Common::KO_KOR ? decodeKorean(currentLine) : currentLine.decode(Common::CodePage::kUtf8), coverage, width, height);
		surface.create(width, height, pixelFormat);
		surface.fillRect(Common::Rect(0, 0, width, height), colorKey);
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				const byte c = coverage[y * width + x];
				if (c)
					surface.setPixel(x, y, pixelFormat.ARGBToColor(c, 255, 255, 255));
			}
		}
		return;
	}
#ifdef USE_FREETYPE2
	if (g_grim->getGameLanguage() == Common::KO_KOR) {
		Common::U32String u32CurrentLine = decodeKorean(currentLine);
		int width = _font->getStringWidth(u32CurrentLine);
		int height = _font->getFontHeight();
		surface.create(width, height, pixelFormat);
		surface.fillRect(Common::Rect(0, 0, width, height), colorKey);
		_font->drawString(&surface, u32CurrentLine, 0, 0, width, 0xFFFFFFFF);
	} else if (_isUnicode) {
		Common::Rect bbox = _font->getBoundingBox(currentLine.decode(Common::CodePage::kUtf8));

		surface.create(bbox.right, bbox.bottom, pixelFormat);
		surface.fillRect(Common::Rect(0, 0, bbox.right, bbox.bottom), colorKey);

		_font->drawString(&surface, currentLine.decode(Common::CodePage::kUtf8), 0, 0, bbox.right, 0xFFFFFFFF);
	} else {
		Common::Rect bbox = _font->getBoundingBox(currentLine);

		surface.create(bbox.right, bbox.bottom, pixelFormat);
		surface.fillRect(Common::Rect(0, 0, bbox.right, bbox.bottom), colorKey);

		_font->drawString(&surface, currentLine, 0, 0, bbox.right, 0xFFFFFFFF);
	}
#endif
}

bool FontTTF::renderAlpha(Graphics::Surface &surface, const Common::String &currentLine, const Graphics::PixelFormat &format, byte r, byte g, byte b) const {
	if (format.bytesPerPixel != 4 || format.aBits() != 8)
		return false;

	if (_fallback) {
		// Opaque text and outline on a transparent key.
		_fallback->render(surface, currentLine, format, format.ARGBToColor(255, 0, 0, 0),
		                  format.ARGBToColor(255, r, g, b), format.ARGBToColor(0, 0, 0, 0));
		return true;
	}

	if (_svfn) {
		Common::Array<byte> coverage;
		int width, height;
		svfnCoverage(g_grim->getGameLanguage() == Common::KO_KOR ? decodeKorean(currentLine) : currentLine.decode(Common::CodePage::kUtf8), coverage, width, height);
		surface.create(width, height, format);
		for (int y = 0; width > 0 && y < height; y++)
			Graphics::TextCompose::coverageToArgb(&coverage[y * width], (uint32 *)surface.getBasePtr(0, y), width, format, r, g, b);
		return true;
	}

#ifdef USE_FREETYPE2
	if (!_font)
		return false;

	// Same sizes as render(); the line is drawn in white on opaque black, so
	// the red channel ends up holding the coverage.
	const uint32 black = format.ARGBToColor(255, 0, 0, 0);
	const uint32 white = format.ARGBToColor(255, 255, 255, 255);
	if (g_grim->getGameLanguage() == Common::KO_KOR) {
		Common::U32String u32CurrentLine = decodeKorean(currentLine);
		int width = _font->getStringWidth(u32CurrentLine);
		int height = _font->getFontHeight();
		surface.create(width, height, format);
		surface.fillRect(Common::Rect(0, 0, width, height), black);
		_font->drawString(&surface, u32CurrentLine, 0, 0, width, white);
	} else if (_isUnicode) {
		Common::U32String u32CurrentLine = currentLine.decode(Common::CodePage::kUtf8);
		Common::Rect bbox = _font->getBoundingBox(u32CurrentLine);
		surface.create(bbox.right, bbox.bottom, format);
		surface.fillRect(Common::Rect(0, 0, bbox.right, bbox.bottom), black);
		_font->drawString(&surface, u32CurrentLine, 0, 0, bbox.right, white);
	} else {
		Common::Rect bbox = _font->getBoundingBox(currentLine);
		surface.create(bbox.right, bbox.bottom, format);
		surface.fillRect(Common::Rect(0, 0, bbox.right, bbox.bottom), black);
		_font->drawString(&surface, currentLine, 0, 0, bbox.right, white);
	}

	// Move the red channel into alpha and colour the line.
	Common::Array<byte> coverage(surface.w, 0);
	for (int y = 0; y < surface.h; y++) {
		uint32 *row = (uint32 *)surface.getBasePtr(0, y);
		for (int x = 0; x < surface.w; x++) {
			byte a, cr, cg, cb;
			format.colorToARGB(row[x], a, cr, cg, cb);
			coverage[x] = cr;
		}
		Graphics::TextCompose::coverageToArgb(coverage.begin(), row, surface.w, format, r, g, b);
	}
	return true;
#else
	return false;
#endif
}

// Hardcoded default font for FPS, GUI, etc
const uint8 BitmapFont::emerFont[][13] = {
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x36, 0x36, 0x36},
{0x00, 0x00, 0x00, 0x66, 0x66, 0xff, 0x66, 0x66, 0xff, 0x66, 0x66, 0x00, 0x00},
{0x00, 0x00, 0x18, 0x7e, 0xff, 0x1b, 0x1f, 0x7e, 0xf8, 0xd8, 0xff, 0x7e, 0x18},
{0x00, 0x00, 0x0e, 0x1b, 0xdb, 0x6e, 0x30, 0x18, 0x0c, 0x76, 0xdb, 0xd8, 0x70},
{0x00, 0x00, 0x7f, 0xc6, 0xcf, 0xd8, 0x70, 0x70, 0xd8, 0xcc, 0xcc, 0x6c, 0x38},
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x1c, 0x0c, 0x0e},
{0x00, 0x00, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x18, 0x0c},
{0x00, 0x00, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x18, 0x30},
{0x00, 0x00, 0x00, 0x00, 0x99, 0x5a, 0x3c, 0xff, 0x3c, 0x5a, 0x99, 0x00, 0x00},
{0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0xff, 0xff, 0x18, 0x18, 0x18, 0x00, 0x00},
{0x00, 0x00, 0x30, 0x18, 0x1c, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x60, 0x60, 0x30, 0x30, 0x18, 0x18, 0x0c, 0x0c, 0x06, 0x06, 0x03, 0x03},
{0x00, 0x00, 0x3c, 0x66, 0xc3, 0xe3, 0xf3, 0xdb, 0xcf, 0xc7, 0xc3, 0x66, 0x3c},
{0x00, 0x00, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78, 0x38, 0x18},
{0x00, 0x00, 0xff, 0xc0, 0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0xe7, 0x7e},
{0x00, 0x00, 0x7e, 0xe7, 0x03, 0x03, 0x07, 0x7e, 0x07, 0x03, 0x03, 0xe7, 0x7e},
{0x00, 0x00, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0xff, 0xcc, 0x6c, 0x3c, 0x1c, 0x0c},
{0x00, 0x00, 0x7e, 0xe7, 0x03, 0x03, 0x07, 0xfe, 0xc0, 0xc0, 0xc0, 0xc0, 0xff},
{0x00, 0x00, 0x7e, 0xe7, 0xc3, 0xc3, 0xc7, 0xfe, 0xc0, 0xc0, 0xc0, 0xe7, 0x7e},
{0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x03, 0x03, 0xff},
{0x00, 0x00, 0x7e, 0xe7, 0xc3, 0xc3, 0xe7, 0x7e, 0xe7, 0xc3, 0xc3, 0xe7, 0x7e},
{0x00, 0x00, 0x7e, 0xe7, 0x03, 0x03, 0x03, 0x7f, 0xe7, 0xc3, 0xc3, 0xe7, 0x7e},
{0x00, 0x00, 0x00, 0x38, 0x38, 0x00, 0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x30, 0x18, 0x1c, 0x1c, 0x00, 0x00, 0x1c, 0x1c, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06},
{0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60},
{0x00, 0x00, 0x18, 0x00, 0x00, 0x18, 0x18, 0x0c, 0x06, 0x03, 0xc3, 0xc3, 0x7e},
{0x00, 0x00, 0x3f, 0x60, 0xcf, 0xdb, 0xd3, 0xdd, 0xc3, 0x7e, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x18},
{0x00, 0x00, 0xfe, 0xc7, 0xc3, 0xc3, 0xc7, 0xfe, 0xc7, 0xc3, 0xc3, 0xc7, 0xfe},
{0x00, 0x00, 0x7e, 0xe7, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xe7, 0x7e},
{0x00, 0x00, 0xfc, 0xce, 0xc7, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc7, 0xce, 0xfc},
{0x00, 0x00, 0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xfc, 0xc0, 0xc0, 0xc0, 0xc0, 0xff},
{0x00, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xfc, 0xc0, 0xc0, 0xc0, 0xff},
{0x00, 0x00, 0x7e, 0xe7, 0xc3, 0xc3, 0xcf, 0xc0, 0xc0, 0xc0, 0xc0, 0xe7, 0x7e},
{0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3},
{0x00, 0x00, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e},
{0x00, 0x00, 0x7c, 0xee, 0xc6, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06},
{0x00, 0x00, 0xc3, 0xc6, 0xcc, 0xd8, 0xf0, 0xe0, 0xf0, 0xd8, 0xcc, 0xc6, 0xc3},
{0x00, 0x00, 0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0},
{0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xdb, 0xff, 0xff, 0xe7, 0xc3},
{0x00, 0x00, 0xc7, 0xc7, 0xcf, 0xcf, 0xdf, 0xdb, 0xfb, 0xf3, 0xf3, 0xe3, 0xe3},
{0x00, 0x00, 0x7e, 0xe7, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xe7, 0x7e},
{0x00, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xfe, 0xc7, 0xc3, 0xc3, 0xc7, 0xfe},
{0x00, 0x00, 0x3f, 0x6e, 0xdf, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x3c},
{0x00, 0x00, 0xc3, 0xc6, 0xcc, 0xd8, 0xf0, 0xfe, 0xc7, 0xc3, 0xc3, 0xc7, 0xfe},
{0x00, 0x00, 0x7e, 0xe7, 0x03, 0x03, 0x07, 0x7e, 0xe0, 0xc0, 0xc0, 0xe7, 0x7e},
{0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff},
{0x00, 0x00, 0x7e, 0xe7, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3},
{0x00, 0x00, 0x18, 0x3c, 0x3c, 0x66, 0x66, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3},
{0x00, 0x00, 0xc3, 0xe7, 0xff, 0xff, 0xdb, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3},
{0x00, 0x00, 0xc3, 0x66, 0x66, 0x3c, 0x3c, 0x18, 0x3c, 0x3c, 0x66, 0x66, 0xc3},
{0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x3c, 0x66, 0x66, 0xc3},
{0x00, 0x00, 0xff, 0xc0, 0xc0, 0x60, 0x30, 0x7e, 0x0c, 0x06, 0x03, 0x03, 0xff},
{0x00, 0x00, 0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c},
{0x00, 0x03, 0x03, 0x06, 0x06, 0x0c, 0x0c, 0x18, 0x18, 0x30, 0x30, 0x60, 0x60},
{0x00, 0x00, 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c},
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x66, 0x3c, 0x18},
{0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x38, 0x30, 0x70},
{0x00, 0x00, 0x7f, 0xc3, 0xc3, 0x7f, 0x03, 0xc3, 0x7e, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xfe, 0xc3, 0xc3, 0xc3, 0xc3, 0xfe, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0},
{0x00, 0x00, 0x7e, 0xc3, 0xc0, 0xc0, 0xc0, 0xc3, 0x7e, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x7f, 0xc3, 0xc3, 0xc3, 0xc3, 0x7f, 0x03, 0x03, 0x03, 0x03, 0x03},
{0x00, 0x00, 0x7f, 0xc0, 0xc0, 0xfe, 0xc3, 0xc3, 0x7e, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x30, 0x30, 0x30, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x30, 0x33, 0x1e},
{0x7e, 0xc3, 0x03, 0x03, 0x7f, 0xc3, 0xc3, 0xc3, 0x7e, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xfe, 0xc0, 0xc0, 0xc0, 0xc0},
{0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x18, 0x00},
{0x38, 0x6c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x00, 0x00, 0x0c, 0x00},
{0x00, 0x00, 0xc6, 0xcc, 0xf8, 0xf0, 0xd8, 0xcc, 0xc6, 0xc0, 0xc0, 0xc0, 0xc0},
{0x00, 0x00, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78},
{0x00, 0x00, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0xfe, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xfc, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00},
{0xc0, 0xc0, 0xc0, 0xfe, 0xc3, 0xc3, 0xc3, 0xc3, 0xfe, 0x00, 0x00, 0x00, 0x00},
{0x03, 0x03, 0x03, 0x7f, 0xc3, 0xc3, 0xc3, 0xc3, 0x7f, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xe0, 0xfe, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xfe, 0x03, 0x03, 0x7e, 0xc0, 0xc0, 0x7f, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x1c, 0x36, 0x30, 0x30, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x30, 0x00},
{0x00, 0x00, 0x7e, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x18, 0x3c, 0x3c, 0x66, 0x66, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xc3, 0xe7, 0xff, 0xdb, 0xc3, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xc3, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0xc3, 0x00, 0x00, 0x00, 0x00},
{0xc0, 0x60, 0x60, 0x30, 0x18, 0x3c, 0x66, 0x66, 0xc3, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0xff, 0x60, 0x30, 0x18, 0x0c, 0x06, 0xff, 0x00, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x0f, 0x18, 0x18, 0x18, 0x38, 0xf0, 0x38, 0x18, 0x18, 0x18, 0x0f},
{0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
{0x00, 0x00, 0xf0, 0x18, 0x18, 0x18, 0x1c, 0x0f, 0x1c, 0x18, 0x18, 0x18, 0xf0},
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x8f, 0xf1, 0x60, 0x00, 0x00, 0x00},
{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
};

} // end of namespace Grim
