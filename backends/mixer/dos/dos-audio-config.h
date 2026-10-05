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

#ifndef BACKENDS_MIXER_DOS_AUDIO_CONFIG_H
#define BACKENDS_MIXER_DOS_AUDIO_CONFIG_H

#include "common/scummsys.h"
#include "common/str.h"

namespace DOS {

/** dos_audio_frames' default: 93 ms buffers at 44100 Hz; the driver queues DOS_SBRingChunks of them (5 x 93 ms). */
const int kDefaultAudioFrames = 4096;
const int kMinAudioFrames = 512;
/** 8192 frames make a 64 KB DMA buffer: SDL's allocator then asks for 128 KB and can cross a 128 KB page. */
const int kMaxAudioFrames = 4096;

/**
 * The device buffer size dos_audio_frames asks for: decimal digits, rounded
 * down to a power of two (the driver's ring needs one); anything else, or a
 * value outside [512, 4096], gives the default.
 */
inline int audioDeviceFrames(const Common::String &value) {
	if (value.empty() || value.size() > 6)
		return kDefaultAudioFrames;
	int v = 0;
	for (uint i = 0; i < value.size(); i++) {
		const char c = value[i];
		if (c < '0' || c > '9')
			return kDefaultAudioFrames;
		v = v * 10 + (c - '0');
	}
	if (v < kMinAudioFrames || v > kMaxAudioFrames)
		return kDefaultAudioFrames;
	int p = kMinAudioFrames;
	while (p * 2 <= v)
		p *= 2;
	return p;
}

} // End of namespace DOS

#endif
