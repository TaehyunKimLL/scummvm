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

#ifndef SCUMM_TRS_STORE_H
#define SCUMM_TRS_STORE_H

#include "common/algorithm.h"
#include "common/array.h"
#include "common/file-cache-stats.h"
#include "common/hashmap.h"
#include "common/stream.h"
#include "common/str.h"

/// KB of strings a .trs bundle keeps once read (TrsStore).
#ifndef SCUMM_TRS_CACHE_KB
#define SCUMM_TRS_CACHE_KB 32
#endif

/// Bytes a .trs bundle reads at a time: the strings next to the one asked
/// for come in with it.
#ifndef SCUMM_TRS_READ_BLOCK
#define SCUMM_TRS_READ_BLOCK 4096
#endif

/// Bytes read at a time while the whole body is read once to index it.
#ifndef SCUMM_TRS_SCAN_BLOCK
#define SCUMM_TRS_SCAN_BLOCK 32768
#endif

namespace Scumm {

/**
 * The body of a .trs bundle, left in its file: the original and translated
 * strings are read as lookups need them, SCUMM_TRS_READ_BLOCK bytes at a
 * time, and the strings used last are kept, SCUMM_TRS_CACHE_KB of them.
 *
 * In memory is an index of the originals instead of their text: a hash of
 * each (with the lines sorted by it), and whether it repeats the line
 * before. That is enough to answer the bundle's binary searches exactly as
 * comparing the text did (find()), provided the lines are in the order the
 * comparison sorts them by, which open() checks.
 *
 * Strings end where ScummEngine::resStrLen() ends them: at a 0 outside an
 * escape, 0xFF taking a code and, unless the code is 1, 2, 3 or 8, two
 * arguments (v1-v7 and HE up to 71; v8 skips five bytes; later HE has no
 * escapes there).
 */
class TrsStore {
public:
	struct Line {
		uint32 orig;	///< body offset of the original
		uint32 trans;	///< body offset of the translation
	};

	TrsStore() : _file(nullptr), _bodyPos(0), _bodySize(0), _version(0), _heversion(0), _cacheBytes(0),
				 _blockSize(SCUMM_TRS_READ_BLOCK), _clock(0), _registered(false) {
		_stats.kind = "trs";
	}

	~TrsStore() { close(); }

	bool isOpen() const { return _file != nullptr; }

	/// Told of each line's translation as open() reads the body.
	typedef void (*TranslationSeen)(void *ctx, const byte *s, uint32 size);

	/**
	 * Take @p file, whose body is @p bodySize bytes from @p bodyPos, for
	 * @p lines (in bundle order). The body is read once, in order and
	 * SCUMM_TRS_SCAN_BLOCK bytes at a time, for the index of the originals;
	 * @p seen, if given, is handed each translation on the way (its end
	 * included). False, with @p file deleted and nothing kept, if a read
	 * fails or the originals are not in sorted order (the caller then keeps
	 * the body in memory, as before).
	 */
	bool open(Common::SeekableReadStream *file, uint32 bodyPos, uint32 bodySize, const Line *lines, uint n,
			  int version, int heversion, const Common::String &name, uint32 cacheBytes = SCUMM_TRS_CACHE_KB * 1024,
			  TranslationSeen seen = nullptr, void *seenCtx = nullptr) {
		close();
		_blockSize = SCUMM_TRS_SCAN_BLOCK;
		_file = file;
		_bodyPos = bodyPos;
		_bodySize = bodySize;
		_version = version;
		_heversion = heversion;
		_cacheBytes = cacheBytes;
		_lines.resize(n);
		for (uint i = 0; i < n; ++i)
			_lines[i] = lines[i];

		Common::Array<uint32> hashes;
		hashes.resize(n);
		_sameAsPrev.clear();
		_sameAsPrev.resize((n + 31) / 32, 0);
		Common::Array<byte> prev, cur, trans;
		bool ok = true;
		for (uint i = 0; i < n && ok; ++i) {
			ok = readString(_lines[i].orig, cur);
			if (!ok)
				break;
			if (seen && _lines[i].trans < _bodySize && readString(_lines[i].trans, trans))
				seen(seenCtx, trans.begin(), trans.size());
			hashes[i] = hash(cur.begin(), cur.size());
			if (i > 0) {
				const int c = memcmp(prev.begin(), cur.begin(), MIN(prev.size(), cur.size()));
				if (c > 0)
					ok = false;	// not sorted: the searches would not find the same lines
				else if (c == 0)
					_sameAsPrev[i / 32] |= 1u << (i % 32);
			}
			prev.swap(cur);
		}
		// Back to small reads, the scan's big buffer gone.
		for (int b = 0; b < kBlocks; ++b) {
			Common::Array<byte>().swap(_blocks[b].data);
			_blocks[b].valid = false;
		}
		_blockSize = SCUMM_TRS_READ_BLOCK;
		if (!ok) {
			close();
			return false;
		}

		_byHash.resize(n);
		for (uint i = 0; i < n; ++i)
			_byHash[i] = (uint16)i;
		Common::sort(_byHash.begin(), _byHash.end(), HashLess(hashes.begin()));
		_hashSorted.resize(n);
		for (uint i = 0; i < n; ++i)
			_hashSorted[i] = hashes[_byHash[i]];

		_stats.name = name;
		_stats.capacity = _cacheBytes + kBlocks * SCUMM_TRS_READ_BLOCK;
		_stats.lookups = _stats.hits = 0;
		Common::FileCacheRegistry::add(&_stats);
		_registered = true;
		updateUsed();
		return true;
	}

