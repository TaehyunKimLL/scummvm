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

#ifndef SCUMM_SAVED_TEXT_H
#define SCUMM_SAVED_TEXT_H

#include "common/stream.h"

namespace Scumm {

/**
 * The encoding of the translated text a Korean save holds (verbs, object and actor names, strings, the message being
 * printed: what loadPtrToResource() stored). A trailer after the serialized state, where a build without it never
 * reads: tag 'TENC' and one byte, 1 for UTF-8 (the jamo branch's format, kept) or 2 for UTF-8 with the rest of the message buffer. This engine writes 1. A save without a
 * trailer (upstream, the CP949 builds) or with 0 holds CP949 and is converted on load (ScummEngine::convertLoadedText()).
 */
enum SavedTextEncoding {
	kSavedTextUnknown = -1, ///< no trailer
	kSavedTextLegacy = 0,   ///< CP949
	kSavedTextUtf8 = 1
};

/** The size of the rest of the message buffer a trailer of kind 2 carries (the state itself syncs the first 256 bytes only). */
enum { kSavedTextTailSize = 256 };

/**
 * Writes the trailer: 'TENC' and 0 (CP949) or 1 (UTF-8). With @p tail (kSavedTextTailSize bytes: _charsetBuffer[256..511])
 * a UTF-8 trailer is kind 2 and carries them, so a message of 256 bytes or more round-trips (Zak, Indy3 and Indy4 have
 * such lines as UTF-8); a build that knows only kind 1 reads nothing of it.
 */
void writeSavedTextEncoding(Common::WriteStream &out, bool utf8, const byte *tail = nullptr);

/**
 * The trailer at the stream's position; kSavedTextUnknown without one. Kind 2 gives kSavedTextUtf8 and its @p tail
 * (kSavedTextTailSize bytes) with *@p hasTail set; kind 1 and a cut kind 2 give no tail.
 */
SavedTextEncoding readSavedTextEncoding(Common::SeekableReadStream &in, byte *tail = nullptr, bool *hasTail = nullptr);

} // End of namespace Scumm

#endif
