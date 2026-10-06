#include <cxxtest/TestSuite.h>

#include "backends/mixer/dos/prefetch.h"
#include "../system/null_osystem.h"

namespace {

bool interruptsOff() {
	return false;
}
bool interruptsOn() {
	return true;
}

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
	void test_wrap_starts_empty_and_only_reap_deletes() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(100000, false, &deleted);
		Audio::AudioStream *p = pool.wrap(f);
		TS_ASSERT_DIFFERS(p, (Audio::AudioStream *)f);
		TS_ASSERT_EQUALS(f->pos(), 0);
		TS_ASSERT(!p->endOfData());
		TS_ASSERT(!p->endOfStream());
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kLive), 1);
		delete p;
		TS_ASSERT_EQUALS(deleted, 0);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kOrphan), 1);
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 1);
		TS_ASSERT_EQUALS(pool.countSlots(DOS::PrefetchPool::kFree), DOS::PrefetchPool::kSlots);
	}

	void test_prefetch_caps_each_call_without_extra_misses() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(100000, false, &deleted);
		Audio::AudioStream *p = pool.wrap(f);
		int16 buf[512];
		for (int i = 0; i < 48; i++) {
			const int before = f->pos();
			pool.prefetchAll();
			TS_ASSERT(f->pos() - before <= DOS::PrefetchPool::kBurstSamples);
			TS_ASSERT_EQUALS(p->readBuffer(buf, 512), 512);
			for (int j = 0; j < 512; j++)
				TS_ASSERT_EQUALS(buf[j], (int16)((i * 512 + j) & 0x7fff));
		}
		TS_ASSERT_EQUALS(f->pos(), 48 * 512 + DOS::PrefetchPool::kRingSamples - 512);
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		delete p;
		pool.reap();
	}

	void test_two_cold_streams_share_one_burst_at_full_rate() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *speech = new FakeStream(100000, false, &deleted, 48000);
		FakeStream *music = new FakeStream(100000, true, &deleted, 44100);
		Audio::AudioStream *voice = pool.wrap(speech);
		Audio::AudioStream *track = pool.wrap(music);
		// Conservatively consume 279 speech samples every piece (48 kHz
		// resampling of 256 frames at 44.1 kHz) and 512 stereo music samples.
		int16 voiceBuf[280], musicBuf[512];
		int voiceRead = 0, musicRead = 0;
		for (int piece = 0; piece < 128; piece++) {
			const int before = speech->pos() + music->pos();
			pool.prefetchAll();
			const int added = speech->pos() + music->pos() - before;
			TS_ASSERT(added <= DOS::PrefetchPool::kBurstSamples);
			TS_ASSERT(added > 0);
			const int voiceN = 279;
			TS_ASSERT_EQUALS(voice->readBuffer(voiceBuf, voiceN), voiceN);
			TS_ASSERT_EQUALS(track->readBuffer(musicBuf, 512), 512);
			for (int i = 0; i < voiceN; i++)
				TS_ASSERT_EQUALS(voiceBuf[i], (int16)((voiceRead + i) & 0x7fff));
			for (int i = 0; i < 512; i++)
				TS_ASSERT_EQUALS(musicBuf[i], (int16)((musicRead + i) & 0x7fff));
			voiceRead += voiceN;
			musicRead += 512;
		}
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		TS_ASSERT(speech->pos() > voiceRead);
		TS_ASSERT(music->pos() > musicRead);
		delete voice;
		delete track;
		pool.reap();
		TS_ASSERT_EQUALS(deleted, 2);
	}

	// A mix piece of 256 output frames at @p mixRate takes
	// ceil(256 * srcRate / mixRate) frames of every stream. Runs @p pieces
	// pieces of prefetchAll() then one read per stream, from empty rings,
	// and returns the misses taken after the first piece.
	struct Model {
		int rate;
		bool stereo;
		int total;
	};
	static uint32 runPieces(const Model *streams, int count, int mixRate, int pieces, int *maxAdded = nullptr) {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *fake[DOS::PrefetchPool::kSlots];
		Audio::AudioStream *proxy[DOS::PrefetchPool::kSlots];
		int consumed[DOS::PrefetchPool::kSlots];
		for (int i = 0; i < count; i++) {
			fake[i] = new FakeStream(streams[i].total, streams[i].stereo, &deleted, streams[i].rate);
			proxy[i] = pool.wrap(fake[i]);
			consumed[i] = 0;
		}
		uint32 missesAfterFirst = 0;
		int16 buf[4096];
		if (maxAdded)
			*maxAdded = 0;
		for (int piece = 0; piece < pieces; piece++) {
			int before = 0;
			for (int i = 0; i < count; i++)
				before += fake[i]->pos();
			pool.prefetchAll();
			int after = 0;
			for (int i = 0; i < count; i++)
				after += fake[i]->pos();
			if (maxAdded && piece > 0 && after - before > *maxAdded)
				*maxAdded = after - before;
			for (int i = 0; i < count; i++) {
				const int ch = streams[i].stereo ? 2 : 1;
				const int frames = (256 * streams[i].rate + mixRate - 1) / mixRate;
				TS_ASSERT_EQUALS(proxy[i]->readBuffer(buf, frames * ch), frames * ch);
				for (int j = 0; j < frames * ch; j++)
					if (buf[j] != (int16)((consumed[i] + j) & 0x7fff)) {
						TS_FAIL("out of order");
						break;
					}
				consumed[i] += frames * ch;
			}
			if (piece == 0)
				missesAfterFirst = pool.misses();
		}
		const uint32 later = pool.misses() - missesAfterFirst;
		for (int i = 0; i < count; i++)
			delete proxy[i];
		pool.reap();
		TS_ASSERT_EQUALS(deleted, count);
		return later;
	}

	// Runs @p pieces pieces of prefetchAll() then reads, from empty rings, on
	// @p count in-phase streams of @p channels channels. Piece p takes
	// @p perPiece[p % period] samples from every stream, in reads of @p chunk
	// samples (one read of the whole amount when @p chunk is 0). Returns in
	// @p added the samples all the parents were asked for in each piece's
	// prefetchAll(). The decision under test is how much a ring is topped up
	// by: the larger of the last two pieces' consumption.
	static void runPattern(int count, int channels, const int *perPiece, int period, int chunk, int pieces, int *added) {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *fake[DOS::PrefetchPool::kSlots];
		Audio::AudioStream *proxy[DOS::PrefetchPool::kSlots];
		for (int i = 0; i < count; i++) {
			fake[i] = new FakeStream(1000000, channels == 2, &deleted);
			proxy[i] = pool.wrap(fake[i]);
		}
		int16 buf[4096];
		for (int piece = 0; piece < pieces; piece++) {
			int before = 0, after = 0;
			for (int i = 0; i < count; i++)
				before += fake[i]->pos();
			pool.prefetchAll();
			for (int i = 0; i < count; i++)
				after += fake[i]->pos();
			added[piece] = after - before;
			const int n = perPiece[piece % period];
			for (int i = 0; i < count; i++)
				for (int done = 0; done < n;) {
					const int k = chunk ? MIN(chunk, n - done) : n;
					TS_ASSERT_EQUALS(proxy[i]->readBuffer(buf, k), k);
					done += k;
				}
		}
		for (int i = 0; i < count; i++)
			delete proxy[i];
		pool.reap();
		TS_ASSERT_EQUALS(deleted, count);
	}

	void test_demand_is_the_larger_of_the_last_two_pieces_when_it_alternates() {
		// Four mono streams in phase take 256 then 768 samples a piece: a
		// piece's demand is 4 * 768 = 3072 > the 1024 burst, in every piece
		// once two have run. From the last piece alone it would swing between
		// 1024 and 3072, from the one before it alone it would lag a piece.
		const int perPiece[] = {256, 768};
		int added[12];
		runPattern(4, 1, perPiece, 2, 0, 12, added);
		TS_ASSERT_EQUALS(added[0], 1024);
		TS_ASSERT_EQUALS(added[1], 1024);
		for (int piece = 2; piece < 12; piece++)
			TS_ASSERT_EQUALS(added[piece], 3072);
	}

	void test_demand_of_a_stereo_stream_that_alternates_one_and_three_chunks() {
		// One stereo stream; the rate converter reads 512 samples at a time:
		// a piece takes 512 (one chunk) or 1536 (three), alternating. The
		// ring is topped up by 1536 in every piece once two have run, not
		// by 1536 and 1024 in turns (last piece alone) or a piece late
		// (the one before it alone).
		const int perPiece[] = {512, 1536};
		int added[12];
		runPattern(1, 2, perPiece, 2, 512, 12, added);
		TS_ASSERT_EQUALS(added[0], 1024);
		TS_ASSERT_EQUALS(added[1], 1024);
		for (int piece = 2; piece < 12; piece++)
			TS_ASSERT_EQUALS(added[piece], 1536);
	}

	void test_demand_of_a_stereo_stream_that_alternates_two_and_four_chunks() {
		const int perPiece[] = {1024, 2048};
		int added[12];
		runPattern(1, 2, perPiece, 2, 512, 12, added);
		for (int piece = 2; piece < 12; piece++)
			TS_ASSERT_EQUALS(added[piece], 2048);
	}

	void test_music_and_speech_at_a_22050_output_are_served_ahead() {
		// MI1 on a Sound Blaster Pro: 44.1 kHz stereo FLAC music needs 1024
		// samples a piece, 44.1 kHz mono speech 512: 1536 > the 1024 burst.
		const Model streams[] = {{44100, true, 400000}, {44100, false, 400000}};
		TS_ASSERT_EQUALS(runPieces(streams, 2, 22050, 200), 0u);
	}

	void test_four_stereo_streams_at_a_44100_output_are_served_ahead() {
		const Model streams[] = {{44100, true, 400000}, {44100, true, 400000}, {44100, true, 400000}, {44100, true, 400000}};
		int maxAdded = 0;
		TS_ASSERT_EQUALS(runPieces(streams, 4, 44100, 200, &maxAdded), 0u);
		// total per piece is max(1024, demand); demand is 4 * 512 here
		TS_ASSERT(maxAdded <= 2048);
	}

	void test_one_48k_stereo_stream_at_a_22050_output_is_served_ahead() {
		// 256 * 48000 / 22050 = 557 frames: 1114 samples a piece
		const Model streams[] = {{48000, true, 400000}};
		TS_ASSERT_EQUALS(runPieces(streams, 1, 22050, 200), 0u);
	}

	void test_one_48k_stereo_stream_at_a_44100_output_is_served_ahead() {
		const Model streams[] = {{48000, true, 400000}};
		TS_ASSERT_EQUALS(runPieces(streams, 1, 44100, 200), 0u);
	}

	void test_low_demand_still_gets_the_whole_burst() {
		// One 48 kHz mono stream at a 48 kHz output takes 256 samples a
		// piece; the burst stays 1024 (the ring runs ahead of it).
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(100000, false, &deleted, 48000);
		Audio::AudioStream *p = pool.wrap(f);
		int16 buf[256];
		pool.prefetchAll();
		TS_ASSERT_EQUALS(f->pos(), DOS::PrefetchPool::kBurstSamples);
		for (int i = 0; i < 10; i++) {
			TS_ASSERT_EQUALS(p->readBuffer(buf, 256), 256);
			const int before = f->pos();
			pool.prefetchAll();
			TS_ASSERT_EQUALS(f->pos() - before, DOS::PrefetchPool::kBurstSamples);
		}
		delete p;
		pool.reap();
	}

	void test_prefetch_caps_stereo_bursts_at_the_ring_edge() {
		int deleted = 0;
		DOS::PrefetchPool pool;
		FakeStream *f = new FakeStream(100000, true, &deleted);
		Audio::AudioStream *p = pool.wrap(f);
		int16 buf[564];
		for (int i = 0; i < 48; i++) {
			const int before = f->pos();
			pool.prefetchAll();
			TS_ASSERT(f->pos() - before <= DOS::PrefetchPool::kBurstSamples);
			TS_ASSERT_EQUALS(f->pos() % 2, 0);
			TS_ASSERT_EQUALS(p->readBuffer(buf, 564), 564);
			for (int j = 0; j < 564; j++)
				TS_ASSERT_EQUALS(buf[j], (int16)((i * 564 + j) & 0x7fff));
		}
		TS_ASSERT_EQUALS(pool.misses(), 0u);
		delete p;
		pool.reap();
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
		int16 buf[100];
		TS_ASSERT_EQUALS(p->readBuffer(buf, 100), 100);
		TS_ASSERT_EQUALS(buf[0], 0);
		TS_ASSERT_EQUALS(buf[99], 99);
		TS_ASSERT_EQUALS(pool.misses(), 1u);
		pool.prefetchAll();
		TS_ASSERT_EQUALS(p->readBuffer(buf, 100), 100);
		TS_ASSERT_EQUALS(buf[0], 100);
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

	void test_no_latency_is_asked_for_with_interrupts_off() {
		DOS::PrefetchPool pool;
		DOS::PrefetchMixer mixer(pool, 44100, true, 1024);
		const Audio::Mixer &m = mixer;
		uint32 ms = 412;
		mixer.setLatencyProvider(fixedLatency, &ms);
		mixer.setWrapGuard(interruptsOff);
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 0u);
		mixer.setWrapGuard(interruptsOn);
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 412u);
	}
};
