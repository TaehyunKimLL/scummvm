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

#ifndef COMMON_FILE_CACHE_STATS_H
#define COMMON_FILE_CACHE_STATS_H

#include "common/array.h"
#include "common/str.h"

namespace Common {

/**
 * The counters of one cache of file data: a file kept open and read in
 * parts as they are used, the parts used lately kept in memory. A cache
 * registers its counters (FileCacheRegistry) for as long as it lives, so a
 * memory log or the debug socket can say what the caches hold and how often
 * they had to read.
 */
struct FileCacheStats {
	FileCacheStats() : capacity(0), used(0), lookups(0), hits(0), reads(0), readBytes(0) {}

	String kind;		///< what is cached ("svf", "trs")
	String name;		///< which file
	uint32 capacity;	///< bytes the cache may hold
	uint32 used;		///< bytes it holds now, read buffers included
	uint32 lookups;		///< uses
	uint32 hits;		///< uses served from memory
	uint32 reads;		///< reads of the file
	uint32 readBytes;	///< bytes read
};

/** The live FileCacheStats; main thread only. */
class FileCacheRegistry {
public:
	static void add(FileCacheStats *stats) {
		list().push_back(stats);
	}

	static void remove(FileCacheStats *stats) {
		Array<FileCacheStats *> &l = list();
		for (uint i = 0; i < l.size(); ++i) {
			if (l[i] == stats) {
				l.remove_at(i);
				return;
			}
		}
	}

	static const Array<FileCacheStats *> &all() { return list(); }

	/** The sum over every cache of @p kind. */
	static FileCacheStats total(const char *kind) {
		FileCacheStats t;
		t.kind = kind;
		const Array<FileCacheStats *> &l = list();
		for (uint i = 0; i < l.size(); ++i) {
			if (l[i]->kind != kind)
				continue;
			t.capacity += l[i]->capacity;
			t.used += l[i]->used;
			t.lookups += l[i]->lookups;
			t.hits += l[i]->hits;
			t.reads += l[i]->reads;
			t.readBytes += l[i]->readBytes;
		}
		return t;
	}

	/**
	 * One line per kind present: "<kind> n=<caches> used=<KB> cap=<KB>
	 * lookups=<n> hits=<n> reads=<n> read=<KB>", joined by @p sep.
	 */
	static String summary(const char *sep = "; ") {
		Array<String> kinds;
		const Array<FileCacheStats *> &l = list();
		for (uint i = 0; i < l.size(); ++i) {
			bool seen = false;
			for (uint k = 0; k < kinds.size() && !seen; ++k)
				seen = kinds[k] == l[i]->kind;
			if (!seen)
				kinds.push_back(l[i]->kind);
		}
		String out;
		for (uint k = 0; k < kinds.size(); ++k) {
			const FileCacheStats t = total(kinds[k].c_str());
			uint n = 0;
			for (uint i = 0; i < l.size(); ++i)
				n += l[i]->kind == kinds[k];
			if (k)
				out += sep;
			out += String::format("%s n=%u used=%u cap=%u lookups=%u hits=%u reads=%u read=%u", kinds[k].c_str(), n,
								  (t.used + 1023) / 1024, (t.capacity + 1023) / 1024, t.lookups, t.hits, t.reads,
								  (t.readBytes + 1023) / 1024);
		}
		return out;
	}

private:
	static Array<FileCacheStats *> &list() {
		static Array<FileCacheStats *> caches;
		return caches;
	}
};

} // End of namespace Common

#endif