	void close() {
		if (_registered)
			Common::FileCacheRegistry::remove(&_stats);
		_registered = false;
		delete _file;
		_file = nullptr;
		_lines.clear();
		_byHash.clear();
		_hashSorted.clear();
		_sameAsPrev.clear();
		for (uint i = 0; i < _entries.size(); ++i)
			delete _entries[i];
		_entries.clear();
		_byOffset.clear();
		for (int b = 0; b < kBlocks; ++b) {
			_blocks[b].data.clear();
			_blocks[b].start = 0;
			_blocks[b].valid = false;
		}
		_stats.reads = _stats.readBytes = _stats.used = 0;
	}

	/**
	 * The lines whose original is @p text (@p textLen bytes before its end,
	 * as resStrLen() counts): [@p first, @p last], neighbours in the sorted
	 * order. False if no original is.
	 */
	bool find(const byte *text, uint32 textLen, uint &first, uint &last) {
		const uint32 h = hash(text, textLen + 1);
		uint lo = 0, hi = _hashSorted.size();
		while (lo < hi) {
			const uint mid = lo + (hi - lo) / 2;
			if (_hashSorted[mid] < h)
				lo = mid + 1;
			else
				hi = mid;
		}
		for (uint k = lo; k < _hashSorted.size() && _hashSorted[k] == h; ++k) {
			const uint line = _byHash[k];
			uint32 origSize = 0;
			const byte *orig = string(_lines[line].orig, &origSize);
			if (!orig || origSize != textLen + 1 || memcmp(orig, text, origSize) != 0)
				continue;
			first = last = line;
			while (first > 0 && sameAsPrev(first))
				--first;
			while (last + 1 < _lines.size() && sameAsPrev(last + 1))
				++last;
			return true;
		}
		return false;
	}

	/**
	 * The string at body offset @p off, from the cache or read into it;
	 * null if it runs past the body or the read fails. The pointer holds
	 * until the next call; @p size, if given, gets its bytes, the end
	 * included.
	 */
	const byte *string(uint32 off, uint32 *size = nullptr) {
		++_stats.lookups;
		Common::HashMap<uint32, Entry *>::iterator it = _byOffset.find(off);
		if (it != _byOffset.end()) {
			++_stats.hits;
			it->_value->lastUse = ++_clock;
			if (size)
				*size = it->_value->bytes.size();
			return it->_value->bytes.begin();
		}
		Common::Array<byte> s;
		if (!readString(off, s))
			return nullptr;
		Entry *e = takeEntry(s.size() + kEntryOverhead);
		e->off = off;
		e->lastUse = ++_clock;
		e->bytes.swap(s);
		_byOffset[off] = e;
		updateUsed();
		if (size)
			*size = e->bytes.size();
		return e->bytes.begin();
	}

	/** Line @p i's translation, as string() gives it. */
	const byte *translation(uint i) { return i < _lines.size() ? string(_lines[i].trans) : nullptr; }
	/** Line @p i's offsets. */
	const Line &line(uint i) const { return _lines[i]; }

	const Common::FileCacheStats &stats() const { return _stats; }
	/// Bytes of the index: offsets, hashes, line order, repeats.
	uint32 indexBytes() const {
		return _lines.size() * sizeof(Line) + _hashSorted.size() * sizeof(uint32) + _byHash.size() * sizeof(uint16) +
			   _sameAsPrev.size() * sizeof(uint32);
	}

	/** FNV-1a over @p len bytes. */
	static uint32 hash(const byte *p, uint32 len) {
		uint32 h = 2166136261u;
		for (uint32 i = 0; i < len; ++i) {
			h ^= p[i];
			h *= 16777619u;
		}
		return h;
	}

private:
	TrsStore(const TrsStore &);
	TrsStore &operator=(const TrsStore &);

