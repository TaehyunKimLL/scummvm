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


#include "common/array.h"
#include "common/str-enc.h"
#include "common/util.h"

#include "agi/saved_strings.h"
#include "agi/textcells.h"

namespace Agi {

void legacySlot(const char *text, char out[kLegacySlotBytes]) {
	memset(out, 0, kLegacySlotBytes);
	const Common::String cut = TextCells::clipBytes(text, kLegacySlotBytes - 1, true);
	memcpy(out, cut.c_str(), cut.size());
}

static void writeOne(Common::WriteStream &out, const char *s, uint stride) {
	// A slot is at most stride - 1 bytes by construction (setString()); the
	// clip only guards a table someone else filled.
	const Common::String cut = TextCells::clipBytes(s, (int16)MIN<uint>(stride - 1, 255), true);
	out.writeByte((byte)cut.size());
	out.write(cut.c_str(), cut.size());
}

void writeSavedStrings(Common::WriteStream &out, const char *table, uint stride, uint count, const Common::String *answers) {
	out.writeUint32BE(kSavedStringsTag);
	out.writeByte(kSavedStringsVersion);
	out.writeByte((byte)count);
	for (uint i = 0; i < count; i++)
		writeOne(out, table + i * stride, stride);
	for (uint i = 0; i < count; i++)
		writeOne(out, answers[i].c_str(), stride);
}

static bool readOne(Common::ReadStream &in, uint stride, Common::String &out) {
	const uint len = in.readByte();
	if (in.eos() || in.err() || len >= stride)
		return false;
	Common::Array<char> buf;
	buf.resize(len + 1);
	if (len && in.read(buf.data(), len) != len)
		return false;
	buf[len] = 0;
	if (strlen(buf.data()) != len || !TextCells::isValidUtf8(buf.data()))
		return false;
	out = Common::String(buf.data(), len);
	return true;
}

SavedStringsResult readSavedStrings(Common::ReadStream &in, char *table, uint stride, uint count, Common::String *answers) {
	const uint32 tag = in.readUint32BE();
	if (in.eos() || tag != kSavedStringsTag)
		return kSavedStringsNone;
	const byte version = in.readByte();
	const byte n = in.readByte();
	if (in.eos() || version != kSavedStringsVersion || n != count)
		return kSavedStringsBroken;
	Common::Array<Common::String> strings, originals;
	strings.resize(count);
	originals.resize(count);
	for (uint i = 0; i < count; i++)
		if (!readOne(in, stride, strings[i]))
			return kSavedStringsBroken;
	for (uint i = 0; i < count; i++)
		if (!readOne(in, stride, originals[i]))
			return kSavedStringsBroken;
	for (uint i = 0; i < count; i++) {
		memset(table + i * stride, 0, stride);
		memcpy(table + i * stride, strings[i].c_str(), strings[i].size());
		answers[i] = originals[i];
	}
	return kSavedStringsRead;
}

void convertLegacyStrings(char *table, uint stride, uint count, int16 maxCells) {
	for (uint i = 0; i < count; i++) {
		char *slot = table + i * stride;
		if (!TextCells::hasHighBytes(slot))
			continue;
		const Common::String u = Common::String(slot).decode(Common::kDos850).encode(Common::kUtf8);
		const Common::String cells = TextCells::clipCells(u.c_str(), maxCells, true);
		const Common::String cut = TextCells::clipBytes(cells.c_str(), (int16)(stride - 1), true);
		memset(slot, 0, stride);
		memcpy(slot, cut.c_str(), cut.size());
	}
}

} // End of namespace Agi
