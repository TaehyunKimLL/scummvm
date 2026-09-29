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

#ifndef BACKENDS_MIXER_DOS_H
#define BACKENDS_MIXER_DOS_H

#include "backends/mixer/mixer.h"

struct SDL_AudioStream;

/**
 * ScummVM's mixer on SDL3's DOS Sound Blaster driver: 16-bit stereo,
 * 2048-frame device buffers, mixed at the rate the card was opened at.
 *
 * SDL3 opens the card at its default rate, 44100 Hz, whatever the stream
 * asks for; the driver brings cards before the SB16 down to 22050 Hz.
 * A stream at another rate than the card's is converted on SDL's audio
 * thread, in floating point, which costs a DOS machine far more than the
 * mixer's own rate conversion. So once the device is open the mixer takes
 * the card's rate (as SdlMixerManager does); output_rate in [scummvm]
 * overrides it.
 *
 * SDL3's DOS threads are cooperative. Its audio thread runs only when the
 * main thread yields (SDL_Delay() in delayMillis(), the event pump) and
 * asks sdlCallback() for data then; the Sound Blaster IRQ copies SDL's
 * ring to the DMA buffer. The callback runs MixerImpl::mixCallback(),
 * which holds the mixer's Common::Mutex -- interrupts off -- so it mixes
 * in short pieces (the IRQ0 timer must not miss a tick), and it calls SDL
 * only after each piece, with interrupts back on: SDL3's DOS mutex does an
 * unconditional sti.
 *
 * If there is no audio device, init() leaves the mixer unset (as
 * SdlMixerManager does) and the caller falls back to NullMixerManager.
 */
class DosMixerManager : public MixerManager {
public:
	DosMixerManager();
	~DosMixerManager() override;

	void init() override;

	void suspendAudio() override;
	int resumeAudio() override;

	/**
	 * Sample frames handed to SDL so far. SDL buffers ahead, so this
	 * equals what the card took only over the long run.
	 */
	uint32 framesMixed() const { return _framesMixed; }

	/**
	 * framesMixed() and getMillis() as of the end of the last callback.
	 * The audio thread only runs while the main thread yields, so the
	 * pair is consistent when read between yields.
	 */
	void lastCallback(uint32 &frames, uint32 &millis) const {
		frames = _framesMixed;
		millis = _callbackMillis;
	}

	static const int kDeviceFrames = 2048;

private:
	static void sdlCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount);

	/**
	 * Preallocated, so the callback never allocates. Bigger requests are
	 * served in several passes.
	 */
	static const uint kBufferBytes = 16384;
	/**
	 * One mixCallback() call, interrupts off: 256 frames (5.8 ms at
	 * 44100 Hz) mix in ~0.14 ms per channel on DOSBox at 60000 cycles,
	 * well inside the IRQ0 timer's millisecond.
	 */
	static const uint kMixPieceBytes = 256 * 4;

	SDL_AudioStream *_stream;
	byte *_buffer;
	bool _subsystemInitialized;
	volatile uint32 _framesMixed;
	volatile uint32 _callbackMillis;
};

#endif
