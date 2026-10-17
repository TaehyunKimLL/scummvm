#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/launcher/play-merge.h"

#include <string>
#include <vector>

// PLAY makes the run's INI from the pack's INI, the home's common [scummvm]
// and the player's USER.INI, and keeps what the engine changed in USER.INI.
class PlayMergeTestSuite : public CxxTest::TestSuite {
	static std::string g(const Play::Ini &i, const char *s, const char *k) {
		const std::string *v = i.get(s, k);
		return v ? *v : "<none>";
	}

	static Play::Ini pack() {
		return Play::parseIni("[scummvm]\nmusic_driver=adlib\nrender_target=auto\nlastselectedgame=kq1\n"
							  "[kq1]\ngameid=kq1sci\nengineid=sci\ndescription=King's Quest I\nmusic_driver=mt32x\ntalkspeed=60\n"
							  "[other]\ngameid=x\n");
	}
	static Play::Ini home() {
		return Play::parseIni("[scummvm]\nmusic_driver=mpu401\nnative_mt32=true\nenable_gs=false\nlastselectedgame=foo\nversioninfo=2.9\n"
							  "gameid=nope\n");
	}
	static Play::Ini user() {
		return Play::parseIni("[scummvm]\nmusic_driver=adlib\nnative_mt32=false\n[kq1]\nmusic_volume=100\n");
	}
	static std::vector<std::string> words(const char *a, const char *b = nullptr, const char *c = nullptr, const char *d = nullptr) {
		std::vector<std::string> v;
		const char *const all[] = { a, b, c, d };
		for (int i = 0; i < 4; ++i)
			if (all[i])
				v.push_back(all[i]);
		return v;
	}

public:
	void test_parse_lower_cases_trims_and_keeps_empty_sections() {
		Play::Ini i = Play::parseIni("\xEF\xBB\xBF# c\r\n[ KQ1 ]\r\n GameID = kq1sci \r\n; x=1\r\n[empty]\r\n");
		TS_ASSERT_EQUALS(g(i, "kq1", "gameid"), "kq1sci");
		TS_ASSERT_EQUALS(g(i, "kq1", "x"), "<none>");
		TS_ASSERT(i.find("empty") != nullptr);
		TS_ASSERT_EQUALS(i.targets().size(), 2u);
	}

	void test_parse_drops_keys_before_any_section() {
		Play::Ini i = Play::parseIni("stray=1\n[kq1]\ngameid=kq1sci\n");
		TS_ASSERT(i.find("") == nullptr);
		TS_ASSERT_EQUALS(i.sections.size(), 1u);
	}

	void test_drop_empty_sections() {
		Play::Ini i = Play::parseIni("[scummvm]\n[kq1]\ntalkspeed=60\n[other]\n");
		i.erase("kq1", "talkspeed");
		i.set("other", "a", "1");
		i.dropEmptySections();
		TS_ASSERT_EQUALS(i.sections.size(), 1u);
		TS_ASSERT_EQUALS(i.sections[0].name, "other");
	}

	void test_write_is_crlf_after_the_header() {
		Play::Ini i;
		i.set("scummvm", "a", "1");
		i.set("kq1", "gameid", "kq1sci");
		TS_ASSERT_EQUALS(Play::writeIni(i, "# h\r\n"), "# h\r\n[scummvm]\r\na=1\r\n\r\n[kq1]\r\ngameid=kq1sci\r\n\r\n");
		TS_ASSERT_EQUALS(g(Play::parseIni(Play::writeIni(i)), "kq1", "gameid"), "kq1sci");
	}

	void test_home_common_wins_over_the_pack() {
		Play::Ini run = Play::mergeRun(pack(), home(), Play::Ini(), "kq1");
		TS_ASSERT_EQUALS(g(run, "scummvm", "music_driver"), "mpu401");
		TS_ASSERT_EQUALS(g(run, "scummvm", "render_target"), "auto");
		// The pack's target key the home sets in common is left out, so the home's applies.
		TS_ASSERT_EQUALS(g(run, "kq1", "music_driver"), "<none>");
		TS_ASSERT_EQUALS(g(run, "kq1", "talkspeed"), "60");
	}

