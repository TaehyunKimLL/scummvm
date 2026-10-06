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

/**
 * @file
 * Sound decoder used in engines:
 *  - agos
 *  - draci
 *  - kyra
 *  - qdengine
 *  - queen
 *  - saga
 *  - sci
 *  - scumm
 *  - sword1
 *  - sword2
 *  - sword25
 *  - touche
 *  - tucker
 *  - vcruise
 *  - wintermute
 */

#ifndef AUDIO_VORBIS_H
#define AUDIO_VORBIS_H

#include "common/scummsys.h"
#include "common/types.h"

#ifdef USE_VORBIS

namespace Common {
class SeekableReadStream;
}

namespace Audio {

class SeekableAudioStream;

/**
 * Create a new SeekableAudioStream from the Ogg Vorbis data in the given stream.
 * Allows for seeking (which is why we require a SeekableReadStream).
 *
 * @param stream			the SeekableReadStream from which to read the Ogg Vorbis data
 * @param disposeAfterUse	whether to delete the stream after use
 * @return	a new SeekableAudioStream, or NULL, if an error occurred
 */
SeekableAudioStream *makeVorbisStream(
	Common::SeekableReadStream *stream,
	DisposeAfterUse::Flag disposeAfterUse);

/**
 * Vorbis streams whose setup header (the third header packet: codebooks,
 * floors, residues, mappings) is the same share one decoded copy of it, so
 * that opening a stream does not unpack and expand the codebooks again. A
 * game whose speech is thousands of short clips made by one encoder setting
 * (the Ultimate Talkie editions: a handful of setup headers for all clips)
 * opens each line in a fraction of the time. The decoded audio is the same
 * with or without the cache.
 *
 * The cache keeps the last kDefaultCapacity setups that were used; a setup
 * dropped from it lives on while a stream still uses it. It is shared by
 * all threads (a mutex guards it). Only Tremor builds with the matching
 * private codec_setup layout and VORBIS_SETUP_INTERNALS opt-in have this
 * cache. libvorbis and other Tremor builds report zero stats.
 *
 * The functions below are for tests and measurements.
 */
namespace VorbisSetupCache {

enum {
	kDefaultCapacity = 4
};

struct Stats {
	uint32 hits;	///< streams that took a cached setup
	uint32 misses;	///< streams whose setup was decoded and cached
	uint32 entries;	///< setups in the cache now
};

/** Turn the cache on (the default) or off for streams opened from now on. */
void setEnabled(bool enabled);
/** Keep at most n setups (at least 1); the least recently used go first. */
void setCapacity(uint n);
/** Drop every cached setup and reset the stats. */
void clear();
Stats getStats();
/** Test-only, linked only when VORBIS_SETUP_CACHE_TEST_HOOK builds vorbis.o. */
void forceDuplicateInsertOnce();
uint duplicateInsertFrees();

} // End of namespace VorbisSetupCache

} // End of namespace Audio

#endif // #ifdef USE_VORBIS
#endif // #ifndef AUDIO_VORBIS_H
