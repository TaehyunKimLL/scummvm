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

// "SCIKOR\0\0", then {u32 version, u32 count}, then count * 8-byte entries
// {u16 offset, u16 len, u16 group, u16 class}, then the blob of UTF-16LE
// code units the offsets index, sorted by code unit. Written by
// harness/k6bake.py. Version 1 held EUC-KR bytes; that cost every lookup
// an encode of each candidate and the encoder its 128 KB reverse table.
static const char kMagic[8] = { 'S', 'C', 'I', 'K', 'O', 'R', 0, 0 };
static const uint32 kVersion = 2;
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

	// Every entry must address code units inside the blob. Checked once
	// here so the lookup path needs no bounds test per probe.
	const uint32 blobUnits = (fileSize - (uint32)(_blob - buf)) / 2;
	for (uint32 i = 0; i < count; ++i) {
		const byte *e = _entries + i * kEntrySize;
		const uint16 off = READ_LE_UINT16(e);
		const uint16 len = READ_LE_UINT16(e + 2);
		if ((uint32)off + len > blobUnits) {
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

int KoreanVocabulary::findIndex(const uint16 *word, uint len) const {
	if (!_data || !len)
		return -1;

	int lo = 0;
	int hi = (int)_count - 1;
	while (lo <= hi) {
		const int mid = lo + (hi - lo) / 2;
		const byte *e = _entries + (uint32)mid * kEntrySize;
		const uint16 off = READ_LE_UINT16(e);
		const uint16 elen = READ_LE_UINT16(e + 2);
		const byte *entry = _blob + (uint32)off * 2;

		// Code unit by code unit, unsigned, which is the order k6bake.py
		// sorted the table in.
		const uint n = MIN<uint>(len, elen);
		int cmp = 0;
		for (uint i = 0; i < n; ++i) {
			const uint16 a = word[i];
			const uint16 b = READ_LE_UINT16(entry + i * 2);
			if (a != b) {
				cmp = (a < b) ? -1 : 1;
				break;
			}
		}
		if (cmp == 0 && len != elen)
			cmp = (len < elen) ? -1 : 1;

		if (cmp == 0)
			return mid;
		if (cmp > 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

bool KoreanVocabulary::sameWord(uint32 a, uint32 b) const {
	const byte *ea = _entries + a * kEntrySize;
	const byte *eb = _entries + b * kEntrySize;
	const uint16 la = READ_LE_UINT16(ea + 2);
	if (la != READ_LE_UINT16(eb + 2))
		return false;
	const byte *wa = _blob + (uint32)READ_LE_UINT16(ea) * 2;
	const byte *wb = _blob + (uint32)READ_LE_UINT16(eb) * 2;
	return memcmp(wa, wb, (uint32)la * 2) == 0;
}

uint KoreanVocabulary::collect(int index, uint16 *groups, uint16 *classes, uint max) const {
	if (index < 0)
		return 0;
	// k6bake.py sorts by word only, so a word's entries sit together.
	uint32 first = (uint32)index;
	while (first > 0 && sameWord(first - 1, first))
		--first;
	uint n = 0;
	for (uint32 i = first; i < _count && n < max && (i == first || sameWord(first, i)); ++i) {
		const byte *e = _entries + i * kEntrySize;
		groups[n] = READ_LE_UINT16(e + 4);
		classes[n] = READ_LE_UINT16(e + 6);
		++n;
	}
	return n;
}

bool KoreanVocabulary::find(const uint16 *word, uint len,
                            uint16 &group, uint16 &wclass) const {
	return collect(findIndex(word, len), &group, &wclass, 1) == 1;
}

bool KoreanVocabulary::lookup(const Common::U32String &word,
                              uint16 &group, uint16 &wclass) const {
	uint8 role;
	return lookupAll(word, &group, &wclass, 1, role) == 1;
}

uint KoreanVocabulary::lookupAll(const Common::U32String &word, uint16 *groups,
                                 uint16 *classes, uint max, uint8 &role) const {
	role = kKoreanRoleNone;
	if (!_data || word.empty() || word.size() > KoreanStems::kMaxLen || !max)
		return 0;

	uint16 typed[KoreanStems::kMaxLen];
	for (uint i = 0; i < word.size(); ++i) {
		if (word[i] > 0xFFFF)
			return 0;
		typed[i] = (uint16)word[i];
	}
	uint n = collect(findIndex(typed, word.size()), groups, classes, max);
	if (n) {
		debugC(2, kDebugLevelHangul, "[korvocab] exact -> %u pair(s), group %u class %u",
		       n, groups[0], classes[0]);
		return n;
	}

	// The readings of the word, best first: 연다 is stored as 열다, 가방을
	// as 가방. The first one the table holds wins.
	KoreanStems stems;
	const uint count = koreanStemCandidates(word, stems);
	for (uint i = 0; i < count; ++i) {
		n = collect(findIndex(stems.cp[i], stems.len[i]), groups, classes, max);
		if (n) {
			role = stems.role[i];
			debugC(2, kDebugLevelHangul,
			       "[korvocab] stem -> %u pair(s), group %u class %u, role %u", n, groups[0], classes[0], role);
			return n;
		}
	}
	return 0;
}

} // End of namespace Sci