	struct Entry {
		uint32 off;
		uint32 lastUse;
		Common::Array<byte> bytes;
	};
	struct Block {
		Block() : start(0), valid(false), lastUse(0) {}
		Common::Array<byte> data;
		uint32 start;
		bool valid;
		uint32 lastUse;
	};
	struct HashLess {
		explicit HashLess(const uint32 *h) : _h(h) {}
		bool operator()(uint16 a, uint16 b) const { return _h[a] < _h[b] || (_h[a] == _h[b] && a < b); }
		const uint32 *_h;
	};
	enum { kBlocks = 2 };
	// What a kept string costs besides its bytes: the entry, its hash map
	// node and the allocator's headers, about.
	static const uint32 kEntryOverhead = 48;

	bool sameAsPrev(uint i) const { return (_sameAsPrev[i / 32] >> (i % 32)) & 1; }

	/** The byte at body offset @p off, read with its block if no block holds it. */
	bool byteAt(uint32 off, byte &out) {
		Block *b = nullptr;
		for (int i = 0; i < kBlocks && !b; ++i)
			if (_blocks[i].valid && off >= _blocks[i].start && off - _blocks[i].start < _blocks[i].data.size())
				b = &_blocks[i];
		if (!b) {
			b = &_blocks[0];
			for (int i = 1; i < kBlocks; ++i)
				if (_blocks[i].lastUse < b->lastUse)
					b = &_blocks[i];
			b->valid = false;
			const uint32 start = off - off % _blockSize;
			const uint32 len = MIN<uint32>(_blockSize, _bodySize - start);
			b->data.resize(len);
			++_stats.reads;
			_stats.readBytes += len;
			if (!_file->seek(_bodyPos + start) || _file->read(b->data.begin(), len) != len) {
				_file->clearErr();
				return false;
			}
			b->start = start;
			b->valid = true;
		}
		b->lastUse = ++_clock;
		out = b->data[off - b->start];
		return true;
	}

	/** The string at @p off, its end included, ended as resStrLen() ends it. */
	bool readString(uint32 off, Common::Array<byte> &out) {
		out.clear();
		uint32 skip = 0;	// escape bytes still to copy whatever they are
		for (uint32 at = off;; ++at) {
			byte c;
			if (at >= _bodySize || !byteAt(at, c))
				return false;
			out.push_back(c);
			if (skip) {
				--skip;
				if (skip == 0x100)	// the code byte of a v1-v7 escape, just read
					skip = (c != 1 && c != 2 && c != 3 && c != 8) ? 2 : 0;
				continue;
			}
			if (!c)
				return true;
			if (c == 0xFF) {
				if (_version == 8)
					skip = 5;
				else if (_heversion <= 71)
					skip = 0x101;	// the code, then maybe two arguments
			}
		}
	}

	Entry *takeEntry(uint32 cost) {
		// Room for @p cost: the least recently used go.
		uint32 used = 0;
		for (uint i = 0; i < _entries.size(); ++i)
			used += _entries[i]->bytes.size() + kEntryOverhead;
		while (!_entries.empty() && used + cost > _cacheBytes) {
			uint oldest = 0;
			for (uint i = 1; i < _entries.size(); ++i)
				if (_entries[i]->lastUse < _entries[oldest]->lastUse)
					oldest = i;
			used -= _entries[oldest]->bytes.size() + kEntryOverhead;
			_byOffset.erase(_entries[oldest]->off);
			delete _entries[oldest];
			_entries.remove_at(oldest);
		}
		Entry *e = new Entry();
		_entries.push_back(e);
		return e;
	}

	void updateUsed() {
		uint32 used = 0;
		for (uint i = 0; i < _entries.size(); ++i)
			used += _entries[i]->bytes.size() + kEntryOverhead;
		for (int b = 0; b < kBlocks; ++b)
			used += _blocks[b].data.size();
		_stats.used = used;
	}

	Common::SeekableReadStream *_file;
	uint32 _bodyPos, _bodySize;
	int _version, _heversion;
	uint32 _cacheBytes;
	uint32 _blockSize;	///< SCUMM_TRS_READ_BLOCK, or SCUMM_TRS_SCAN_BLOCK in open()
	Common::Array<Line> _lines;
	Common::Array<uint16> _byHash;		///< lines in hash order
	Common::Array<uint32> _hashSorted;	///< their hashes
	Common::Array<uint32> _sameAsPrev;	///< bit i: line i's original is line i - 1's
	Common::Array<Entry *> _entries;
	Common::HashMap<uint32, Entry *> _byOffset;
	Block _blocks[kBlocks];
	uint32 _clock;
	Common::FileCacheStats _stats;
	bool _registered;
};

} // End of namespace Scumm

#endif