	void test_user_wins_over_home_and_pack() {
		Play::Ini run = Play::mergeRun(pack(), home(), user(), "kq1");
		TS_ASSERT_EQUALS(g(run, "kq1", "music_driver"), "adlib");
		TS_ASSERT_EQUALS(g(run, "kq1", "native_mt32"), "false");
		TS_ASSERT_EQUALS(g(run, "kq1", "music_volume"), "100");
		Play::Ini u = user();
		u.set("kq1", "music_driver", "mpu401"); // the target's own over USER.INI's [scummvm]
		TS_ASSERT_EQUALS(g(Play::mergeRun(pack(), home(), u, "kq1"), "kq1", "music_driver"), "mpu401");
	}

	void test_the_pack_says_what_the_game_is() {
		Play::Ini run = Play::mergeRun(pack(), home(), user(), "kq1");
		TS_ASSERT_EQUALS(g(run, "kq1", "gameid"), "kq1sci");
		TS_ASSERT_EQUALS(g(run, "kq1", "engineid"), "sci");
		TS_ASSERT_EQUALS(g(run, "kq1", "description"), "King's Quest I");
		Play::Ini u;
		u.set("kq1", "gameid", "hacked");
		u.set("scummvm", "path", "X:/");
		Play::Ini run2 = Play::mergeRun(pack(), home(), u, "kq1");
		TS_ASSERT_EQUALS(g(run2, "kq1", "gameid"), "kq1sci");
		TS_ASSERT_EQUALS(g(run2, "kq1", "path"), "<none>");
	}

	void test_run_has_only_the_target_and_no_bookkeeping() {
		Play::Ini run = Play::mergeRun(pack(), home(), user(), "kq1");
		TS_ASSERT(run.find("other") == nullptr);
		TS_ASSERT_EQUALS(run.sections.size(), 2u);
		TS_ASSERT_EQUALS(g(run, "scummvm", "lastselectedgame"), "<none>");
		TS_ASSERT_EQUALS(g(run, "scummvm", "versioninfo"), "<none>");
	}

	void test_an_old_pack_ini_with_many_keys_goes_into_the_run() {
		// An older pack's INI (MI1KO): [scummvm] keys and three targets.
		Play::Ini old = Play::parseIni("[scummvm]\nlastselectedgame=mi1kol\nrender_target=auto\nmusic_driver=adlib\n"
									   "[mi1ko]\nengineid=scumm\ngameid=monkey\nlanguage=ko\nextra=VGA\nextrapath=DATA\nhires_text_map=data:M1KO.MAP\n"
									   "[mi1kol]\nengineid=scumm\ngameid=monkey\nlanguage=ko\nextrapath=DATA\nrender_target=clut8\n");
		Play::Ini run = Play::mergeRun(old, Play::Ini(), Play::Ini(), "mi1kol");
		TS_ASSERT_EQUALS(g(run, "scummvm", "music_driver"), "adlib");
		TS_ASSERT_EQUALS(g(run, "mi1kol", "render_target"), "clut8");
		TS_ASSERT_EQUALS(g(run, "mi1kol", "extrapath"), "DATA");
		TS_ASSERT_EQUALS(g(run, "mi1kol", "language"), "ko");
		TS_ASSERT(run.find("mi1ko") == nullptr);
		TS_ASSERT_EQUALS(Play::engineOf(Play::writeIni(run), "mi1kol"), "scumm");
	}

