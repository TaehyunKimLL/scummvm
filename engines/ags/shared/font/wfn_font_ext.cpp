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

// The parts of WFNFont that need neither the engine's globals nor its
// streams: character lookup and the Korean extension reader (extfntN.wfn).
// Kept apart so the unit tests can link them without the engine.

#include "ags/shared/font/wfn_font.h"
#include "common/endian.h"
#include "graphics/hires_text/codepage_kr.h"

namespace AGS3 {

static const char   EXT_FILE_SIGNATURE[] = "WGT Font File  ";
static const size_t EXT_FILE_SIG_LENGTH = 15;
static const size_t EXT_HEADER_SIZE = EXT_FILE_SIG_LENGTH + sizeof(uint32_t);
static const size_t EXT_CHAR_HEADER_SIZE = sizeof(uint16_t) * 2;

WFNChar::WFNChar()
	: Width(0)
	, Height(0)
	, Data(nullptr) {
}

void WFNChar::RestrictToBytes(size_t bytes) {
	if (bytes < GetRequiredPixelSize())
		Height = static_cast<uint16_t>(bytes / GetRowByteCount());
}

const WFNChar &WFNFont::GetChar(uint32_t code) const {
	if (code >= 256 && !_extItems.empty()) {
		const int idx = Graphics::KoreanCodePage::ksx1001HangulIndexOf(code);
		return idx >= 0 ? _extItems[idx] : _emptyChar;
	}
	// Without an extension exactly as before, when the code was passed as uint16_t
	const uint16_t c = static_cast<uint16_t>(code);
	return c < _refs.size() ? *_refs[c] : _emptyChar;
}

void WFNFont::Clear() {
	_refs.clear();
	_items.clear();
	_pixelData.clear();
	ClearExt();
}

void WFNFont::ClearExt() {
	_extItems.clear();
	_extData.clear();
}

uint16_t WFNFont::GetExtHeight() const {
	return _extItems.empty() ? 0 : _extItems[0].Height;
}

WFNError WFNFont::ReadExtFromData(const uint8_t *data, size_t size) {
	std::vector<uint8_t> buf;
	if (data && size > 0) {
		buf.resize(size);
		memcpy(&buf.front(), data, size);
	}
	return ParseExt(buf);
}

WFNError WFNFont::ParseExt(std::vector<uint8_t> &buf) {
	ClearExt();

	const size_t size = buf.size();
	const uint8_t *data = size > 0 ? &buf.front() : nullptr;
	if (!data || size < EXT_HEADER_SIZE ||
		memcmp(data, EXT_FILE_SIGNATURE, EXT_FILE_SIG_LENGTH) != 0)
		return kWFNErr_BadSignature;

	const size_t table_addr = READ_LE_UINT32(data + EXT_FILE_SIG_LENGTH);
	if (table_addr < EXT_HEADER_SIZE || table_addr > size)
		return kWFNErr_BadTableAddress;

	// The table must hold exactly the KS X 1001 Hangul block, nothing else:
	// a shorter or longer one is not a file this reader knows.
	const size_t char_count = Graphics::KoreanCodePage::kKsx1001HangulCount;
	if (size - table_addr != char_count * sizeof(uint32_t))
		return kWFNErr_BadCharCount;

	// Take the buffer over; the items point into it.
	_extData.swap(buf);
	const uint8_t *file = &_extData.front();

	// Every character is checked on its own: its header and pixel rows must
	// lie between the file header and the offsets table. A bad one becomes
	// an empty character and leaves the others alone.
	WFNError err = kWFNErr_NoError;
	_extItems.resize(char_count);
	for (size_t i = 0; i < char_count; ++i) {
		const size_t off = READ_LE_UINT32(file + table_addr + i * sizeof(uint32_t));
		if (off < EXT_HEADER_SIZE || off > table_addr - EXT_CHAR_HEADER_SIZE) {
			err = kWFNErr_HasBadCharacters;
			continue;
		}
		WFNChar ch;
		ch.Width = READ_LE_UINT16(file + off);
		ch.Height = READ_LE_UINT16(file + off + sizeof(uint16_t));
		const size_t pixels = ch.GetRequiredPixelSize();
		if (pixels > table_addr - off - EXT_CHAR_HEADER_SIZE) {
			err = kWFNErr_HasBadCharacters;
			continue;
		}
		if (pixels > 0)
			ch.Data = file + off + EXT_CHAR_HEADER_SIZE;
		else
			ch.Width = ch.Height = 0;
		_extItems[i] = ch;
	}
	return err;
}

} // namespace AGS3
