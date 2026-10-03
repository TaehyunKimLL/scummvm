#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/sound-ini.h"

// SOUND ADLIB|MT32|GM edits the [scummvm] section of SCUMMVM.INI.
class DosSoundIniTestSuite : public CxxTest::TestSuite {
public:
	void test_parse_names() {
		DOS::SoundChoice c = DOS::kSoundAdlib;
		TS_ASSERT(DOS::parseSoundChoice("MT32", c));
		TS_ASSERT_EQUALS(c, DOS::kSoundMt32);
		TS_ASSERT(DOS::parseSoundChoice("gm", c));
		TS_ASSERT_EQUALS(c, DOS::kSoundGm);
		TS_ASSERT(DOS::parseSoundChoice("AdLib", c));
		TS_ASSERT_EQUALS(c, DOS::kSoundAdlib);
		TS_ASSERT(!DOS::parseSoundChoice("opl", c));
		TS_ASSERT(!DOS::parseSoundChoice("", c));
		TS_ASSERT(!DOS::parseSoundChoice(nullptr, c));
	}

	void test_replaces_the_keys_in_place() {
		const char *in = "[scummvm]\r\nmusic_driver=adlib\r\n\r\n[kq1]\r\ngameid=kq1\r\n";
		const char *want = "[scummvm]\r\nmusic_driver=mpu401\r\nnative_mt32=true\r\nenable_gs=false\r\n\r\n[kq1]\r\ngameid=kq1\r\n";
		TS_ASSERT_EQUALS(DOS::applySoundChoice(in, DOS::kSoundMt32), want);
	}

	void test_adds_missing_keys_after_the_last_setting_not_after_trailing_comments() {
		const char *in = "[scummvm]\n# dos_vsync=off\nrender_target=auto\n\n# Korean text\n[kq1ko]\npath=GAMES\n";
		const char *want = "[scummvm]\n# dos_vsync=off\nrender_target=auto\nmusic_driver=adlib\nnative_mt32=false\nenable_gs=false\n\n# Korean text\n[kq1ko]\npath=GAMES\n";
		TS_ASSERT_EQUALS(DOS::applySoundChoice(in, DOS::kSoundAdlib), want);
	}

	void test_commented_keys_are_not_edited() {
		const char *in = "[scummvm]\nmusic_driver=adlib\n# music_driver=mpu401\n# native_mt32=true\n";
		Common::String out = DOS::applySoundChoice(in, DOS::kSoundGm);
		TS_ASSERT(out.contains("# music_driver=mpu401\n"));
		TS_ASSERT(out.contains("# native_mt32=true\n"));
		TS_ASSERT(out.contains("music_driver=mpu401\n"));
		TS_ASSERT(out.contains("enable_gs=true\n"));
	}

	void test_other_sections_are_untouched() {
		const char *in = "[kq1]\nmusic_driver=mt32\n\n[scummvm]\nx=1\n";
		Common::String out = DOS::applySoundChoice(in, DOS::kSoundAdlib);
		TS_ASSERT(out.contains("[kq1]\nmusic_driver=mt32\n"));
		TS_ASSERT(out.contains("[scummvm]\nx=1\nmusic_driver=adlib\nnative_mt32=false\nenable_gs=false\n"));
	}

	void test_creates_the_section_in_an_empty_or_sectionless_file() {
		TS_ASSERT_EQUALS(DOS::applySoundChoice("", DOS::kSoundGm),
			"[scummvm]\nmusic_driver=mpu401\nnative_mt32=false\nenable_gs=true\n");
		TS_ASSERT_EQUALS(DOS::applySoundChoice("[kq1]\ngameid=kq1\n", DOS::kSoundAdlib),
			"[scummvm]\nmusic_driver=adlib\nnative_mt32=false\nenable_gs=false\n\n[kq1]\ngameid=kq1\n");
	}

	void test_spaces_around_equals_and_a_missing_final_newline() {
		const char *in = "[scummvm]\nmusic_driver = adlib\nenable_gs = true";
		TS_ASSERT_EQUALS(DOS::applySoundChoice(in, DOS::kSoundMt32),
			"[scummvm]\nmusic_driver=mpu401\nenable_gs=false\nnative_mt32=true\n");
	}

	void test_applying_twice_is_stable() {
		const char *in = "[scummvm]\nmusic_driver=adlib\n\n[a]\nb=c\n";
		Common::String once = DOS::applySoundChoice(in, DOS::kSoundMt32);
		TS_ASSERT_EQUALS(DOS::applySoundChoice(once, DOS::kSoundMt32), once);
	}
};
