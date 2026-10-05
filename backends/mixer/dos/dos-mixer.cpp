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

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include <pc.h>
#include <SDL3/SDL.h>

#include "backends/mixer/dos/dos-mixer.h"
#include "backends/mixer/dos/dos-audio-config.h"
#include "backends/mixer/dos/dos-audio-stats.h"
#include "backends/mixer/dos/prefetch.h"
#include "backends/platform/dos/dos-exit.h"
#include "backends/platform/dos/dos-irq.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/system.h"
#include "common/textconsole.h"

// sdl3-sb-stats.patch (link-checked through DOS_SBStatsChecked in dos-irq.cpp).
extern "C" void DOS_SBGetStats(int *irqs, int *underruns, int *queued, int *minAvail, int *chunk, int resetMin);

namespace {

// A stream started with interrupts off (from a timer proc in IRQ0) plays
// unwrapped: wrapping would allocate and decode inside the interrupt.
bool interruptsOn() {
	uint32 flags;
	__asm__ __volatile__("pushfl; popl %0" : "=r"(flags));
	return (flags & 0x200) != 0;
}

DosMixerManager *s_manager = nullptr;

// dos_audio_mark: a 10 ms 2 kHz tone on the PC speaker (PIT channel 2),
// which reaches the speaker at once, unlike the Sound Blaster's queue.
// The click therefore leads the 1 kHz square's onset in the recording by
// about 10 ms (this busy-wait) plus the output latency: an analysis that
// measures speech start from the click must subtract the 10 ms.
void speakerClick() {
	const uint16 div = 1193182 / 2000;
	outportb(0x43, 0xB6);
	outportb(0x42, div & 0xff);
	outportb(0x42, div >> 8);
	outportb(0x61, inportb(0x61) | 3);
	const uint32 t0 = g_system->getMillis();
	while (g_system->getMillis() - t0 < 10) {
	}
	outportb(0x61, inportb(0x61) & ~3);
}

} // End of anonymous namespace

DosMixerManager::DosMixerManager()
	: _stream(nullptr), _buffer(new byte[kBufferBytes]), _subsystemInitialized(false), _framesMixed(0),
	  _callbackMillis(0), _pool(nullptr), _prefetchMixer(nullptr), _deviceFrames(DOS::kDefaultAudioFrames),
	  _mixRate(0), _devRate(0), _devBytesPerFrame(0), _soundBlaster(false), _haveTsc(false), _mark(false),
	  _declinedLogged(0) {
}

DosMixerManager::~DosMixerManager() {
	if (s_manager == this)
		s_manager = nullptr;
	if (_mixer)
		_mixer->setReady(false);
	if (_stream)
		SDL_DestroyAudioStream(_stream);	// closes the device it opened: no callback after this
	if (_subsystemInitialized)
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	DOS::soundBlasterClosed();
	// The mixer's channels hold the proxies, the pool their streams: the
	// mixer goes first (MixerManager's destructor then deletes nothing).
	delete _mixer;
	_mixer = nullptr;
	delete _pool;
	delete[] _buffer;
}

