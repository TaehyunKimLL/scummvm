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
 * reads: tag 'TENC' and one byte, 1 for UTF-8 (the jamo branch's format, kept). This engine writes 1. A save without a
 * trailer (upstream, the CP949 builds) or with 0 holds CP949 and is converted on load (ScummEngine::convertLoadedText()).
 */
enum SavedTextEncoding {
	kSavedTextUnknown = -1, ///< no trailer
	kSavedTextLegacy = 0,   ///< CP949
	kSavedTextUtf8 = 1
};

void writeSavedTextEncoding(Common::WriteStream &out, bool utf8);

/** The trailer at the stream's position; kSavedTextUnknown without one. */
SavedTextEncoding readSavedTextEncoding(Common::SeekableReadStream &in);

} // End of namespace Scumm

#endif
