#include <cxxtest/TestSuite.h>

#include "common/hangul.h"

// The particle picker moved from Sci::ScriptStrings to common/ (AGI's
// stringPrintf uses it after %0 %w %s %m %g). Same cases as
// test/engines/sci/translation.h, which keeps testing SCI's forwarder.
class KoreanParticlesTestSuite : public CxxTest::TestSuite {
	static Common::String p(const char *in) {
		Common::String s(in);
		Common::resolveKoreanParticles(s);
		return s;
	}

public:
	void test_particles_follow_the_last_syllable() {
		TS_ASSERT_EQUALS(p("\xEB\xAC\xB8\xEC\x9D\x84(\xEB\xA5\xBC)"), "\xEB\xAC\xB8\xEC\x9D\x84");
		TS_ASSERT_EQUALS(p("\xEC\x83\x81\xEC\x9E\x90\xEC\x9D\x84(\xEB\xA5\xBC)"), "\xEC\x83\x81\xEC\x9E\x90\xEB\xA5\xBC");
		TS_ASSERT_EQUALS(p("\xEA\xB8\xB8(\xEC\x9C\xBC)\xEB\xA1\x9C"), "\xEA\xB8\xB8\xEB\xA1\x9C");
		TS_ASSERT_EQUALS(p("\xEB\xAC\xB8(\xEC\x9C\xBC)\xEB\xA1\x9C"), "\xEB\xAC\xB8\xEC\x9C\xBC\xEB\xA1\x9C");
	}

	void test_digits_quotes_and_nothing_to_read() {
		TS_ASSERT_EQUALS(p("\"WIT\"\xEC\x9D\x80(\xEB\x8A\x94)"), "\"WIT\"\xEB\x8A\x94");
		TS_ASSERT_EQUALS(p("3\xEC\x9D\x84(\xEB\xA5\xBC)"), "3\xEC\x9D\x84");
		TS_ASSERT_EQUALS(p("(\xEC\x9D\xB4)"), "(\xEC\x9D\xB4)");
		TS_ASSERT_EQUALS(p("Score (100)"), "Score (100)");
	}
};