	void test_capture_keeps_what_the_engine_changed() {
		Play::Ini u = user();
		Play::Ini run = Play::mergeRun(pack(), home(), u, "kq1");
		Play::Ini after = run;
		after.set("kq1", "talkspeed", "120");
		after.set("kq1", "sfx_volume", "200");
		after.set("kq1", "guioptions", "sndNoSpeech");
		after.set("scummvm", "lastselectedgame", "kq1");
		after.set("scummvm", "gfx_mode", "x");
		TS_ASSERT(Play::captureChanges(run, after, pack(), home(), "kq1", u));
		TS_ASSERT_EQUALS(g(u, "kq1", "talkspeed"), "120");
		TS_ASSERT_EQUALS(g(u, "kq1", "sfx_volume"), "200");
		TS_ASSERT_EQUALS(g(u, "kq1", "music_volume"), "100");
		TS_ASSERT_EQUALS(g(u, "kq1", "guioptions"), "<none>");
		TS_ASSERT_EQUALS(g(u, "scummvm", "lastselectedgame"), "<none>");
		TS_ASSERT_EQUALS(g(u, "scummvm", "gfx_mode"), "<none>");
		TS_ASSERT_EQUALS(g(u, "scummvm", "music_driver"), "adlib");
	}

	void test_capture_of_an_unchanged_run_changes_nothing() {
		Play::Ini u = user();
		Play::Ini run = Play::mergeRun(pack(), home(), u, "kq1");
		TS_ASSERT(!Play::captureChanges(run, run, pack(), home(), "kq1", u));
		TS_ASSERT(!Play::captureChanges(run, Play::Ini(), pack(), home(), "kq1", u));
	}

	void test_capture_ignores_a_dropped_key() {
		Play::Ini u = user();
		Play::Ini run = Play::mergeRun(pack(), home(), u, "kq1");
		Play::Ini after = run;
		after.erase("kq1", "music_volume");
		TS_ASSERT(!Play::captureChanges(run, after, pack(), home(), "kq1", u));
		TS_ASSERT_EQUALS(g(u, "kq1", "music_volume"), "100");
	}

	void test_capture_back_to_the_layers_value_drops_it_from_user() {
		Play::Ini u;
		u.set("kq1", "talkspeed", "120");
		Play::Ini run = Play::mergeRun(pack(), home(), u, "kq1");
		Play::Ini after = run;
		after.set("kq1", "talkspeed", "60"); // the pack's
		TS_ASSERT(Play::captureChanges(run, after, pack(), home(), "kq1", u));
		TS_ASSERT_EQUALS(g(u, "kq1", "talkspeed"), "<none>");
		// Back to the home's common value: dropped from both sections of USER.INI.
		Play::Ini u2 = user();
		Play::Ini run2 = Play::mergeRun(pack(), home(), u2, "kq1");
		Play::Ini after2 = run2;
		after2.set("kq1", "music_driver", "mpu401");
		TS_ASSERT(Play::captureChanges(run2, after2, pack(), home(), "kq1", u2));
		TS_ASSERT_EQUALS(g(u2, "scummvm", "music_driver"), "<none>");
		TS_ASSERT_EQUALS(g(u2, "kq1", "music_driver"), "<none>");
	}

	void test_migration_keeps_only_the_players_choices() {
		Play::Ini old = Play::parseIni("[scummvm]\nmusic_driver=adlib\nsubtitles=false\nrender_target=auto\n"
									   "[kq1ko]\nhires_text_map=HIRESVF.MAP\nmusic_volume=150\nsubtitles=true\ngameid=kq1sci\n"
									   "[kq1kol]\nmusic_volume=90\ntalkspeed=30\ndos_vsync=off\n");
		Play::Ini m = Play::migrateOld(old);
		TS_ASSERT_EQUALS(m.sections.size(), 1u);
		TS_ASSERT_EQUALS(g(m, "scummvm", "music_volume"), "150");
		TS_ASSERT_EQUALS(g(m, "scummvm", "subtitles"), "true");
		TS_ASSERT_EQUALS(g(m, "scummvm", "talkspeed"), "30");
		TS_ASSERT_EQUALS(g(m, "scummvm", "dos_vsync"), "off");
		TS_ASSERT_EQUALS(g(m, "scummvm", "hires_text_map"), "<none>");
		TS_ASSERT_EQUALS(g(m, "scummvm", "music_driver"), "<none>");
		TS_ASSERT_EQUALS(g(m, "scummvm", "render_target"), "<none>");
		TS_ASSERT_EQUALS(g(m, "scummvm", "gameid"), "<none>");
	}

