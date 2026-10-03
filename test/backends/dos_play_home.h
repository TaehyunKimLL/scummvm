#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/launcher/play-home.h"

#include <string>
#include <vector>

// The folder of the EXE is where the program writes and reads its own
// data, so a game folder can sit on a read-only drive.
class PlayHomeTestSuite : public CxxTest::TestSuite {
public:
	void test_exe_dir_of_a_full_path() {
		TS_ASSERT_EQUALS(Play::exeDir("C:\\SCUMMVM\\PLAY.EXE"), "C:\\SCUMMVM");
		TS_ASSERT_EQUALS(Play::exeDir("c:/scummvm/scumm.exe"), "c:/scummvm");
	}

	void test_exe_dir_at_a_drive_root_keeps_the_separator() {
		TS_ASSERT_EQUALS(Play::exeDir("C:\\PLAY.EXE"), "C:\\");
		TS_ASSERT_EQUALS(Play::exeDir("\\PLAY.EXE"), "\\");
	}

	void test_exe_dir_without_a_directory_is_empty() {
		TS_ASSERT_EQUALS(Play::exeDir("PLAY.EXE"), "");
		TS_ASSERT_EQUALS(Play::exeDir("C:PLAY.EXE"), "");
		TS_ASSERT_EQUALS(Play::exeDir(nullptr), "");
		TS_ASSERT_EQUALS(Play::exeDir(""), "");
	}

