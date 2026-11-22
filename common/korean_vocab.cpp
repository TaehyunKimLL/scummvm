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

#include "common/debug.h"
#include "common/endian.h"
#include "common/file.h"
#include "common/textconsole.h"

#include "common/korean_stem.h"
#include "common/korean_vocab.h"

namespace Common {

static const uint32 kHeadV2 = 16;
static const uint32 kHeadV3 = 20;
static const uint32 kEntryV2 = 8;
static const uint32 kEntryV3 = 12;
static const uint32 kMaxHeaderText = 1024;

#define KV_DEBUG(level, ...) \
	do { if (_debugChannel) debugC(level, _debugChannel, __VA_ARGS__); } while (0)

KoreanVocabulary::KoreanVocabulary(uint32 debugChannel) : _data(nullptr), _size(0), _count(0), _version(0),
	_entrySize(0), _entries(nullptr), _blob(nullptr), _blobUnits(0), _debugChannel(debugChannel) {
}

bool KoreanVocabulary::load(const Path &filename, const char *magic, const TranslationFileHeader &expected) {
	unload();
	File f;
	if (!f.open(filename)) {
		KV_DEBUG(1, "[korvocab] %s not found; Korean words resolve nothing", filename.toString().c_str());
		return false;
	}
	return loadFromStream(f, magic, filename.toString(), expected);
}

bool KoreanVocabulary::refuse(const String &name, const String &why) {
	warning("%s: %s", name.c_str(), why.c_str());
	unload();
	return false;
}

bool KoreanVocabulary::loadFromStream(SeekableReadStream &in, const char *magic, const String &name,
                                      const TranslationFileHeader &expected) {
	unload();
	const int64 fileSize = in.size();
	if (fileSize < (int64)kHeadV2)
		return refuse(name, "too small to be a Korean word table");
	if (fileSize > 16 * 1024 * 1024)
		return refuse(name, "larger than 16 MB, not a Korean word table");

	_data = (byte *)malloc((size_t)fileSize);
	if (!_data)
		return refuse(name, "out of memory");
	_size = (uint32)fileSize;
	if (in.read(_data, _size) != _size)
		return refuse(name, "could not be read in full");

	byte want[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	memcpy(want, magic, MIN<size_t>(strlen(magic), 6));
	if (memcmp(_data, want, 8) != 0)
		return refuse(name, String::format("not a %s word table (bad magic)", magic));

	_version = READ_LE_UINT32(_data + 8);
	_count = READ_LE_UINT32(_data + 12);
	uint32 entriesAt;
	if (_version == 2) {
		_entrySize = kEntryV2;
		entriesAt = kHeadV2;
	} else if (_version == 3) {
		if (_size < kHeadV3)
			return refuse(name, "too small for a version 3 header");
		_entrySize = kEntryV3;
		const uint32 headerSize = READ_LE_UINT32(_data + 16);
		if (headerSize == 0 || headerSize % 4 || headerSize > kMaxHeaderText || kHeadV3 + headerSize > _size)
			return refuse(name, String::format("header size %u is not valid", headerSize));
		const char *text = (const char *)_data + kHeadV3;
		const char *end = (const char *)memchr(text, 0, headerSize);
		if (!end)
			return refuse(name, "the header text has no NUL");
		const uint32 textLen = (uint32)(end - text);
		String error;
		if (!TranslationFileHeader::parse(String(text, textLen), _header, error))
			return refuse(name, "bad header: " + (error.empty() ? String("no #! line") : error));
		String engine(magic, MIN<size_t>(strlen(magic), 3));
		engine.toLowercase();
		if (_header.engine != engine)
			return refuse(name, "header engine=" + _header.engine + " in a " + magic + " table");
		entriesAt = kHeadV3 + headerSize;
	} else {
		return refuse(name, String::format("version %u; this build reads versions 2 and 3", _version));
	}

	if ((uint64)entriesAt + (uint64)_count * _entrySize > _size)
		return refuse(name, String::format("claims %u entries but is only %u bytes", _count, _size));
	_entries = _data + entriesAt;
	_blob = _entries + _count * _entrySize;
	_blobUnits = (_size - (uint32)(_blob - _data)) / 2;

	// Every entry addresses code units inside the blob, and the entries are in the order findIndex() searches:
	// checked once here, so a bad file is refused instead of giving wrong answers later.
	for (uint32 i = 0; i < _count; ++i) {
		const Entry e = entry(i);
		if (e.length == 0 || (uint64)e.offset + e.length > _blobUnits)
			return refuse(name, String::format("entry %u runs past the end of its string table", i));
		if (i > 0 && compareEntries(i - 1, i) > 0)
			return refuse(name, String::format("entry %u is out of order", i));
	}

	if (_version == 2) {
		_header = TranslationFileHeader();
		checkTranslationHeader(_header, expected, name);
	} else if (checkTranslationHeader(_header, expected, name) == kTranslationMismatch) {
		unload();
		return false;
	}
	KV_DEBUG(1, "[korvocab] loaded %u Korean words (version %u) from %s", _count, _version, name.c_str());
	return true;
}

void KoreanVocabulary::unload() {
	free(_data);
	_data = nullptr;
	_size = _count = _version = _entrySize = _blobUnits = 0;
	_entries = _blob = nullptr;
	_header = TranslationFileHeader();
}

KoreanVocabulary::Entry KoreanVocabulary::entry(uint32 index) const {
	const byte *e = _entries + index * _entrySize;
	Entry out;
	if (_version == 2) {
		out.offset = READ_LE_UINT16(e);
		e += 2;
	} else {
		out.offset = READ_LE_UINT32(e);
		e += 4;
	}
	out.length = READ_LE_UINT16(e);
	out.group = READ_LE_UINT16(e + 2);
	out.wclass = READ_LE_UINT16(e + 4);
	return out;
}

int KoreanVocabulary::compare(uint32 index, const uint16 *word, uint len) const {
	const Entry e = entry(index);
	const byte *w = _blob + e.offset * 2;
	const uint n = MIN<uint>(len, e.length);
	for (uint i = 0; i < n; ++i) {
		const uint16 a = READ_LE_UINT16(w + i * 2);
		if (a != word[i])
			return a < word[i] ? -1 : 1;
	}
	if (e.length == len)
		return 0;
	return e.length < len ? -1 : 1;
}

int KoreanVocabulary::compareEntries(uint32 a, uint32 b) const {
	const Entry ea = entry(a);
	const Entry eb = entry(b);
	const byte *wa = _blob + ea.offset * 2;
	const byte *wb = _blob + eb.offset * 2;
	const uint n = MIN<uint>(ea.length, eb.length);
	for (uint i = 0; i < n; ++i) {
		const uint16 ua = READ_LE_UINT16(wa + i * 2);
		const uint16 ub = READ_LE_UINT16(wb + i * 2);
		if (ua != ub)
			return ua < ub ? -1 : 1;
	}
	if (ea.length == eb.length)
		return 0;
	return ea.length < eb.length ? -1 : 1;
}

int KoreanVocabulary::findIndex(const uint16 *word, uint len) const {
	if (!_data || !len)
		return -1;
	int lo = 0;
	int hi = (int)_count - 1;
	while (lo <= hi) {
		const int mid = lo + (hi - lo) / 2;
		const int c = compare((uint32)mid, word, len);
		if (c == 0)
			return mid;
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return -1;
}

bool KoreanVocabulary::sameWord(uint32 a, uint32 b) const {
	const Entry ea = entry(a);
	const Entry eb = entry(b);
	return ea.length == eb.length && memcmp(_blob + ea.offset * 2, _blob + eb.offset * 2, ea.length * 2) == 0;
}

uint KoreanVocabulary::collect(int index, uint16 *groups, uint16 *classes, uint max) const {
	if (index < 0)
		return 0;
	uint32 first = (uint32)index;
	while (first > 0 && sameWord(first - 1, first))
		--first;
	uint n = 0;
	for (uint32 i = first; i < _count && n < max && (i == first || sameWord(first, i)); ++i) {
		const Entry e = entry(i);
		groups[n] = e.group;
		classes[n] = e.wclass;
		++n;
	}
	return n;
}

bool KoreanVocabulary::lookup(const U32String &word, uint16 &group, uint16 &wclass) const {
	uint8 role;
	return lookupAll(word, &group, &wclass, 1, role) == 1;
}

uint KoreanVocabulary::lookupExact(const U32String &word, uint16 *groups, uint16 *classes, uint max) const {
	if (!_data || word.empty() || word.size() > KoreanStems::kMaxLen || !max)
		return 0;
	uint16 typed[KoreanStems::kMaxLen];
	for (uint i = 0; i < word.size(); ++i) {
		if (word[i] > 0xFFFF)
			return 0;
		typed[i] = (uint16)word[i];
	}
	return collect(findIndex(typed, word.size()), groups, classes, max);
}

uint KoreanVocabulary::lookupAll(const U32String &word, uint16 *groups, uint16 *classes, uint max,
                                 uint8 &role) const {
	role = kKoreanRoleNone;
	uint n = lookupExact(word, groups, classes, max);
	if (n) {
		KV_DEBUG(2, "[korvocab] exact -> %u pair(s), group %u class %u", n, groups[0], classes[0]);
		return n;
	}
	if (!_data || word.empty() || word.size() > KoreanStems::kMaxLen || !max)
		return 0;
	// The readings of the word, best first: 연다 is stored as 열다, 가방을 as 가방.
	KoreanStems stems;
	const uint count = koreanStemCandidates(word, stems);
	for (uint i = 0; i < count; ++i) {
		n = collect(findIndex(stems.cp[i], stems.len[i]), groups, classes, max);
		if (n) {
			role = stems.role[i];
			KV_DEBUG(2, "[korvocab] stem -> %u pair(s), group %u class %u, role %u", n, groups[0], classes[0], role);
			return n;
		}
	}
	return 0;
}

bool KoreanVocabulary::lookupJoined(const U32String &first, const U32String &second) const {
	uint16 g, c;
	if (lookupExact(first + second, &g, &c, 1))
		return true;
	KoreanStems stems;
	const uint n = koreanStemCandidates(second, stems);
	for (uint i = 0; i < n; ++i) {
		U32String joined = first;
		for (uint k = 0; k < stems.len[i]; ++k)
			joined += (u32char_type_t)stems.cp[i][k];
		if (lookupExact(joined, &g, &c, 1))
			return true;
	}
	// A reading of the joined string, the one lookupAll() would take, counts only when it reaches past the first
	// word: 사 줘 -> 사다 reads the second word as the verb's auxiliary, 나무 보다 -> 나무 would drop it as a particle.
	const U32String joined = first + second;
	const uint m = koreanStemCandidates(joined, stems);
	for (uint i = 0; i < m; ++i) {
		if (!lookupExact(stems.get(i), &g, &c, 1))
			continue;
		bool insideFirst = stems.len[i] <= first.size();
		for (uint k = 0; insideFirst && k < stems.len[i]; ++k)
			insideFirst = stems.cp[i][k] == first[k];
		return !insideFirst;
	}
	return false;
}

} // End of namespace Common
