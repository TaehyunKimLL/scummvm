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

#include "sci/engine/text_overlay.h"

#include "common/debug.h"
#include "common/file.h"
#include "common/textconsole.h"

namespace Sci {

// Text.MAP header: magic, then the size of the record area in bytes.
static const byte kMapMagic[4] = { 0x03, 0x06, 0x00, 0xFF };
static const uint kMapHeaderSize = 6;
static const uint kMapRecordSize = 6;
// Text.Res block header: type, number, storedLen, origLen, method.
static const uint kBlockHeaderSize = 9;
static const byte kBlockTypeText = 3;

/**
 * Open a file trying both spellings the patches use. The archives ship
 * "Text.MAP"/"Text.Res" (KQ1, SQ1) and "text.map"/"Text.res" (KQ5), and
 * ScummVM's file lookup is case sensitive on most backends.
 */
static bool openEither(Common::File &f, const char *a, const char *b) {
	return f.open(Common::Path(a)) || f.open(Common::Path(b));
}

bool TextOverlay::load() {
	if (_loaded)
		return true;

	Common::File mapFile;
	if (!openEither(mapFile, "Text.MAP", "text.map"))
		return false;

	Common::File resFile;
	if (!openEither(resFile, "Text.Res", "text.res")) {
		warning("TextOverlay: Text.MAP present but Text.Res is missing");
		return false;
	}

	const uint32 mapSize = mapFile.size();
	if (mapSize < kMapHeaderSize) {
		warning("TextOverlay: Text.MAP is too small (%u bytes)", mapSize);
		return false;
	}

	byte magic[4];
	if (mapFile.read(magic, 4) != 4 || memcmp(magic, kMapMagic, 4) != 0) {
		warning("TextOverlay: Text.MAP has an unexpected signature");
		return false;
	}

	// The record area size from the header is the ONLY authority. Both files
	// are padded to a fixed size and the tail still holds a previous game's
	// records, which point into unrelated data and decode as garbage.
	const uint16 recordBytes = mapFile.readUint16LE();
	if (recordBytes == 0 || (recordBytes % kMapRecordSize) != 0 ||
		kMapHeaderSize + recordBytes > mapSize) {
		warning("TextOverlay: Text.MAP declares a %u byte record area, which is invalid", recordBytes);
		return false;
	}

	const uint32 resSize = resFile.size();
	_data.resize(resSize);
	if (resFile.read(&_data[0], resSize) != resSize) {
		warning("TextOverlay: could not read Text.Res");
		_data.clear();
		return false;
	}

	const uint recordCount = recordBytes / kMapRecordSize;
	uint skipped = 0;
	for (uint i = 0; i < recordCount; i++) {
		const uint16 number = mapFile.readUint16LE();
		const uint32 offset = mapFile.readUint32LE();

		if (offset + kBlockHeaderSize > resSize) {
			skipped++;
			continue;
		}

		const byte *hdr = &_data[offset];
		const byte type = hdr[0];
		const uint16 blockNumber = READ_LE_UINT16(hdr + 1);
		const uint16 storedLen = READ_LE_UINT16(hdr + 3);
		const uint16 method = READ_LE_UINT16(hdr + 7);

		// Reject anything that is not a plain stored TEXT block for this
		// record, rather than guessing: a mismatch means we are reading
		// padding, not data.
		if (type != kBlockTypeText || blockNumber != number || method != 0) {
			skipped++;
			continue;
		}
		if (storedLen < 4) {
			// Stub record: listed but carries no text. Not an override.
			skipped++;
			continue;
		}

		const uint32 payloadLen = storedLen - 4;
		if (offset + kBlockHeaderSize + payloadLen > resSize) {
			skipped++;
			continue;
		}

		Block b;
		b.offset = offset + kBlockHeaderSize;
		b.length = payloadLen;
		_blocks[number] = b;
	}

	if (_blocks.empty()) {
		warning("TextOverlay: Text.MAP had no usable records");
		_data.clear();
		return false;
	}

	_loaded = true;
	debug(1, "TextOverlay: %u TEXT resources overridden (%u records skipped)",
		  _blocks.size(), skipped);
	return true;
}

bool TextOverlay::getText(uint16 number, int index, Common::String &out) const {
	if (!_loaded || index < 0)
		return false;

	if (!_blocks.contains(number))
		return false;

	const Block &b = _blocks[number];
	const byte *p = &_data[b.offset];
	const byte *end = p + b.length;

	// Walk to the requested string exactly the way lookupText() walks a real
	// TEXT resource: strings are NUL separated and indexed from zero.
	for (int i = 0; i < index; i++) {
		while (p < end && *p)
			p++;
		if (p >= end)
			return false;	// ran out of strings: fall back to the original
		p++;
	}
	if (p >= end)
		return false;

	const byte *stringEnd = p;
	while (stringEnd < end && *stringEnd)
		stringEnd++;

	out = Common::String((const char *)p, stringEnd - p);
	return true;
}

} // End of namespace Sci
