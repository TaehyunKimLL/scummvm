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

#ifndef BACKENDS_MIXER_DOS_PREFETCH_H
#define BACKENDS_MIXER_DOS_PREFETCH_H

#include "audio/audiostream.h"
#include "audio/mixer_intern.h"
#include "common/scummsys.h"
#include "common/util.h"

namespace DOS {

/**
 * Decode-ahead rings for the mixer's expensive streams: speech and music
 * (FLAC, Vorbis, VOC and WAV read from the file as they play).
 *
 * On DOS the mixer's Common::Mutex is cli, and MixerImpl::mixCallback()
 * reads every stream under it: a FLAC frame decoded there holds IRQ0 off
 * for 14-42 ms (the FLAC spike) and getMillis() falls behind. A wrapped
 * stream is read ahead into a ring by prefetchAll(), interrupts on,
 * before each mix piece; under the mutex its proxy only copies. Teardown
 * waits too: when the mixer deletes a proxy (a finished line, stopHandle())
 * the slot is only marked, and reap() - interrupts on - deletes the decoder
 * and closes its file.
 *
 * Contexts: wrap() and its prime run on the main thread; prefetchAll() and
 * reap() on SDL3's audio thread, which is cooperative and runs only while
 * the main thread yields, so the two never interleave. A proxy may be
 * deleted from any context, an interrupt included: that only sets its
 * slot's state, which nothing but reap() acts on. A wrapped stream's
 * readBuffer() and destructor must never call SDL or yield: prefetchAll()
 * is safe against wrap() and reap() only because they cannot interleave.
 */
class PrefetchPool {
public:
	static const int kSlots = 4;
	/** Samples per ring: 32 KB, 186 ms of 44.1 kHz stereo or 372 ms mono. */
	static const int kRingSamples = 16384;
	/** Samples decoded on the main thread when a stream starts. */
	static const int kPrimeSamples = 4096;

	enum State { kFree = 0, kLive = 1, kOrphan = 2 };

	struct Slot {
		volatile int state;
		Audio::AudioStream *parent;
		int16 *ring;
		int head;		///< next sample to read
		int fill;		///< samples held
		int channels;
		int markLeft;	///< marker samples still to send before the stream's own
		int markPos;	///< marker samples sent
		int markPeriod;	///< marker period, in frames
	};

	PrefetchPool() : _misses(0), _declined(0) {
		for (int i = 0; i < kSlots; i++) {
			Slot &s = _slots[i];
			s.state = kFree;
			s.parent = nullptr;
			s.ring = nullptr;
			s.head = s.fill = 0;
			s.channels = 1;
			s.markLeft = s.markPos = 0;
			s.markPeriod = 2;
		}
	}

	/** Deletes every stream still held. Delete the mixer (and so every proxy) first. */
	~PrefetchPool() {
		for (int i = 0; i < kSlots; i++)
			release(_slots[i]);
	}

	/**
	 * Main thread. A proxy that reads @p parent through a ring, primed with
	 * kPrimeSamples; @p parent itself when no slot or memory is free.
	 * @p markMillis > 0 puts a 1 kHz square of that length in front of the
	 * stream (dos_audio_mark: the lip-sync measurement finds it in a recording).
	 */
	Audio::AudioStream *wrap(Audio::AudioStream *parent, int markMillis = 0);

	/** Audio thread, interrupts on: tops every live ring up. */
	void prefetchAll() {
		for (int i = 0; i < kSlots; i++)
			if (_slots[i].state == kLive)
				fill(_slots[i], kRingSamples);
	}

	/** Interrupts on: deletes the streams whose proxies the mixer let go of. */
	void reap() {
		for (int i = 0; i < kSlots; i++)
			if (_slots[i].state == kOrphan)
				release(_slots[i]);
	}

	/** Reads a ring could not serve, which were decoded under the mixer's mutex. */
	uint32 misses() const { return _misses; }

	/**
	 * Streams that should have been wrapped and were not: no free slot, no
	 * memory, or the mixer's guard refused (a timer proc in IRQ0). Counted
	 * only: wrap() may run in an interrupt, which must not log.
	 */
	uint32 declined() const { return _declined; }
	/** One atomic increment: a timer proc in IRQ0 may race the main thread. */
	void noteDeclined() { __sync_fetch_and_add(&_declined, 1u); }

	int countSlots(State st) const {
		int n = 0;
		for (int i = 0; i < kSlots; i++)
			n += (_slots[i].state == st) ? 1 : 0;
		return n;
	}

	/** The proxy's readBuffer(), under the mixer's mutex: copies; decodes only on a miss. */
	int read(Slot &s, int16 *buf, int n) {
		int done = 0;
		while (done < n && s.markLeft > 0) {
			const int frame = s.markPos / s.channels;
			buf[done++] = ((frame / (s.markPeriod / 2)) & 1) ? -16000 : 16000;
			s.markPos++;
			s.markLeft--;
		}
		while (done < n && s.fill > 0) {
			const int k = MIN(n - done, MIN(s.fill, kRingSamples - s.head));
			memcpy(buf + done, s.ring + s.head, k * sizeof(int16));
			s.head = (s.head + k) % kRingSamples;
			s.fill -= k;
			done += k;
		}
		if (done < n && !s.parent->endOfData()) {
			_misses++;
			const int got = s.parent->readBuffer(buf + done, n - done);
			if (got > 0)
				done += got;
		}
		return done;
	}

private:
	void fill(Slot &s, int limit) {
		while (s.fill < limit && !s.parent->endOfData()) {
			const int tail = (s.head + s.fill) % kRingSamples;
			int n = MIN(limit - s.fill, kRingSamples - tail);
			n -= n % s.channels;	// whole frames: the ring's edge stays on a frame
			if (n <= 0)
				break;
			const int got = s.parent->readBuffer(s.ring + tail, n);
			if (got <= 0)
				break;	// nothing now (a queue that is empty for the moment)
			s.fill += got;
		}
	}

