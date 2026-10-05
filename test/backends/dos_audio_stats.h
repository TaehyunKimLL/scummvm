#include <cxxtest/TestSuite.h>
#include "backends/mixer/dos/dos-audio-stats.h"

class DosAudioStatsTestSuite : public CxxTest::TestSuite {
public:
	void test_line() {
		DOS::AudioStats s;
		s.millis = 1000;
		s.tsc = 40000000ULL;
		s.pieces = 7;
		s.pieceMaxTsc = 12000;
		s.prefetchTsc = 300;
		s.mixTsc = 400;
		s.misses = 0;
		s.speech = 3;
		s.music = 1;
		s.sbIrqs = 50;
		s.sbUnderruns = 2;
		s.sbQueued = 49152;
		s.sbMinAvail = 16384;
		s.sbChunk = 16384;
		s.rate = 44100;
		s.frames = 4096;
		TS_ASSERT_EQUALS(DOS::formatAudioStats(s),
			"ms=1000 tsc=40000000 pieces=7 piecemax=12000 prefetch=300 mix=400 misses=0 speech=3 music=1 "
			"irqs=50 under=2 queued=49152 minavail=16384 chunk=16384 rate=44100 frames=4096");
	}
	void test_no_sound_blaster() {
		DOS::AudioStats s;
		TS_ASSERT_EQUALS(DOS::formatAudioStats(s),
			"ms=0 tsc=0 pieces=0 piecemax=0 prefetch=0 mix=0 misses=0 speech=0 music=0 "
			"irqs=-1 under=-1 queued=-1 minavail=-1 chunk=-1 rate=0 frames=0");
	}
};