	void test_absolute_path_joins_to_the_cwd_with_forward_slashes() {
		TS_ASSERT_EQUALS(Play::absolutePath("D:\\", "GAMES\\LB2KO"), "D:/GAMES/LB2KO");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "GAMES\\LB2KO"), "D:/PACK/GAMES/LB2KO");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK/", "GAMES"), "D:/PACK/GAMES");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "."), "D:/PACK/.");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", ".."), "D:/PACK/..");
	}

	void test_absolute_path_keeps_a_full_path() {
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "C:\\GAMES\\X"), "C:\\GAMES\\X");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "C:/GAMES"), "C:/GAMES");
	}

	void test_rooted_path_takes_the_drive_of_the_cwd() {
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "\\GAMES\\X"), "D:/GAMES/X");
	}

	static std::string driveE(char letter) {
		return letter == 'E' || letter == 'e' ? "E:/MUSIC" : "";
	}

	void test_drive_relative_path_takes_that_drives_directory() {
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "E:GAMES", driveE), "E:/MUSIC/GAMES");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "E:", driveE), "E:/MUSIC");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "e:A\\B", driveE), "E:/MUSIC/A/B");
	}

	void test_drive_relative_path_on_the_cwd_drive_uses_the_cwd() {
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "D:GAMES", driveE), "D:/PACK/GAMES");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "d:", driveE), "D:/PACK");
	}

	void test_drive_relative_path_is_left_alone_when_the_drive_is_unknown() {
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "F:GAMES", driveE), "F:GAMES");
		TS_ASSERT_EQUALS(Play::absolutePath("D:/PACK", "F:GAMES"), "F:GAMES");
	}

	void test_same_path_ignores_case_separators_and_a_trailing_one() {
		TS_ASSERT(Play::samePath("C:\\SCUMMVM", "c:/scummvm/"));
		TS_ASSERT(Play::samePath("C:\\", "C:/"));
		TS_ASSERT(!Play::samePath("C:\\SCUMMVM", "D:\\SCUMMVM"));
		TS_ASSERT(!Play::samePath("C:\\SCUMMVM", "C:\\SCUMMVM\\GAMES"));
	}

	static std::vector<std::string> run(const std::vector<const char *> &a, const char *cwd = "D:/PACK") {
		return Play::absolutizeArgs((int)a.size(), a.data(), cwd);
	}

	void test_long_path_option_with_equals() {
		std::vector<const char *> a;
		a.push_back("C:\\SCUMMVM\\PLAY.EXE");
		a.push_back("--path=GAMES\\LB2KO");
		a.push_back("lb2ko");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 3u);
		TS_ASSERT_EQUALS(r[0], "C:\\SCUMMVM\\PLAY.EXE");
		TS_ASSERT_EQUALS(r[1], "--path=D:/PACK/GAMES/LB2KO");
		TS_ASSERT_EQUALS(r[2], "lb2ko");
	}

	void test_short_path_option_attached_and_separate() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("-pGAMES");
		a.push_back("-p");
		a.push_back("MORE");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[1], "-pD:/PACK/GAMES");
		TS_ASSERT_EQUALS(r[2], "-p");
		TS_ASSERT_EQUALS(r[3], "D:/PACK/MORE");
	}

	void test_other_path_options() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("--savepath=S");
		a.push_back("--extrapath=E");
		a.push_back("-cmy.ini");
		a.push_back("-l");
		a.push_back("log.txt");
		a.push_back("--soundfont=a.sf2");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r[1], "--savepath=D:/PACK/S");
		TS_ASSERT_EQUALS(r[2], "--extrapath=D:/PACK/E");
		TS_ASSERT_EQUALS(r[3], "-cD:/PACK/my.ini");
		TS_ASSERT_EQUALS(r[5], "D:/PACK/log.txt");
		TS_ASSERT_EQUALS(r[6], "--soundfont=D:/PACK/a.sf2");
	}

	void test_options_without_a_path_value_are_copied() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("--language=ko");
		a.push_back("-e");
		a.push_back("adlib");
		a.push_back("--fullscreen");
		a.push_back("--path");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r.size(), a.size());
		for (size_t i = 0; i < r.size(); ++i)
			TS_ASSERT_EQUALS(r[i], a[i]);
	}

	void test_trailing_short_option_without_value() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("-p");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 2u);
		TS_ASSERT_EQUALS(r[1], "-p");
	}
	void test_drive_relative_option_value_uses_the_drive_callback() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("--path=E:GAMES");
		std::vector<std::string> r = Play::absolutizeArgs((int)a.size(), a.data(), "D:/PACK", driveE);
		TS_ASSERT_EQUALS(r[1], "--path=E:/MUSIC/GAMES");
	}

	void test_empty_path_value_and_empty_next_word_are_copied() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("--path=");
		a.push_back("-p");
		a.push_back("");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[1], "--path=");
		TS_ASSERT_EQUALS(r[3], "");
	}

	void test_upper_case_short_options_and_initial_cfg() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("-PG");
		a.push_back("--config=c.ini");
		a.push_back("-i");
		a.push_back("i.ini");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r[1], "-PD:/PACK/G");
		TS_ASSERT_EQUALS(r[2], "--config=D:/PACK/c.ini");
		TS_ASSERT_EQUALS(r[4], "D:/PACK/i.ini");
	}

	void test_path_option_in_the_middle_of_the_arguments() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("--fullscreen");
		a.push_back("--path=GAMES");
		a.push_back("target");
		std::vector<std::string> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[2], "--path=D:/PACK/GAMES");
		TS_ASSERT_EQUALS(r[3], "target");
	}

	void test_add_detect_and_auto_detect_without_a_path_scan_the_cwd() {
		static const char *const kCommands[] = { "--add", "--detect", "--auto-detect", "-a", "-A" };
		for (size_t i = 0; i < (sizeof(kCommands) / sizeof(kCommands[0])); ++i) {
			std::vector<const char *> a;
			a.push_back("X");
			a.push_back(kCommands[i]);
			std::vector<std::string> r = run(a);
			TS_ASSERT_EQUALS(r.size(), 3u);
			TS_ASSERT_EQUALS(r[1], kCommands[i]);
			TS_ASSERT_EQUALS(r[2], "--path=D:/PACK");
		}
	}

	void test_add_with_a_path_gets_no_second_one() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("--add");
		a.push_back("-p");
		a.push_back("GAMES");
		TS_ASSERT_EQUALS(run(a).size(), 4u);
		std::vector<const char *> b;
		b.push_back("X");
		b.push_back("--path=GAMES");
		b.push_back("--add");
		TS_ASSERT_EQUALS(run(b).size(), 3u);
	}

	void test_a_plain_target_launch_gets_no_path() {
		std::vector<const char *> a;
		a.push_back("X");
		a.push_back("lb2ko");
		TS_ASSERT_EQUALS(run(a).size(), 2u);
	}

	void test_valid_ids() {
		TS_ASSERT(Play::validId("MI1KO"));
		TS_ASSERT(Play::validId("lb-2_ko"));
		TS_ASSERT(Play::validId("12345678"));
		TS_ASSERT(!Play::validId(""));
		TS_ASSERT(!Play::validId("123456789"));
		TS_ASSERT(!Play::validId("..\\X"));
		TS_ASSERT(!Play::validId("A B"));
		TS_ASSERT(!Play::validId("A.B"));
	}

	void test_join_uses_one_forward_slash() {
		TS_ASSERT_EQUALS(Play::join("C:/SCUMMVM", "GAMES"), "C:/SCUMMVM/GAMES");
		TS_ASSERT_EQUALS(Play::join("C:\\SCUMMVM\\", "GAMES\\X"), "C:/SCUMMVM/GAMES/X");
		TS_ASSERT_EQUALS(Play::join("D:/", "MI1KO.INI"), "D:/MI1KO.INI");
	}

	void test_engine_of_the_first_engineid_line() {
		TS_ASSERT_EQUALS(Play::engineOf("[scummvm]\r\nmusic_driver=adlib\r\n[mi1ko]\r\nengineid=SCUMM\r\ngameid=monkey\r\n"), "scumm");
		TS_ASSERT_EQUALS(Play::engineOf("[lb1]\ngameid=laurabow\n  engineid=sci\n"), "sci");
		TS_ASSERT_EQUALS(Play::engineOf("[scummvm]\nmusic_driver=adlib\n"), "");
		TS_ASSERT_EQUALS(Play::engineOf(""), "");
	}

	void test_profile_of_a_game_is_below_games_in_the_home_folder() {
		Play::Profile p = Play::profileOf("C:\\SCUMMVM", "mi1ko");
		TS_ASSERT_EQUALS(p.dir, "C:/SCUMMVM/GAMES/MI1KO");
		TS_ASSERT_EQUALS(p.ini, "C:/SCUMMVM/GAMES/MI1KO/SCUMMVM.INI");
		TS_ASSERT_EQUALS(p.saves, "C:/SCUMMVM/GAMES/MI1KO/SAVES");
		TS_ASSERT_EQUALS(p.log, "C:/SCUMMVM/GAMES/MI1KO/SCUMMVM.LOG");
		TS_ASSERT_EQUALS(p.exitLog, "C:/SCUMMVM/GAMES/MI1KO/EXITLOG.TXT");
		TS_ASSERT_EQUALS(p.sound, "C:/SCUMMVM/GAMES/MI1KO/SOUND.INI");
	}

	void test_child_args_name_the_profile_and_the_game_folder() {
		Play::Profile p = Play::profileOf("C:/SCUMMVM", "MI1KO");
		std::vector<std::string> extra;
		extra.push_back("mi1ko");
		std::vector<std::string> r = Play::childArgs(p, "D:/GAMES/MI1KO", extra);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[0], "--config=C:/SCUMMVM/GAMES/MI1KO/SCUMMVM.INI");
		TS_ASSERT_EQUALS(r[1], "--savepath=C:/SCUMMVM/GAMES/MI1KO/SAVES");
		TS_ASSERT_EQUALS(r[2], "--path=D:/GAMES/MI1KO");
		TS_ASSERT_EQUALS(r[3], "mi1ko");
	}

	void test_child_args_keep_a_path_of_the_command_line() {
		Play::Profile p = Play::profileOf("C:/SCUMMVM", "MI1KO");
		std::vector<std::string> extra;
		extra.push_back("--path=E:/OTHER");
		extra.push_back("mi1ko");
		std::vector<std::string> r = Play::childArgs(p, "D:/GAMES/MI1KO", extra);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[2], "--path=E:/OTHER");
		std::vector<std::string> shortForm;
		shortForm.push_back("-pE:/OTHER");
		TS_ASSERT_EQUALS(Play::childArgs(p, "D:/G", shortForm).size(), 3u);
	}

	void test_child_args_without_a_game_folder_add_no_path() {
		Play::Profile p = Play::profileOf("C:/SCUMMVM", "MI1KO");
		std::vector<std::string> extra;
		extra.push_back("mi1ko");
		TS_ASSERT_EQUALS(Play::childArgs(p, "", extra).size(), 3u);
	}

	void test_engine_of_a_target_wins_over_the_first_engine() {
		const char *ini = "[mi1ko]\nengineid=scumm\n[lb1]\nengineid=sci\n";
		TS_ASSERT_EQUALS(Play::engineOf(ini, "lb1"), "sci");
		TS_ASSERT_EQUALS(Play::engineOf(ini, "MI1KO"), "scumm");
		TS_ASSERT_EQUALS(Play::engineOf(ini, "other"), "scumm");
		TS_ASSERT_EQUALS(Play::engineOf(ini), "scumm");
	}

	void test_engine_of_ignores_case_spaces_and_comments() {
		TS_ASSERT_EQUALS(Play::engineOf("[ LB1 ]\r\n; engineid=scumm\r\n EngineID = SCI \r\n", "lb1"), "sci");
		TS_ASSERT_EQUALS(Play::engineOf("# engineid=scumm\n[x]\n"), "");
	}

	void test_has_target() {
		TS_ASSERT(Play::hasTarget("[lb1]\ngameid=laurabow\n", "LB1"));
		TS_ASSERT(!Play::hasTarget("[lb1]\ngameid=laurabow\n", "lb2"));
		TS_ASSERT(!Play::hasTarget("[lb1]\n[lb2]\ngameid=x\n", "lb1"));
		TS_ASSERT(!Play::hasTarget("", "lb1"));
	}

	void test_target_of_is_the_last_word_without_a_dash() {
		std::vector<std::string> e;
		TS_ASSERT_EQUALS(Play::targetOf(e), "");
		e.push_back("--fullscreen");
		e.push_back("lb1");
		TS_ASSERT_EQUALS(Play::targetOf(e), "lb1");
		e.push_back("-d3");
		TS_ASSERT_EQUALS(Play::targetOf(e), "lb1");
	}

	void test_dos_path_uses_backslashes() {
		TS_ASSERT_EQUALS(Play::dosPath("C:/SCUMMVM/GAMES/MI1KO"), "C:\\SCUMMVM\\GAMES\\MI1KO");
		TS_ASSERT_EQUALS(Play::dosPath(""), "");
	}

	void test_default_is_a_reserved_id_in_upper_case_only() {
		TS_ASSERT(Play::isReservedId("DEFAULT"));
		TS_ASSERT(!Play::isReservedId("MI1KO"));
		TS_ASSERT(!Play::isReservedId("DEFAULT2"));
		TS_ASSERT(Play::validId("DEFAULT"));
	}
};
