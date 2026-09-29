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

#include <SDL3/SDL.h>

#include "backends/mixer/dos/dos-mixer.h"
#include "common/config-manager.h"
#include "common/debug.h"
#include "common/textconsole.h"

DosMixerManager::DosMixerManager()
	: _stream(nullptr), _buffer(new byte[kBufferBytes]), _subsystemInitialized(false), _framesMixed(0) {
}

DosMixerManager::~DosMixerManager() {
	if (_mixer)
		_mixer->setReady(false);
	if (_stream)
		SDL_DestroyAudioStream(_stream);	// closes the device it opened
	if (_subsystemInitialized)
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
	delete[] _buffer;
}

void DosMixerManager::init() {
	// The Sound Blaster driver keeps four device buffers in its ring:
	// at 44100 Hz, 1024 frames give ~93 ms of cushion for a main thread
	// that does not yield.
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "1024");
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		warning("DOS: no audio: %s", SDL_GetError());
		return;
	}
	_subsystemInitialized = true;

	SDL_AudioSpec spec;
	int frames = 0;
	if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &frames)) {
		warning("DOS: no audio device: %s", SDL_GetError());
		return;
	}
	if (ConfMan.hasKey("output_rate") && ConfMan.getInt("output_rate") > 0)
		spec.freq = ConfMan.getInt("output_rate");
	spec.format = SDL_AUDIO_S16;
	spec.channels = 2;
	_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdlCallback, this);
	if (!_stream) {
		warning("DOS: no audio device: %s", SDL_GetError());
		return;
	}

	SDL_AudioSpec device;
	if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(_stream), &device, &frames))
		debug(1, "DOS: audio %s at %d Hz, %d channels, format 0x%x, %d frames; mixer at %d Hz",
			SDL_GetCurrentAudioDriver(), device.freq, device.channels, (uint)device.format, frames, spec.freq);

	_mixer = new Audio::MixerImpl(spec.freq, true, kDeviceFrames);
	_mixer->setReady(true);
	// Streams from SDL_OpenAudioDeviceStream() start paused.
	SDL_ResumeAudioStreamDevice(_stream);
}

// On SDL3's audio thread, interrupts on.
void DosMixerManager::sdlCallback(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount) {
	DosMixerManager *manager = (DosMixerManager *)userdata;
	// Whole frames (4 bytes); more than asked for is fine.
	uint left = (additionalAmount > 0) ? ((uint)additionalAmount + 3) & ~3u : 0;
	while (left > 0) {
		const uint n = MIN(left, kBufferBytes);
		for (uint off = 0; off < n; off += kMixPieceBytes)
			manager->_mixer->mixCallback(manager->_buffer + off, MIN(kMixPieceBytes, n - off));
		// Interrupts are on again: SDL may be called.
		SDL_PutAudioStreamData(stream, manager->_buffer, n);
		manager->_framesMixed += n / 4;
		left -= n;
	}
}

void DosMixerManager::suspendAudio() {
	if (_stream)
		SDL_PauseAudioStreamDevice(_stream);
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

#endif
