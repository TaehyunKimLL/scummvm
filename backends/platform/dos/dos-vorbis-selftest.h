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

#ifndef BACKENDS_PLATFORM_DOS_VORBIS_SELFTEST_H
#define BACKENDS_PLATFORM_DOS_VORBIS_SELFTEST_H

#include "common/str.h"

namespace DOS {

/**
 * dos_vorbis_selftest=<speech file>: opens @p clips clips of an Ogg Vorbis
 * speech file (monkey2.sog) the way SCUMM does for a talk line (a File, a
 * SeekableSubReadStream, makeVorbisStream; spread evenly over the index, or
 * the consecutive clips from index @p from if that is not negative), decodes
 * the first 4096 frames (a conservative stand-in for what the prefetch ring
 * reads ahead of a new line; the mixer itself no longer primes) and then
 * the rest, with the TSC, and logs one "DOS: vorbis selftest" line. Does
 * nothing, and says so, without Vorbis.
 */
void vorbisSelftest(const Common::String &path, uint clips, int from);

} // End of namespace DOS

#endif
