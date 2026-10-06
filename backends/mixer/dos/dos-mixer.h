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
#include "backends/mixer/dos/dos-audio-stats.h"

struct SDL_AudioStream;

namespace DOS {
class PrefetchPool;
class PrefetchMixer;
}

/**
 * ScummVM's mixer on SDL3's DOS Sound Blaster driver: 16-bit stereo,
 * dos_audio_frames-frame device buffers (4096 by default), mixed at the
 * rate the card was opened at.
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
 * unconditional sti. Speech and music streams are decoded ahead outside
 * the mutex (prefetch.h): before each piece the callback tops their rings
 * up (by what they took in the last two pieces, plus a burst of 1024
 * samples per piece; a callback mixes several pieces), interrupts on, and
 * after the last it frees what the mixer let go of.
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

	/**
	 * Milliseconds between handing the mixer a sample now and the card
	 * playing it, as far as can be told: what SDL's stream and the Sound
	 * Blaster's ring hold, the DMA half queued at the last interrupt and,
	 * on average, half of the one playing. 0 without a Sound Blaster.
	 * Interrupts on only (DOS_SBGetStats re-enables them).
	 */
	uint32 outputLatencyMillis() const;

	/** The counters behind the debug socket's `audio` (DOS::audioStats()). */
	void fillStats(DOS::AudioStats &s);

private:
	static void sdlCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount);
	static void onSpeech(void *ctx);

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
	DOS::PrefetchPool *_pool;
	DOS::PrefetchMixer *_prefetchMixer;	///< _mixer, as what it is
	int _deviceFrames;		///< dos_audio_frames
	int _mixRate;			///< the mixer's rate
	int _devRate;			///< the card's rate
	int _devBytesPerFrame;	///< the card's sample format x channels
	bool _soundBlaster;		///< SDL's driver is "soundblaster"
	DOS::AudioStats _stats;	///< pieces, pieceMaxTsc, prefetchTsc, mixTsc
	bool _haveTsc;
	bool _mark;				///< dos_audio_mark
	uint32 _declinedLogged;	///< PrefetchPool::declined() as last logged
};

#endif
