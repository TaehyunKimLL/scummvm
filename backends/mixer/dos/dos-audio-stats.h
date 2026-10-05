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

#ifndef BACKENDS_MIXER_DOS_AUDIO_STATS_H
#define BACKENDS_MIXER_DOS_AUDIO_STATS_H

#include "common/scummsys.h"
#include "common/str.h"

namespace DOS {

/**
 * What the DOS mixer has done so far, for the debug socket's `audio`
 * command (the harness's audio gate). Times are time stamp counter cycles
 * (0 without a TSC); the Sound Blaster fields come from
 * sdl3-sb-stats.patch and are -1 without a Sound Blaster.
 */
struct AudioStats {
	uint32 millis = 0;		///< getMillis() at the reading
	uint64 tsc = 0;			///< the TSC at the reading
	uint32 pieces = 0;		///< interrupts-off mix pieces so far
	uint64 pieceMaxTsc = 0;	///< the longest of them since the last reading
	uint64 prefetchTsc = 0;	///< decoding ahead so far (interrupts on)
	uint64 mixTsc = 0;		///< mixing so far (interrupts off)
	uint32 misses = 0;		///< reads a ring could not serve
	uint32 speech = 0;		///< speech streams started
	uint32 music = 0;		///< music streams started (CD tracks)
	int sbIrqs = -1;		///< Sound Blaster interrupts so far
	int sbUnderruns = -1;	///< of them, with the ring empty (silence played)
	int sbQueued = -1;		///< bytes in the ring now
	int sbMinAvail = -1;	///< least bytes in the ring at an interrupt since the last reading
	int sbChunk = -1;		///< bytes per DMA half
	uint32 rate = 0;		///< mixer rate
	uint32 frames = 0;		///< device buffer frames (dos_audio_frames)
};

inline Common::String formatAudioStats(const AudioStats &s) {
	return Common::String::format(
		"ms=%u tsc=%llu pieces=%u piecemax=%llu prefetch=%llu mix=%llu misses=%u speech=%u music=%u "
		"irqs=%d under=%d queued=%d minavail=%d chunk=%d rate=%u frames=%u",
		(uint)s.millis, (unsigned long long)s.tsc, (uint)s.pieces, (unsigned long long)s.pieceMaxTsc,
		(unsigned long long)s.prefetchTsc, (unsigned long long)s.mixTsc, (uint)s.misses, (uint)s.speech,
		(uint)s.music, s.sbIrqs, s.sbUnderruns, s.sbQueued, s.sbMinAvail, s.sbChunk, (uint)s.rate, (uint)s.frames);
}

/** Fills @p s from the running DOS mixer (dos-mixer.cpp); false without one. */
bool audioStats(AudioStats &s);

} // End of namespace DOS

#endif
