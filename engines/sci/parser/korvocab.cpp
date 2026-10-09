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

#include "common/file.h"
#include "common/textconsole.h"

#include "sci/sci.h"
#include "sci/parser/korvocab.h"
#include "sci/parser/korstem.h"

namespace Sci {

// "SCIKOR\0\0", then {u32 version, u32 count}, then count * 8-byte entries,
// then the blob. Written by harness/k6bake.py.
static const char kMagic[8] = { 'S', 'C', 'I', 'K', 'O', 'R', 0, 0 };
static const uint32 kVersion = 1;
static const uint32 kHeaderSize = 16;
static const uint32 kEntrySize = 8;

bool KoreanVocabulary::load(const Common::String &filename) {
	unload();

	Common::File f;
	if (!f.open(Common::Path(filename))) {
		// Absent is not an error - it means no Korean mapping is installed.
		debugC(1, kDebugLevelHangul,
		       "[korvocab] %s not found; Korean input will resolve nothing",
		       filename.c_str());
		return false;
	}

	const uint32 fileSize = (uint32)f.size();
	if (fileSize < kHeaderSize) {
		warning("%s is too small to be a Korean vocabulary", filename.c_str());
		return false;
	}

	byte *buf = (byte *)malloc(fileSize);
	if (!buf)
		return false;
	if (f.read(buf, fileSize) != fileSize) {
		free(buf);
		warning("%s could not be read in full", filename.c_str());
		return false;
	}

	if (memcmp(buf, kMagic, sizeof(kMagic)) != 0) {
		free(buf);
		warning("%s is not a Korean vocabulary (bad magic)", filename.c_str());
		return false;
	}

	const uint32 version = READ_LE_UINT32(buf + 8);
	const uint32 count = READ_LE_UINT32(buf + 12);
	if (version != kVersion) {
		free(buf);
		warning("%s is version %u, expected %u", filename.c_str(),
		        version, kVersion);
		return false;
	}

	// Refuse a file whose table does not fit rather than trusting the count
	// and indexing past the end later, where the failure would look like a
	// random wrong answer instead of a bad file.
	if ((uint64)kHeaderSize + (uint64)count * kEntrySize > fileSize) {
		free(buf);
		warning("%s claims %u entries but is only %u bytes",
		        filename.c_str(), count, fileSize);
		return false;
	}

	_data = buf;
	_size = fileSize;
	_count = count;
	_entries = buf + kHeaderSize;
	_blob = _entries + (uint32)count * kEntrySize;

	// Every entry must address bytes inside the blob. Checked once here so
	// the lookup path needs no bounds test per probe.
	const uint32 blobSize = fileSize - (uint32)(_blob - buf);
	for (uint32 i = 0; i < count; ++i) {
		const byte *e = _entries + i * kEntrySize;
		const uint16 off = READ_LE_UINT16(e);
		const uint16 len = READ_LE_UINT16(e + 2);
		if ((uint32)off + len > blobSize) {
			warning("%s entry %u runs past the end of its string table",
			        filename.c_str(), i);
			unload();
			return false;
		}
	}

	debugC(1, kDebugLevelHangul, "[korvocab] loaded %u Korean words from %s",
	       _count, filename.c_str());
	return true;
}

void KoreanVocabulary::unload() {
	free(_data);
	_data = nullptr;
	_size = 0;
	_count = 0;
	_entries = nullptr;
	_blob = nullptr;
}

bool KoreanVocabulary::find(const char *word, uint len,
                            uint16 &group, uint16 &wclass) const {
	if (!_data || !len)
		return false;

	int lo = 0;
	int hi = (int)_count - 1;
	while (lo <= hi) {
		const int mid = lo + (hi - lo) / 2;
		const byte *e = _entries + (uint32)mid * kEntrySize;
		const uint16 off = READ_LE_UINT16(e);
		const uint16 elen = READ_LE_UINT16(e + 2);

		// Compare as UNSIGNED bytes. EUC-KR lead bytes are >= 0x80 and a
		// signed char comparison orders them below ASCII, which would not
		// match the order k6bake.py sorted the table in - the search would
		// then miss entries that are present.
		const uint n = MIN<uint>(len, elen);
		int cmp = 0;
		for (uint i = 0; i < n; ++i) {
			const byte a = (byte)word[i];
			const byte b = _blob[off + i];
			if (a != b) {
				cmp = (a < b) ? -1 : 1;
				break;
			}
		}
		if (cmp == 0 && len != elen)
			cmp = (len < elen) ? -1 : 1;

		if (cmp == 0) {
			group = READ_LE_UINT16(e + 4);
			wclass = READ_LE_UINT16(e + 6);
			return true;
		}
		if (cmp > 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return false;
}

bool KoreanVocabulary::lookup(const char *word, uint len,
                              uint16 &group, uint16 &wclass) const {
	if (!_data || !len)
		return false;

	if (find(word, len, group, wclass)) {
		debugC(2, kDebugLevelHangul, "[korvocab] exact -> group %u class %u",
		       group, wclass);
		return true;
	}

	// Stemming: 먹는다 is stored as 먹다. Each candidate is a SHORTER or
	// re-suffixed form of what was typed; nothing is invented and then
	// searched for.
	Common::String candidates[kMaxKoreanStems];
	const uint n = koreanStemCandidates(word, len, candidates);
	for (uint i = 0; i < n; ++i) {
		if (find(candidates[i].c_str(), candidates[i].size(), group, wclass)) {
			debugC(2, kDebugLevelHangul,
			       "[korvocab] stem -> group %u class %u", group, wclass);
			return true;
		}
	}
	return false;
}

} // End of namespace Sci