void DosMixerManager::init() {
	// dos_audio_frames: device buffer frames, a power of two from 512 to
	// 4096, 4096 by default. The Sound Blaster driver keeps four buffers in
	// its ring: at 44100 Hz 4096 frames give 93 ms interrupts and 372 ms of
	// cushion for a main thread that does not yield (a room load on a
	// Pentium 75 blocks it for more than 186 ms: the FLAC spike saw 1-2
	// underruns at 2048 frames and none at 4096). The DMA buffer is two
	// buffers (32 KB at 16-bit stereo), and SDL takes twice that below 1 MB
	// so that it does not cross a 64 KB page.
	ConfMan.registerDefault("dos_audio_frames", DOS::kDefaultAudioFrames);
	_deviceFrames = DOS::audioDeviceFrames(ConfMan.get("dos_audio_frames"));
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, Common::String::format("%d", _deviceFrames).c_str());
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		warning("DOS: no audio: %s", SDL_GetError());
		return;
	}
	_subsystemInitialized = true;

	const bool rateSet = ConfMan.hasKey("output_rate") && ConfMan.getInt("output_rate") > 0;
	SDL_AudioSpec spec;
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;
	spec.freq = rateSet ? ConfMan.getInt("output_rate") : 44100;
	_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdlCallback, this);
	if (!_stream) {
		warning("DOS: no audio device: %s", SDL_GetError());
		return;
	}

	// For the way out (DOS::soundBlasterClosed()): SDL3 masks the card's
	// IRQ whatever it was, and leaves an SB16's transfer running.
	const char *driver = SDL_GetCurrentAudioDriver();
	_soundBlaster = driver && strcmp(driver, "soundblaster") == 0;
	if (_soundBlaster)
		DOS::noteSoundBlasterOpen();

	// The card's rate is known only now: SDL asks for 44100 Hz, and the
	// driver brings cards before the SB16 down to 22050. Mix at that rate
	// unless output_rate says otherwise; the device is still paused.
	SDL_AudioSpec device;
	int frames = 0;
	if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(_stream), &device, &frames)) {
		if (!rateSet && device.freq > 0 && device.freq != spec.freq) {
			spec.freq = device.freq;
			if (!SDL_SetAudioStreamFormat(_stream, &spec, nullptr))
				warning("DOS: audio stream at %d Hz: %s", spec.freq, SDL_GetError());
		}
		_devRate = device.freq;
		_devBytesPerFrame = SDL_AUDIO_BYTESIZE(device.format) * device.channels;
		debug(1, "DOS: audio %s at %d Hz, %d channels, format 0x%x, %d frames; mixer at %d Hz",
			SDL_GetCurrentAudioDriver(), device.freq, device.channels, (uint)device.format, frames, spec.freq);
	}
	_mixRate = spec.freq;

	_pool = new DOS::PrefetchPool();
	_prefetchMixer = new DOS::PrefetchMixer(*_pool, spec.freq, true, frames > 0 ? frames : _deviceFrames);
	_prefetchMixer->setWrapGuard(interruptsOn);
	_prefetchMixer->setSpeechHook(onSpeech, this);
	ConfMan.registerDefault("dos_audio_mark", false);
	_mark = ConfMan.getBool("dos_audio_mark");
	if (_mark)
		_prefetchMixer->setMarkMillis(10);
	_haveTsc = DOS::haveTsc();
	_mixer = _prefetchMixer;
	_mixer->setReady(true);
	s_manager = this;
	// Streams from SDL_OpenAudioDeviceStream() start paused.
	SDL_ResumeAudioStreamDevice(_stream);
}

// On SDL3's audio thread, interrupts on.
void DosMixerManager::sdlCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount) {
	DosMixerManager *manager = (DosMixerManager *)userdata;
	DOS::AudioStats &st = manager->_stats;
	const bool tsc = manager->_haveTsc;
	// Whole frames (4 bytes); more than asked for is fine.
	uint left = (additionalAmount > 0) ? ((uint)additionalAmount + 3) & ~3u : 0;
	while (left > 0) {
		const uint n = MIN(left, kBufferBytes);
		for (uint off = 0; off < n; off += kMixPieceBytes) {
			// Interrupts on: decode ahead, so that the piece below
			// (interrupts off) only copies the speech and music rings.
			// Same context as mixCallback() (SDL3's cooperative audio thread;
			// the SB IRQ only drains DMA): no lock.
			const uint64 t0 = tsc ? DOS::irqRdtsc() : 0;
			manager->_pool->prefetchAll();
			const uint64 t1 = tsc ? DOS::irqRdtsc() : 0;
			manager->_mixer->mixCallback(manager->_buffer + off, MIN(kMixPieceBytes, n - off));
			if (tsc) {
				const uint64 t2 = DOS::irqRdtsc();
				st.prefetchTsc += t1 - t0;
				st.mixTsc += t2 - t1;
				if (t2 - t1 > st.pieceMaxTsc)
					st.pieceMaxTsc = t2 - t1;
			}
			st.pieces++;
		}
		// Interrupts are on again: SDL may be called.
		SDL_PutAudioStreamData(stream, manager->_buffer, n);
		manager->_framesMixed += n / 4;
		left -= n;
	}
	// What the mixer let go of (a finished line's decoder and file) is
	// freed here, interrupts on, not under its mutex.
	manager->_pool->reap();
	// Streams that played unwrapped (wrap() cannot log: it may run in an
	// interrupt): say so here, interrupts on.
	const uint32 declined = manager->_pool->declined();
	if (declined != manager->_declinedLogged) {
		manager->_declinedLogged = declined;
		debug(1, "DOS: prefetch declined %u", (uint)declined);
	}
	manager->_callbackMillis = g_system->getMillis();
}