	void test_migration_whitelist() {
		const char *const yes[] = { "music_volume", "sfx_volume", "speech_volume", "mute", "speech_mute", "subtitles",
									"talkspeed", "dos_loading_screen", "dos_vsync" };
		for (size_t i = 0; i < sizeof(yes) / sizeof(yes[0]); ++i)
			TS_ASSERT(Play::isUserChoice(yes[i]));
		TS_ASSERT(!Play::isUserChoice("music_driver"));
		TS_ASSERT(!Play::isUserChoice("render_target"));
		TS_ASSERT(!Play::isUserChoice("language"));
	}

	void test_sound_choice_in_user_ini() {
		Play::Ini u = user();
		Play::setSoundChoice(u, Play::kSoundMt32);
		TS_ASSERT_EQUALS(g(u, "scummvm", "music_driver"), "mpu401");
		TS_ASSERT_EQUALS(g(u, "scummvm", "native_mt32"), "true");
		TS_ASSERT_EQUALS(g(u, "scummvm", "enable_gs"), "false");
		Play::SoundChoice c = Play::kSoundAdlib;
		TS_ASSERT(Play::soundChoiceOf(Play::writeIni(u), c));
		TS_ASSERT_EQUALS(c, Play::kSoundMt32);
		u.set("kq1", "enable_gs", "true");
		TS_ASSERT(Play::eraseSoundChoice(u));
		TS_ASSERT_EQUALS(g(u, "scummvm", "music_driver"), "<none>");
		TS_ASSERT_EQUALS(g(u, "kq1", "enable_gs"), "<none>");
		TS_ASSERT_EQUALS(g(u, "kq1", "music_volume"), "100");
		TS_ASSERT(!Play::eraseSoundChoice(u));
	}

	void test_language_folder_codes_are_the_engines() {
		TS_ASSERT_EQUALS(Play::languageFolderOf("ko"), "ko");
		TS_ASSERT_EQUALS(Play::languageFolderOf("KO"), "ko");
		TS_ASSERT_EQUALS(Play::languageFolderOf("kr"), "ko");
		TS_ASSERT_EQUALS(Play::languageFolderOf("jp"), "ja");
		TS_ASSERT_EQUALS(Play::languageFolderOf("ja"), "ja");
		TS_ASSERT_EQUALS(Play::languageFolderOf("gb"), "en");
		TS_ASSERT_EQUALS(Play::languageFolderOf("us"), "en");
		TS_ASSERT_EQUALS(Play::languageFolderOf("br"), "pt");
		TS_ASSERT_EQUALS(Play::languageFolderOf("cn"), "zh");
		TS_ASSERT_EQUALS(Play::languageFolderOf("zh-cn"), "zh");
		TS_ASSERT_EQUALS(Play::languageFolderOf("tw"), "zh");
		TS_ASSERT_EQUALS(Play::languageFolderOf("fr-CA"), "fr");
		TS_ASSERT_EQUALS(Play::languageFolderOf("se"), "sv");
		TS_ASSERT_EQUALS(Play::languageFolderOf("ua"), "");
		TS_ASSERT_EQUALS(Play::languageFolderOf("xx"), "");
		TS_ASSERT_EQUALS(Play::languageFolderOf("korean"), "");
		TS_ASSERT_EQUALS(Play::languageFolderOf(""), "");
	}

