#include <cxxtest/TestSuite.h>
#include "audio/mixer_intern.h"
#include "../system/null_osystem.h"

class MixerLatencyTestSuite : public CxxTest::TestSuite {
public:
	void setUp() override {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();	// MixerImpl's mutex needs g_system
#endif
	}
	void tearDown() override {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_a_mixer_knows_no_latency_by_default() {
		Audio::MixerImpl mixer(44100, true, 1024);
		const Audio::Mixer &m = mixer;
		TS_ASSERT_EQUALS(m.getOutputLatencyMillis(), 0u);
	}
};
