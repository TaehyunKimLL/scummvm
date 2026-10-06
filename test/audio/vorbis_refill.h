#include <cxxtest/TestSuite.h>

#include "audio/decoders/vorbis.h"

class VorbisRefillTestSuite : public CxxTest::TestSuite {
public:
	void test_the_refill_quantum_is_4096_except_on_dos() {
		// 4096 is what VorbisStream always used; only the DOS build
		// (build-dos.sh defines VORBIS_REFILL_SAMPLES=1024 for every object)
		// makes it smaller, so no other platform changes.
#ifdef __DJGPP__
		TS_ASSERT_EQUALS((int)Audio::kVorbisRefillSamples, 1024);
#else
		TS_ASSERT_EQUALS((int)Audio::kVorbisRefillSamples, 4096);
#endif
	}
};
