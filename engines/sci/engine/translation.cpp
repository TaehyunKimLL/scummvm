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

#include "sci/engine/translation.h"

#include "common/debug.h"
#include "common/file.h"
#include "common/textconsole.h"

namespace Sci {

static const char kMagic[8] = { 'S', 'C', 'I', 'T', 'R', 'S', 0, 0 };
static const uint16 kVersion = 1;
static const uint32 kHeaderSize = 0x18;
static const uint32 kLangEntrySize = 32;
static const uint32 kFileEntrySize = 20;

// Candidate bundle names, in order. A game-specific name wins over the
// generic one so several games can share a directory.
static const char *const kBundleNames[] = { "sci.trs", "translation.trs" };

Common::String Translation::normalise(const Common::String &s) {
	// Must match m5mktrs.py exactly: CR/LF/TAB become spaces, runs of spaces
	// collapse, leading and trailing spaces are dropped. Nothing else - no
	// case folding, no punctuation stripping.
	Common::String out;
	bool pendingSpace = false;
	for (uint i = 0; i < s.size(); i++) {
		char c = s[i];
		if (c == '\r' || c == '\n' || c == '\t')
			c = ' ';
		if (c == ' ') {
			if (!out.empty())
				pendingSpace = true;
			continue;
		}
		if (pendingSpace) {
			out += ' ';
			pendingSpace = false;
		}
		out += c;
	}
	return out;
}

uint32 Translation::hash(const Common::String &normalised) {
	// FNV-1a 32 over the UTF-8 bytes of the normalised key.
	uint32 h = 0x811C9DC5u;
	for (uint i = 0; i < normalised.size(); i++) {
		h ^= (byte)normalised[i];
		h *= 0x01000193u;
	}
	return h;
}

const char *Translation::poolString(uint32 offset) const {
	if (offset >= _poolLen)
		return nullptr;
	return _pool + offset;
}

bool Translation::load(const Common::String &language) {
	if (_loaded)
		return true;

	Common::File f;
	bool opened = false;
	for (uint i = 0; i < ARRAYSIZE(kBundleNames) && !opened; i++)
		opened = f.open(Common::Path(kBundleNames[i]));
	if (!opened)
		return false;

	const uint32 size = f.size();
	if (size < kHeaderSize + kLangEntrySize) {
		warning("Translation: bundle is too small (%u bytes)", size);
		return false;
	}

	Common::Array<byte> bytes;
	bytes.resize(size);
	if (f.read(&bytes[0], size) != size) {
		warning("Translation: could not read the bundle");
		return false;
	}
	return loadFromMemory(bytes, language);
}

bool Translation::loadFromMemory(const Common::Array<byte> &bytes, const Common::String &language) {
	if (_loaded)
		return true;
	const uint32 size = bytes.size();
	if (size < kHeaderSize + kLangEntrySize)
		return false;

	_data = bytes;
	const byte *d = &_data[0];

	if (memcmp(d, kMagic, 8) != 0) {
		warning("Translation: bad signature");
		_data.clear();
		return false;
	}
	const uint16 version = READ_LE_UINT16(d + 8);
	const uint16 langCount = READ_LE_UINT16(d + 10);
	const uint32 poolOffset = READ_LE_UINT32(d + 12);
	const uint32 poolLength = READ_LE_UINT32(d + 16);

	if (version != kVersion) {
		warning("Translation: unsupported version %u", version);
		_data.clear();
		return false;
	}
	if (poolOffset > size || poolLength > size - poolOffset) {
		warning("Translation: string pool runs past the end of the file");
		_data.clear();
		return false;
	}
	if (kHeaderSize + (uint32)langCount * kLangEntrySize > size) {
		warning("Translation: language table runs past the end of the file");
		_data.clear();
		return false;
	}

	// Find the requested language.
	for (uint16 i = 0; i < langCount; i++) {
		const byte *le = d + kHeaderSize + i * kLangEntrySize;
		char code[9];
		memcpy(code, le, 8);
		code[8] = '\0';

		if (!language.equalsIgnoreCase(code))
			continue;

		const uint32 entryCount = READ_LE_UINT32(le + 8);
		const uint32 entriesOff = READ_LE_UINT32(le + 12);
		const uint32 bucketCount = READ_LE_UINT32(le + 16);
		const uint32 bucketsOff = READ_LE_UINT32(le + 20);

		// Structural checks. Anything that does not add up rejects the whole
		// bundle rather than leaving a half-built index in place.
		if (bucketCount == 0 || (bucketCount & (bucketCount - 1)) != 0) {
			warning("Translation: bucket count %u is not a power of two", bucketCount);
			break;
		}
		if (entriesOff > size || (uint64)entryCount * kFileEntrySize > size - entriesOff) {
			warning("Translation: entry table runs past the end of the file");
			break;
		}
		if (bucketsOff > size || (uint64)(bucketCount + 1) * 4 > size - bucketsOff) {
			warning("Translation: bucket table runs past the end of the file");
			break;
		}

		// Decode entries once into a native array rather than reading the
		// packed layout on every lookup.
		_entryTable.resize(entryCount);
		for (uint32 e = 0; e < entryCount; e++) {
			const byte *p = d + entriesOff + e * kFileEntrySize;
			Entry &ent = _entryTable[e];
			ent.hash = READ_LE_UINT32(p);
			ent.srcOffset = READ_LE_UINT32(p + 4);
			ent.dstOffset = READ_LE_UINT32(p + 8);
			ent.number = READ_LE_UINT16(p + 12);
			ent.index = READ_LE_UINT16(p + 14);
			// Byte 16 of the entry was reserved and written as zero until
			// script strings joined the bundle; zero therefore means "a TEXT
			// resource", which is what every older bundle contains.
			ent.kind = p[16] ? p[16] : Key::kText;

			if (ent.srcOffset >= poolLength || ent.dstOffset >= poolLength) {
				warning("Translation: entry %u points outside the string pool", e);
				_entryTable.clear();
				_data.clear();
				return false;
			}
		}

		_buckets = (const uint32 *)(d + bucketsOff);
		_bucketCount = bucketCount;
		_entries = _entryTable.begin();
		_entryCount = entryCount;
		_pool = (const char *)(d + poolOffset);
		_poolLen = poolLength;

		// The pool must end in a NUL, otherwise a malformed last entry could
		// read past the buffer.
		if (poolLength == 0 || _pool[poolLength - 1] != '\0') {
			warning("Translation: string pool is not NUL terminated");
			_entryTable.clear();
			_data.clear();
			return false;
		}

		_language = code;
		_loaded = true;
		debug(1, "Translation: %u entries for '%s'", _entryCount, _language.c_str());
		return true;
	}

	_entryTable.clear();
	_data.clear();
	return false;
}

bool Translation::loadAny() {
	// Read the first language entry's code straight out of the file, then go
	// through the normal load path so every structural check still runs.
	Common::File f;
	static const char *const names[] = { "sci.trs", "translation.trs" };
	bool opened = false;
	for (uint i = 0; i < ARRAYSIZE(names) && !opened; i++)
		opened = f.open(Common::Path(names[i]));
	if (!opened)
		return false;

	byte head[kHeaderSize + kLangEntrySize];
	if (f.read(head, sizeof(head)) != sizeof(head))
		return false;
	f.close();

	if (memcmp(head, kMagic, 8) != 0 || READ_LE_UINT16(head + 10) == 0)
		return false;

	char code[9];
	memcpy(code, head + kHeaderSize, 8);
	code[8] = '\0';
	if (!code[0])
		return false;
	return load(Common::String(code));
}

bool Translation::translate(const Common::String &source, Common::String &out,
                            const Key &key) const {
	if (!_loaded || source.empty())
		return false;

	const Common::String norm = normalise(source);
	if (norm.empty())
		return false;

	const uint32 h = hash(norm);
	const uint32 b = h & (_bucketCount - 1);
	const uint32 lo = READ_LE_UINT32(&_buckets[b]);
	const uint32 hi = READ_LE_UINT32(&_buckets[b + 1]);

	if (lo > hi || hi > _entryCount)
		return false;

	const Entry *fallback = nullptr;
	for (uint32 i = lo; i < hi; i++) {
		const Entry &e = _entries[i];
		if (e.hash != h)
			continue;

		// A hash match is not proof: compare the normalised source, so a
		// collision costs a comparison and never a wrong answer.
		const char *src = poolString(e.srcOffset);
		if (!src || normalise(src) != norm)
			continue;

		// A matching key wins immediately; otherwise remember the first
		// source match and keep looking for one whose key agrees.
		if (key.isSet() && e.kind == key.kind && e.number == key.number &&
		    e.index == key.index) {
			const char *dst = poolString(e.dstOffset);
			if (dst) {
				out = dst;	// the pool is UTF-8; copy, do not transcode
				return true;
			}
		}
		if (!fallback)
			fallback = &e;
	}

	if (fallback) {
		// The key matched no entry with this source. That is expected when
		// the bundle was built from another release of the game and the
		// numbering moved, and the first match is the right answer far more
		// often than none. But the same source can legitimately carry
		// different translations in different places - LB1's bundle has 86
		// such groups, 277 entries - and returning the first one is then a
		// wrong answer. Say so once per source, so that a bundle/game
		// mismatch is visible instead of showing up as an oddly-worded line.
		if (key.isSet() && !_warnedFallback.contains(fallback->hash)) {
			_warnedFallback[fallback->hash] = true;
			warning("SCITRS: no entry for %s %u #%u matches \"%.40s\"; using the first source match",
			        key.kind == Key::kScript ? "script" : "text",
			        key.number, key.index, norm.c_str());
		}
		const char *dst = poolString(fallback->dstOffset);
		if (dst) {
			out = dst;
			return true;
		}
	}
	return false;
}

} // End of namespace Sci
