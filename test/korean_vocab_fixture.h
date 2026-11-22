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

// Korean word tables built in memory for the tests of common/korean_vocab (and later the engines' parser tests):
// the layouts of common/korean_vocab.h, version 2 and 3, rows sorted by code units with the rows of one word kept in
// the order given (a sequence must stay ordered), as tools/korean/k6bake.py writes them.

#ifndef TEST_KOREAN_VOCAB_FIXTURE_H
#define TEST_KOREAN_VOCAB_FIXTURE_H

#include <common/array.h>
#include <common/endian.h>
#include <common/str.h>
#include <common/ustr.h>

namespace KoreanVocabFixture {

struct Row {
	const char *utf8;
	uint16 group;
	uint16 cls;
};

inline int compareUnits(const Common::U32String &a, const Common::U32String &b) {
	const uint n = MIN<uint>(a.size(), b.size());
	for (uint i = 0; i < n; ++i) {
		if (a[i] != b[i])
			return a[i] < b[i] ? -1 : 1;
	}
	return a.size() == b.size() ? 0 : (a.size() < b.size() ? -1 : 1);
}

inline void put16(Common::Array<byte> &out, uint16 v) {
	out.push_back((byte)(v & 0xFF));
	out.push_back((byte)(v >> 8));
}

inline void put32(Common::Array<byte> &out, uint32 v) {
	put16(out, (uint16)(v & 0xFFFF));
	put16(out, (uint16)(v >> 16));
}

/**
 * A table of @p rows. @p version 2 or 3; for 3, @p header is the header line (TranslationFileHeader::toString()),
 * written NUL-terminated and padded to a multiple of 4.
 */
inline Common::Array<byte> build(const char *magic6, const Row *rows, uint count, uint32 version = 2,
                                 const char *header = "") {
	Common::Array<Common::U32String> words;
	Common::Array<uint> order;
	for (uint i = 0; i < count; ++i) {
		words.push_back(Common::String(rows[i].utf8).decode(Common::kUtf8));
		order.push_back(i);
	}
	// Insertion sort: stable, so the rows of one word keep their order.
	for (uint i = 1; i < order.size(); ++i) {
		const uint v = order[i];
		uint j = i;
		while (j > 0 && compareUnits(words[order[j - 1]], words[v]) > 0) {
			order[j] = order[j - 1];
			--j;
		}
		order[j] = v;
	}
	Common::Array<byte> out;
	char magic[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	memcpy(magic, magic6, MIN<size_t>(strlen(magic6), 6));
	for (uint i = 0; i < 8; ++i)
		out.push_back((byte)magic[i]);
	put32(out, version);
	put32(out, count);
	if (version == 3) {
		const uint32 len = (uint32)strlen(header) + 1;
		const uint32 padded = (len + 3) & ~3u;
		put32(out, padded);
		for (uint32 i = 0; i < padded; ++i)
			out.push_back(i < len - 1 ? (byte)header[i] : 0);
	}
	Common::Array<byte> blob;
	uint32 offset = 0;
	for (uint k = 0; k < order.size(); ++k) {
		const Common::U32String &w = words[order[k]];
		if (version == 3)
			put32(out, offset);
		else
			put16(out, (uint16)offset);
		put16(out, (uint16)w.size());
		put16(out, rows[order[k]].group);
		put16(out, rows[order[k]].cls);
		if (version == 3)
			put16(out, 0);
		for (uint i = 0; i < w.size(); ++i)
			put16(blob, (uint16)w[i]);
		offset += w.size();
	}
	for (uint i = 0; i < blob.size(); ++i)
		out.push_back(blob[i]);
	return out;
}

} // End of namespace KoreanVocabFixture

#endif
