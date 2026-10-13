/* ScummVM - Graphic Adventure Engine
 * (GPLv3 or later; see COPYING)
 */

// A SCIKOR v2 layout table built in memory, for the word-table tests of
// common/ and engines/agi/: harness/k6bake.py's format (8-byte magic,
// u32 version 2, u32 count, count * {u16 offset, u16 len, u16 group,
// u16 class}, then UTF-16LE code units), rows sorted by code units with
// rows of one word kept in the order given (a sequence must stay ordered).

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

inline Common::Array<byte> build(const char *magic6, const Row *rows, uint count) {
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
	char magic[8] = { 0 };
	memcpy(magic, magic6, 6);
	for (uint i = 0; i < 8; ++i)
		out.push_back((byte)magic[i]);
	byte head[8];
	WRITE_LE_UINT32(head, 2);
	WRITE_LE_UINT32(head + 4, count);
	for (uint i = 0; i < 8; ++i)
		out.push_back(head[i]);
	Common::Array<byte> blob;
	uint16 offset = 0;
	for (uint k = 0; k < order.size(); ++k) {
		const Common::U32String &w = words[order[k]];
		byte e[8];
		WRITE_LE_UINT16(e, offset);
		WRITE_LE_UINT16(e + 2, (uint16)w.size());
		WRITE_LE_UINT16(e + 4, rows[order[k]].group);
		WRITE_LE_UINT16(e + 6, rows[order[k]].cls);
		for (uint i = 0; i < 8; ++i)
			out.push_back(e[i]);
		for (uint i = 0; i < w.size(); ++i) {
			blob.push_back((byte)(w[i] & 0xFF));
			blob.push_back((byte)(w[i] >> 8));
		}
		offset += (uint16)w.size();
	}
	for (uint i = 0; i < blob.size(); ++i)
		out.push_back(blob[i]);
	return out;
}

} // End of namespace KoreanVocabFixture

#endif
