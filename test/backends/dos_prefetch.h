#include <cxxtest/TestSuite.h>

#include "backends/mixer/dos/prefetch.h"
#include "../system/null_osystem.h"

namespace {

uint32 fixedLatency(void *ctx) {
	return *(uint32 *)ctx;
}

// Sample i of the stream is (i & 0x7fff), so a reader can check the order.
class FakeStream : public Audio::AudioStream {
public:
	FakeStream(int total, bool stereo, int *deleted, int rate = 44100)
		: _total(total), _pos(0), _stereo(stereo), _rate(rate), _deleted(deleted) {}
	~FakeStream() override { (*_deleted)++; }
	int readBuffer(int16 *buffer, const int numSamples) override {
		const int n = MIN(numSamples, _total - _pos);
		for (int i = 0; i < n; i++)
			buffer[i] = (int16)((_pos + i) & 0x7fff);
		_pos += n;
		return n;
	}
	bool isStereo() const override { return _stereo; }
	int getRate() const override { return _rate; }
	bool endOfData() const override { return _pos >= _total; }
	int pos() const { return _pos; }

private:
	int _total, _pos;
	bool _stereo;
	int _rate;
	int *_deleted;
};

void countCall(void *ctx) {
	(*(int *)ctx)++;
}

bool refuse() {
	return false;
}

} // End of anonymous namespace

class DosPrefetchPoolTestSuite : public CxxTest::TestSuite {
public:
	void test_wrap_primes_on_the_spot_and_only_reap_deletes() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(100000, false, &deleted);
		Audio::AudioStream *p = pool.wrap(f);
		TS_ASSERT_DIFFERS(p, (Audio::AudioStream *)f);
		TS_ASSERT_EQUALS(f->pos(), DOS::PrefetchPool::kPrimeSamples);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kLive), 1);
		delete p;
		TS_ASSERT_EQUALS(deleted, 0);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kOrphan), 1);
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 1);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kFree), DOS::PrefetchPool::kSlots);
	}

	void test_reads_come_in_order_across_the_ring_edge() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		const int total = 3 * DOS::PrefetchPool::kRingSamples + 123;
		Audio::AudioStream *p = pool.wrap(new FakeStream(total, false, &deleted));
		int16 buf[777];
		int got = 0;
		bool inOrder = true;
		while (!p->endOfData()) {
			pool.prefetchAll();
			const int n = p->readBuffer(buf, 777);
			for (int i = 0; i < n; i++)
				inOrder = inOrder && buf[i] == (int16)((got + i) & 0x7fff);
			got += n;
		}
		TS_ASSERT(inOrder);
		TS_ASSERT_EQUALS(got, total);
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		TS_ASSERT(p->endOfStream());
		delete p;
		pool.reap();
	}

	void test_a_dry_ring_decodes_in_place_and_counts_a_miss() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		Audio::AudioStream *p = pool.wrap(new FakeStream(100000, false, &deleted));
		int16 buf[DOS::PrefetchPool::kPrimeSamples];
		TS_ASSERT_EQUALS(p->readBuffer(buf, DOS::PrefetchPool::kPrimeSamples), DOS::PrefetchPool::kPrimeSamples);
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		TS_ASSERT_EQUALS(p->readBuffer(buf, 100), 100);
		TS_ASSERT_EQUALS(buf[0], (int16)DOS::PrefetchPool::kPrimeSamples);
		TS_ASSERT_EQUALS(pool.misses(), 1u);
		delete p;
		pool.reap();
	}

	void test_stereo_reads_from_the_parent_stay_whole_frames() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(20002, true, &deleted);
		Audio::AudioStream *p = pool.wrap(f);
		pool.prefetchAll();
		TS_ASSERT_EQUALS(f->pos() % 2, 0);
		TS_ASSERT(p->isStereo());
		delete p;
		pool.reap();
	}

	void test_a_full_pool_hands_the_stream_back() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		Audio::AudioStream *p[DOS::PrefetchPool::kSlots];
		for (int i = 0; i < DOS::PrefetchPool::kSlots; i++)
			p[i] = pool.wrap(new FakeStream(10000, false, &deleted));
		FakeStream *extra = new FakeStream(10000, false, &deleted);
		TS_ASSERT_EQUALS(pool.declined(), 0u);
		TS_ASSERT_EQUALS(pool.wrap(extra), (Audio::AudioStream *)extra);
		TS_ASSERT_EQUALS(pool.declined(), 1u);
		delete extra;
		for (int i = 0; i < DOS::PrefetchPool::kSlots; i++)
			delete p[i];
		pool.reap();
		TS_ASSERT_EQUALS(deleted, DOS::PrefetchPool::kSlots + 1);
	}

	void test_the_marker_comes_first() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		Audio::AudioStream *p = pool.wrap(new FakeStream(1000, false, &deleted, 44100), 10);
		int16 buf[442];
		TS_ASSERT_EQUALS(p->readBuffer(buf, 442), 442);
		// 441 frames (10 ms) of a 1002 Hz square (period 44 frames), then the stream
		TS_ASSERT_EQUALS(buf[0], 16000);
		TS_ASSERT_EQUALS(buf[21], 16000);
		TS_ASSERT_EQUALS(buf[22], -16000);
		TS_ASSERT_EQUALS(buf[44], 16000);
		TS_ASSERT_EQUALS(buf[441], 0);
		delete p;
		pool.reap();
	}

	void test_the_pool_deletes_what_it_still_holds() {
		int deleted = 0;
		{
			DOS::PrefetchPool pool;
			Audio::AudioStream *a = pool.wrap(new FakeStream(10000, false, &deleted));
			Audio::AudioStream *b = pool.wrap(new FakeStream(10000, false, &deleted));
			delete a;
			delete b;
		}
		TS_ASSERT_EQUALS(deleted, 2);
	}
};

class DosPrefetchMixerTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();	// Channel::mix() reads getMillis()
#endif
	}
	void tearDown() override {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_speech_and_music_are_wrapped_not_sfx_nor_borrowed_streams() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer *mixer = new DOS::PrefetchMixer(pool, 44100, true, 1024);
		mixer->setReady(true);
		Audio::Mixer &m = *mixer;
		Audio::SoundHandle h1, h2, h3, h4;
		FakeStream borrowed(5000, false, &deleted);
		m.playStream(Audio::Mixer::kSpeechSoundType, &h1, new FakeStream(5000, false, &deleted));
		m.playStream(Audio::Mixer::kMusicSoundType, &h2, new FakeStream(5000, true, &deleted));
		m.playStream(Audio::Mixer::kSFXSoundType, &h3, new FakeStream(5000, false, &deleted));
		m.playStream(Audio::Mixer::kSpeechSoundType, &h4, &borrowed, -1, Audio::Mixer::kMaxChannelVolume, 0,
		             DisposeAfterUse::NO);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kLive), 2);
		TS_ASSERT_EQUALS(mixer->speechStarts(), 1u);
		TS_ASSERT_EQUALS(mixer->musicStarts(), 1u);
		TS_ASSERT_EQUALS(pool.declined(), 0u);
		delete mixer;
		TS_ASSERT_EQUALS(deleted, 1);	// the SFX stream; the wrapped two wait for reap()
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 3);
	}

	void test_a_finished_line_is_freed_by_reap_not_under_the_mixer() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer *mixer = new DOS::PrefetchMixer(pool, 44100, true, 1024);
		mixer->setReady(true);
		Audio::Mixer &m = *mixer;
		Audio::SoundHandle h;
		m.playStream(Audio::Mixer::kSpeechSoundType, &h, new FakeStream(3000, false, &deleted));
		byte buf[1024 * 4];
		for (int i = 0; i < 10 && m.isSoundHandleActive(h); i++) {
			pool.prefetchAll();
			mixer->mixCallback(buf, sizeof(buf));
		}
		TS_ASSERT(!m.isSoundHandleActive(h));
		TS_ASSERT_EQUALS(deleted, 0);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kOrphan), 1);
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 1);
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		delete mixer;
	}

	void test_the_speech_hook_runs_once_a_line_and_the_guard_can_refuse() {
		int deleted = 0, calls = 0;
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer *mixer = new DOS::PrefetchMixer(pool, 44100, true, 1024);
		mixer->setReady(true);
		mixer->setSpeechHook(countCall, &calls);
		mixer->setWrapGuard(refuse);
		Audio::Mixer &m = *mixer;
		Audio::SoundHandle h;
		m.playStream(Audio::Mixer::kSpeechSoundType, &h, new FakeStream(3000, false, &deleted));
		TS_ASSERT_EQUALS(calls, 1);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kLive), 0);
		TS_ASSERT_EQUALS(pool.declined(), 1u);	// the guard refused: counted, not logged here
		delete mixer;
		TS_ASSERT_EQUALS(deleted, 1);
	}

	void test_the_marker_goes_in_front_of_speech_only() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		byte buf[64 * 4];
		{
			DOS::PrefetchMixer mixer(pool, 44100, true, 1024);
			mixer.setReady(true);
			mixer.setMarkMillis(10);
			Audio::Mixer &m = mixer;
			Audio::SoundHandle h;
			m.playStream(Audio::Mixer::kSpeechSoundType, &h, new FakeStream(3000, false, &deleted));
			mixer.mixCallback(buf, sizeof(buf));
			TS_ASSERT(ABS(((int16 *)buf)[0]) > 10000);
		}
		pool.reap();
		{
			DOS::PrefetchMixer mixer(pool, 44100, true, 1024);
			mixer.setReady(true);
			mixer.setMarkMillis(10);
			Audio::Mixer &m = mixer;
			Audio::SoundHandle h;
			m.playStream(Audio::Mixer::kMusicSoundType, &h, new FakeStream(3000, false, &deleted));
			mixer.mixCallback(buf, sizeof(buf));
			TS_ASSERT_EQUALS(((int16 *)buf)[0], 0);
		}
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 2);
	}

	void test_the_latency_comes_from_the_provider() {
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer mixer(pool, 44100, true, 1024);
		const Audio::Mixer &m = mixer;
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 0u);
		uint32 ms = 412;
		mixer.setLatencyProvider(fixedLatency, &ms);
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 412u);
	}
};
