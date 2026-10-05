#include <cxxtest/TestSuite.h>
#include "backends/mixer/dos/dos-audio-config.h"

class DosAudioConfigTestSuite : public CxxTest::TestSuite {
public:
	void test_default_and_junk() {
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames(""), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("4k"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("-2048"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("1234567"), 4096);
	}
	void test_powers_of_two_in_range() {
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("512"), 512);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("2048"), 2048);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("8192"), 8192);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("0512"), 512);
	}
	void test_rounded_down_and_out_of_range() {
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("3000"), 2048);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("8191"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("256"), 4096);
		TS_ASSERT_EQUALS(DOS::audioDeviceFrames("16384"), 4096);
	}
};