	void release(Slot &s) {
		if (s.state == kFree)
			return;
		delete s.parent;
		free(s.ring);
		s.parent = nullptr;
		s.ring = nullptr;
		s.head = s.fill = 0;
		s.markLeft = s.markPos = 0;
		s.state = kFree;
	}

	Slot _slots[kSlots];
	uint32 _misses;
	volatile uint32 _declined;
};

/** What the mixer's channel holds in place of a wrapped stream. */
class PrefetchProxy : public Audio::AudioStream {
public:
	PrefetchProxy(PrefetchPool &pool, PrefetchPool::Slot &slot, int rate, bool stereo)
		: _pool(pool), _slot(slot), _rate(rate), _stereo(stereo) {}
	~PrefetchProxy() override { _slot.state = PrefetchPool::kOrphan; }

	int readBuffer(int16 *buffer, const int numSamples) override { return _pool.read(_slot, buffer, numSamples); }
	bool isStereo() const override { return _stereo; }
	int getRate() const override { return _rate; }
	bool endOfData() const override {
		return _slot.markLeft == 0 && _slot.fill == 0 && _slot.parent->endOfData();
	}
	bool endOfStream() const override {
		return _slot.markLeft == 0 && _slot.fill == 0 && _slot.parent->endOfStream();
	}

private:
	PrefetchPool &_pool;
	PrefetchPool::Slot &_slot;
	const int _rate;
	const bool _stereo;
};

inline Audio::AudioStream *PrefetchPool::wrap(Audio::AudioStream *parent, int markMillis) {
	if (!parent)
		return nullptr;
	reap();
	Slot *s = nullptr;
	for (int i = 0; i < kSlots && !s; i++)
		if (_slots[i].state == kFree)
			s = &_slots[i];
	if (!s) {
		noteDeclined();
		return parent;
	}
	int16 *ring = (int16 *)malloc(kRingSamples * sizeof(int16));
	if (!ring) {
		noteDeclined();
		return parent;
	}
	const bool stereo = parent->isStereo();
	const int rate = parent->getRate();
	s->parent = parent;
	s->ring = ring;
	s->head = s->fill = 0;
	s->channels = stereo ? 2 : 1;
	s->markPeriod = MAX(2, rate / 1000);
	s->markLeft = (markMillis > 0) ? rate * markMillis / 1000 * s->channels : 0;
	s->markPos = 0;
	fill(*s, kPrimeSamples);
	s->state = kLive;
	return new PrefetchProxy(*this, *s, rate, stereo);
}

/**
 * MixerImpl that wraps the speech and music streams it is given to own
 * (DisposeAfterUse::YES) in the pool's rings. Streams it borrows, and
 * sound effects (short, in memory), play as they are.
 */
class PrefetchMixer : public Audio::MixerImpl {
public:
	typedef void (*SpeechHook)(void *ctx);
	typedef bool (*WrapGuard)();
	typedef uint32 (*LatencyFn)(void *ctx);

	PrefetchMixer(PrefetchPool &pool, uint sampleRate, bool stereo, uint outBufSize)
		: Audio::MixerImpl(sampleRate, stereo, outBufSize), _pool(pool), _hook(nullptr), _hookCtx(nullptr),
		  _guard(nullptr), _latencyFn(nullptr), _latencyCtx(nullptr), _markMillis(0), _speechStarts(0), _musicStarts(0) {}

	/** Called at every speech stream's start, before its prime. */
	void setSpeechHook(SpeechHook fn, void *ctx) {
		_hook = fn;
		_hookCtx = ctx;
	}
	/** Where getOutputLatencyMillis() comes from (DOS: the Sound Blaster's queue). */
	void setLatencyProvider(LatencyFn fn, void *ctx) {
		_latencyFn = fn;
		_latencyCtx = ctx;
	}
	uint32 getOutputLatencyMillis() const override { return _latencyFn ? _latencyFn(_latencyCtx) : 0; }
	/** When it returns false the stream plays unwrapped (DOS: interrupts are off). */
	void setWrapGuard(WrapGuard fn) { _guard = fn; }
	/** dos_audio_mark: a 1 kHz square of this length in front of every speech stream. */
	void setMarkMillis(int ms) { _markMillis = ms; }
	uint32 speechStarts() const { return _speechStarts; }
	uint32 musicStarts() const { return _musicStarts; }

	void playStream(SoundType type, Audio::SoundHandle *handle, Audio::AudioStream *input, int id, byte volume,
	                int8 balance, DisposeAfterUse::Flag autofreeStream, bool permanent, bool reverseStereo) override {
		if (input && autofreeStream == DisposeAfterUse::YES && (type == kSpeechSoundType || type == kMusicSoundType)) {
			if (type == kSpeechSoundType) {
				_speechStarts++;
				if (_hook)
					_hook(_hookCtx);
			} else {
				_musicStarts++;
			}
			if (!_guard || _guard())
				input = _pool.wrap(input, type == kSpeechSoundType ? _markMillis : 0);
			else
				_pool.noteDeclined();
		}
		Audio::MixerImpl::playStream(type, handle, input, id, volume, balance, autofreeStream, permanent, reverseStereo);
	}

private:
	PrefetchPool &_pool;
	SpeechHook _hook;
	void *_hookCtx;
	WrapGuard _guard;
	LatencyFn _latencyFn;
	void *_latencyCtx;
	int _markMillis;
	uint32 _speechStarts;
	uint32 _musicStarts;
};

} // End of namespace DOS

#endif
