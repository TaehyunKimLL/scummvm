#include <cxxtest/TestSuite.h>

#include "audio/decoders/vorbis.h"

class VorbisRefillTestSuite : public CxxTest::TestSuite {
public:
	void test_the_refill_quantum_defaults_to_4096() {
		// 4096 is what VorbisStream always used; only a build that defines
		// VORBIS_REFILL_SAMPLES (the DOS build does, and checks it in
		// dos-mixer.cpp) makes it smaller, so no other platform changes.
		TS_ASSERT_EQUALS((int)Audio::kVorbisRefillSamples, 4096);
	}
};
