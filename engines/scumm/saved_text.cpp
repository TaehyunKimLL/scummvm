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

#include "common/endian.h"

#include "scumm/saved_text.h"

namespace Scumm {

static const uint32 kSavedTextTag = MKTAG('T', 'E', 'N', 'C');

void writeSavedTextEncoding(Common::WriteStream &out, bool utf8) {
	out.writeUint32BE(kSavedTextTag);
	out.writeByte(utf8 ? 1 : 0);
}

SavedTextEncoding readSavedTextEncoding(Common::SeekableReadStream &in) {
	if (in.size() - in.pos() < 5)
		return kSavedTextUnknown;
	if (in.readUint32BE() != kSavedTextTag)
		return kSavedTextUnknown;
	const byte v = in.readByte();
	if (in.err())
		return kSavedTextUnknown;
	return v ? kSavedTextUtf8 : kSavedTextLegacy;
}

} // End of namespace Scumm