	void test_language_check() {
		std::vector<std::string> ko(1, "ko");
		TS_ASSERT(Play::hasLanguage("en", std::vector<std::string>(), ""));
		TS_ASSERT(Play::hasLanguage("ko", ko, ""));
		TS_ASSERT(Play::hasLanguage("ko", std::vector<std::string>(), "ko")); // an older pack: language=ko in its target
		TS_ASSERT(Play::hasLanguage("ko", std::vector<std::string>(), "KR"));
		TS_ASSERT(!Play::hasLanguage("de", ko, "ko"));
		TS_ASSERT(!Play::hasLanguage("", ko, ""));
		TS_ASSERT_EQUALS(Play::languagesOf(ko, ""), "en ko");
		TS_ASSERT_EQUALS(Play::languagesOf(ko, "ko"), "en ko");
		TS_ASSERT_EQUALS(Play::languagesOf(std::vector<std::string>(), ""), "en");
		std::vector<std::string> odd;
		odd.push_back("ja");
		odd.push_back("zz"); // a folder that is no language
		odd.push_back("en");
		TS_ASSERT_EQUALS(Play::languagesOf(odd, "ko"), "en ja ko");
	}

	void test_language_arg_forms() {
		TS_ASSERT_EQUALS(Play::languageArg(words("--language=ko", "kq1")), "ko");
		TS_ASSERT_EQUALS(Play::languageArg(words("-qja", "kq1")), "ja");
		TS_ASSERT_EQUALS(Play::languageArg(words("-q", "EN", "kq1")), "EN");
		TS_ASSERT_EQUALS(Play::languageArg(words("--language=ko", "--language=en", "kq1")), "en");
		TS_ASSERT_EQUALS(Play::languageArg(words("-c", "-qko.ini", "kq1")), "");
		TS_ASSERT_EQUALS(Play::languageArg(words("kq1")), "");
	}

	void test_bare_words_skip_option_values() {
		TS_ASSERT_EQUALS(Play::bareWords(words("--language=ko", "kq1")).size(), 1u);
		TS_ASSERT_EQUALS(Play::bareWords(words("-q", "ko", "kq1")).size(), 1u);
		TS_ASSERT_EQUALS(Play::bareWords(words("-p", "D:/G", "-f", "kq1"))[0], "kq1");
		std::vector<std::string> two = Play::bareWords(words("kq1", "ko"));
		TS_ASSERT_EQUALS(two.size(), 2u);
		TS_ASSERT_EQUALS(two[1], "ko");
		TS_ASSERT_EQUALS(Play::bareWords(words("--8bit")).size(), 0u);
	}

	void test_language_hint_for_two_target_words() {
		std::vector<std::string> t(1, "kq1");
		TS_ASSERT_EQUALS(Play::languageHint("KQ1", words("kq1", "KO"), t), "PLAY KQ1 --language=ko kq1");
		TS_ASSERT_EQUALS(Play::languageHint("KQ1", words("en", "kq1"), t), "PLAY KQ1 --language=en kq1");
		TS_ASSERT_EQUALS(Play::languageHint("KQ1", words("kq1", "xx"), t), "");
		TS_ASSERT_EQUALS(Play::languageHint("KQ1", words("kq2", "ko"), t), "");
		TS_ASSERT_EQUALS(Play::languageHint("KQ1", words("kq1", "ko", "en"), t), "");
	}

	void test_8bit_becomes_the_engines_set_options() {
		std::vector<std::string> a = Play::engineArgs(words("--language=ko", "--8bit", "kq1"));
		TS_ASSERT_EQUALS(a.size(), 4u);
		TS_ASSERT_EQUALS(a[0], "--language=ko");
		TS_ASSERT_EQUALS(a[1], "--set=render_target=clut8");
		TS_ASSERT_EQUALS(a[2], "--set=hires_text_font=bitmap");
		TS_ASSERT_EQUALS(a[3], "kq1");
		TS_ASSERT_EQUALS(Play::engineArgs(words("kq1")).size(), 1u);
		TS_ASSERT_EQUALS(Play::bareWords(a).size(), 1u);
	}
};