void DosMixerManager::suspendAudio() {
	if (_stream)
		SDL_PauseAudioStreamDevice(_stream);
	// Main thread, interrupts on, audio thread not running: free what the
	// mixer let go of, so a finished line does not keep its file open.
	if (_pool)
		_pool->reap();
	_audioSuspended = true;
}

int DosMixerManager::resumeAudio() {
	if (!_audioSuspended)
		return -2;
	if (_stream && !SDL_ResumeAudioStreamDevice(_stream))
		return -1;
	_audioSuspended = false;
	return 0;
}

uint32 DosMixerManager::outputLatencyMillis() const {
	if (!_stream || !_soundBlaster || _devRate <= 0 || _devBytesPerFrame <= 0 || _mixRate <= 0)
		return 0;
	int irqs, under, queued, minAvail, chunk;
	DOS_SBGetStats(&irqs, &under, &queued, &minAvail, &chunk, 0);
	const int streamBytes = SDL_GetAudioStreamQueued(_stream);
	const uint64 streamMs = (streamBytes > 0) ? (uint64)(streamBytes / 4) * 1000 / _mixRate : 0;
	const int deviceBytes = MAX(0, queued) + MAX(0, chunk) + MAX(0, chunk) / 2;
	const uint64 deviceMs = (uint64)(deviceBytes / _devBytesPerFrame) * 1000 / _devRate;
	return (uint32)(streamMs + deviceMs);
}

void DosMixerManager::fillStats(DOS::AudioStats &s) {
	s = _stats;
	s.millis = g_system->getMillis();
	s.tsc = _haveTsc ? DOS::irqRdtsc() : 0;
	s.misses = _pool ? _pool->misses() : 0;
	s.speech = _prefetchMixer ? _prefetchMixer->speechStarts() : 0;
	s.music = _prefetchMixer ? _prefetchMixer->musicStarts() : 0;
	if (_soundBlaster)
		DOS_SBGetStats(&s.sbIrqs, &s.sbUnderruns, &s.sbQueued, &s.sbMinAvail, &s.sbChunk, 1);
	s.rate = _mixRate;
	s.frames = _deviceFrames;
	_stats.pieceMaxTsc = 0;
}

// Main thread, at every speech stream's start (PrefetchMixer::playStream).
// A line started from a timer proc (interrupts off) is not marked or logged:
// the click waits on getMillis(), and SDL and the log must not be entered.
void DosMixerManager::onSpeech(void *ctx) {
	DosMixerManager *m = (DosMixerManager *)ctx;
	if (!interruptsOn())
		return;
	if (m->_mark)
		speakerClick();
	debug(1, "DOS: speech %u latency %u ms", (uint)m->_prefetchMixer->speechStarts(), (uint)m->outputLatencyMillis());
}

bool DOS::audioStats(DOS::AudioStats &s) {
	if (!s_manager)
		return false;
	s_manager->fillStats(s);
	return true;
}

#endif
