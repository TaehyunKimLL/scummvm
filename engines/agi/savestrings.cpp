/* ScummVM - Graphic Adventure Engine (GPLv3+, see translation.h for the header) */

#include "common/array.h"
#include "agi/savestrings.h"
#include "agi/textcells.h"
#include "agi/translation.h"

namespace Agi {

static void writeOne(Common::WriteStream &out, const char *s, uint maxLen) {
	uint len = 0;
	while (len < maxLen && s[len])
		len++;
	out.writeByte((byte)len);
	out.write(s, len);
}

void writeUtf8Strings(Common::WriteStream &out, const char *table, uint stride, uint count, const Common::String *answers) {
	const uint maxLen = MIN<uint>(stride - 1, 255);
	for (uint i = 0; i < count; i++)
		writeOne(out, table + i * stride, maxLen);
	for (uint i = 0; i < count; i++)
		writeOne(out, answers[i].c_str(), maxLen);
}

static bool readOne(Common::ReadStream &in, char *dst, uint stride) {
	const uint len = in.readByte();
	if (in.eos() || len >= stride)
		return false;
	if (in.read(dst, len) != len)
		return false;
	dst[len] = 0;
	return true;
}

bool readUtf8Strings(Common::ReadStream &in, char *table, uint stride, uint count, Common::String *answers) {
	for (uint i = 0; i < count; i++) {
		if (!readOne(in, table + i * stride, stride))
			return false;
	}
	Common::Array<char> buf;
	buf.resize(stride);
	for (uint i = 0; i < count; i++) {
		if (!readOne(in, buf.data(), stride))
			return false;
		answers[i] = buf.data();
	}
	return true;
}

void convertLegacyStrings(char *table, uint stride, uint count, int16 maxCells) {
	for (uint i = 0; i < count; i++) {
		char *slot = table + i * stride;
		if (!TextCells::hasHighBytes(slot))
			continue;
		const Common::String u = TextCells::clipCells(TextOverlay::legacyToUtf8(slot, Common::kDos850).c_str(), maxCells, true);
		Common::strlcpy(slot, u.c_str(), stride);
	}
}

} // End of namespace Agi
