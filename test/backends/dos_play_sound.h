#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/launcher/play-sound.h"

// SOUND ADLIB|MT32|GM edits the [scummvm] section of SCUMMVM.INI.
class PlaySoundTestSuite : public CxxTest::TestSuite {
public:
	void test_parse_names() {
		Play::SoundChoice c = Play::kSoundAdlib;
		TS_ASSERT(Play::parseSoundChoice("MT32", c));
		TS_ASSERT_EQUALS(c, Play::kSoundMt32);
		TS_ASSERT(Play::parseSoundChoice("gm", c));
		TS_ASSERT_EQUALS(c, Play::kSoundGm);
		TS_ASSERT(Play::parseSoundChoice("AdLib", c));
		TS_ASSERT_EQUALS(c, Play::kSoundAdlib);
		TS_ASSERT(!Play::parseSoundChoice("opl", c));
		TS_ASSERT(!Play::parseSoundChoice("", c));
		TS_ASSERT(!Play::parseSoundChoice(nullptr, c));
	}

	void test_replaces_the_keys_in_place() {
		const char *in = "[scummvm]\r\nmusic_driver=adlib\r\n\r\n[kq1]\r\ngameid=kq1\r\n";
		const char *want = "[scummvm]\r\nmusic_driver=mpu401\r\nnative_mt32=true\r\nenable_gs=false\r\n\r\n[kq1]\r\ngameid=kq1\r\n";
		TS_ASSERT_EQUALS(Play::applySoundChoice(in, Play::kSoundMt32), want);
	}

	void test_adds_missing_keys_after_the_last_setting_not_after_trailing_comments() {
		const char *in = "[scummvm]\n# dos_vsync=off\nrender_target=auto\n\n# Korean text\n[kq1ko]\npath=GAMES\n";
		const char *want = "[scummvm]\n# dos_vsync=off\nrender_target=auto\nmusic_driver=adlib\nnative_mt32=false\nenable_gs=false\n\n# Korean text\n[kq1ko]\npath=GAMES\n";
		TS_ASSERT_EQUALS(Play::applySoundChoice(in, Play::kSoundAdlib), want);
	}

	void test_commented_keys_are_not_edited() {
		const char *in = "[scummvm]\nmusic_driver=adlib\n# music_driver=mpu401\n# native_mt32=true\n";
		std::string out = Play::applySoundChoice(in, Play::kSoundGm);
		TS_ASSERT((out.find("# music_driver=mpu401\n") != std::string::npos));
		TS_ASSERT((out.find("# native_mt32=true\n") != std::string::npos));
		TS_ASSERT((out.find("music_driver=mpu401\n") != std::string::npos));
		TS_ASSERT((out.find("enable_gs=true\n") != std::string::npos));
	}

	void test_other_sections_are_untouched() {
		const char *in = "[kq1]\nmusic_driver=mt32\n\n[scummvm]\nx=1\n";
		std::string out = Play::applySoundChoice(in, Play::kSoundAdlib);
		TS_ASSERT((out.find("[kq1]\nmusic_driver=mt32\n") != std::string::npos));
		TS_ASSERT((out.find("[scummvm]\nx=1\nmusic_driver=adlib\nnative_mt32=false\nenable_gs=false\n") != std::string::npos));
	}

	void test_creates_the_section_in_an_empty_or_sectionless_file() {
		TS_ASSERT_EQUALS(Play::applySoundChoice("", Play::kSoundGm),
			"[scummvm]\nmusic_driver=mpu401\nnative_mt32=false\nenable_gs=true\n");
		TS_ASSERT_EQUALS(Play::applySoundChoice("[kq1]\ngameid=kq1\n", Play::kSoundAdlib),
			"[scummvm]\nmusic_driver=adlib\nnative_mt32=false\nenable_gs=false\n\n[kq1]\ngameid=kq1\n");
	}

	void test_spaces_around_equals_and_a_missing_final_newline() {
		const char *in = "[scummvm]\nmusic_driver = adlib\nenable_gs = true";
		TS_ASSERT_EQUALS(Play::applySoundChoice(in, Play::kSoundMt32),
			"[scummvm]\nmusic_driver=mpu401\nenable_gs=false\nnative_mt32=true\n");
	}

	void test_applying_twice_is_stable() {
		const char *in = "[scummvm]\nmusic_driver=adlib\n\n[a]\nb=c\n";
		std::string once = Play::applySoundChoice(in, Play::kSoundMt32);
		TS_ASSERT_EQUALS(Play::applySoundChoice(once, Play::kSoundMt32), once);
	}

	void test_a_utf8_bom_before_the_header_is_kept_and_skipped() {
		const std::string out = Play::applySoundChoice("\xEF\xBB\xBF[scummvm]\nmusic_driver=mpu401\n", Play::kSoundAdlib);
		TS_ASSERT_EQUALS(out.find("\xEF\xBB\xBF[scummvm]\n"), 0u);
		TS_ASSERT((out.find("music_driver=adlib") != std::string::npos));
		TS_ASSERT(!(out.find("mpu401") != std::string::npos));
		TS_ASSERT_EQUALS(out.find("[scummvm]"), out.rfind("[scummvm]"));
	}
};
