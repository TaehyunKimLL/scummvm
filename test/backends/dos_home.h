#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/dos-home.h"

// The folder of the EXE is where the program writes and reads its own
// data, so a game folder can sit on a read-only drive.
class DosHomeTestSuite : public CxxTest::TestSuite {
public:
	void test_exe_dir_of_a_full_path() {
		TS_ASSERT_EQUALS(DOS::exeDir("C:\\SCUMMVM\\SCUMMVM.EXE"), "C:\\SCUMMVM");
		TS_ASSERT_EQUALS(DOS::exeDir("c:/scummvm/scumm.exe"), "c:/scummvm");
	}

	void test_exe_dir_at_a_drive_root_keeps_the_separator() {
		TS_ASSERT_EQUALS(DOS::exeDir("C:\\SCUMMVM.EXE"), "C:\\");
		TS_ASSERT_EQUALS(DOS::exeDir("\\SCUMMVM.EXE"), "\\");
	}

	void test_exe_dir_without_a_directory_is_empty() {
		TS_ASSERT_EQUALS(DOS::exeDir("SCUMMVM.EXE"), "");
		TS_ASSERT_EQUALS(DOS::exeDir("C:SCUMMVM.EXE"), "");
		TS_ASSERT_EQUALS(DOS::exeDir(nullptr), "");
		TS_ASSERT_EQUALS(DOS::exeDir(""), "");
	}

	void test_absolute_path_joins_to_the_cwd_with_forward_slashes() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:\\", "GAMES\\LB2KO"), "D:/GAMES/LB2KO");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "GAMES\\LB2KO"), "D:/PACK/GAMES/LB2KO");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK/", "GAMES"), "D:/PACK/GAMES");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "."), "D:/PACK/.");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", ".."), "D:/PACK/..");
	}

	void test_absolute_path_keeps_a_full_path() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "C:\\GAMES\\X"), "C:\\GAMES\\X");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "C:/GAMES"), "C:/GAMES");
	}

	void test_rooted_path_takes_the_drive_of_the_cwd() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "\\GAMES\\X"), "D:/GAMES/X");
	}

	static Common::String driveE(char letter) {
		return letter == 'E' || letter == 'e' ? "E:/MUSIC" : "";
	}

	void test_drive_relative_path_takes_that_drives_directory() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "E:GAMES", driveE), "E:/MUSIC/GAMES");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "E:", driveE), "E:/MUSIC");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "e:A\\B", driveE), "E:/MUSIC/A/B");
	}

	void test_drive_relative_path_on_the_cwd_drive_uses_the_cwd() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "D:GAMES", driveE), "D:/PACK/GAMES");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "d:", driveE), "D:/PACK");
	}

	void test_drive_relative_path_is_left_alone_when_the_drive_is_unknown() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "F:GAMES", driveE), "F:GAMES");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK", "F:GAMES"), "F:GAMES");
	}

	void test_same_path_ignores_case_separators_and_a_trailing_one() {
		TS_ASSERT(DOS::samePath("C:\\SCUMMVM", "c:/scummvm/"));
		TS_ASSERT(DOS::samePath("C:\\", "C:/"));
		TS_ASSERT(!DOS::samePath("C:\\SCUMMVM", "D:\\SCUMMVM"));
		TS_ASSERT(!DOS::samePath("C:\\SCUMMVM", "C:\\SCUMMVM\\GAMES"));
	}

	static Common::Array<Common::String> run(const Common::Array<const char *> &a, const char *cwd = "D:/PACK") {
		return DOS::absolutizeArgs((int)a.size(), a.data(), cwd);
	}

	void test_long_path_option_with_equals() {
		Common::Array<const char *> a;
		a.push_back("C:\\SCUMMVM\\SCUMMVM.EXE");
		a.push_back("--path=GAMES\\LB2KO");
		a.push_back("lb2ko");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 3u);
		TS_ASSERT_EQUALS(r[0], "C:\\SCUMMVM\\SCUMMVM.EXE");
		TS_ASSERT_EQUALS(r[1], "--path=D:/PACK/GAMES/LB2KO");
		TS_ASSERT_EQUALS(r[2], "lb2ko");
	}

	void test_short_path_option_attached_and_separate() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("-pGAMES");
		a.push_back("-p");
		a.push_back("MORE");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[1], "-pD:/PACK/GAMES");
		TS_ASSERT_EQUALS(r[2], "-p");
		TS_ASSERT_EQUALS(r[3], "D:/PACK/MORE");
	}

	void test_other_path_options() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("--savepath=S");
		a.push_back("--extrapath=E");
		a.push_back("-cmy.ini");
		a.push_back("-l");
		a.push_back("log.txt");
		a.push_back("--soundfont=a.sf2");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r[1], "--savepath=D:/PACK/S");
		TS_ASSERT_EQUALS(r[2], "--extrapath=D:/PACK/E");
		TS_ASSERT_EQUALS(r[3], "-cD:/PACK/my.ini");
		TS_ASSERT_EQUALS(r[5], "D:/PACK/log.txt");
		TS_ASSERT_EQUALS(r[6], "--soundfont=D:/PACK/a.sf2");
	}

	void test_options_without_a_path_value_are_copied() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("--language=ko");
		a.push_back("-e");
		a.push_back("adlib");
		a.push_back("--fullscreen");
		a.push_back("--path");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r.size(), a.size());
		for (uint i = 0; i < r.size(); ++i)
			TS_ASSERT_EQUALS(r[i], a[i]);
	}

	void test_trailing_short_option_without_value() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("-p");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 2u);
		TS_ASSERT_EQUALS(r[1], "-p");
	}
	void test_drive_relative_option_value_uses_the_drive_callback() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("--path=E:GAMES");
		Common::Array<Common::String> r = DOS::absolutizeArgs((int)a.size(), a.data(), "D:/PACK", driveE);
		TS_ASSERT_EQUALS(r[1], "--path=E:/MUSIC/GAMES");
	}

	void test_empty_path_value_and_empty_next_word_are_copied() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("--path=");
		a.push_back("-p");
		a.push_back("");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[1], "--path=");
		TS_ASSERT_EQUALS(r[3], "");
	}

	void test_upper_case_short_options_and_initial_cfg() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("-PG");
		a.push_back("--config=c.ini");
		a.push_back("-i");
		a.push_back("i.ini");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r[1], "-PD:/PACK/G");
		TS_ASSERT_EQUALS(r[2], "--config=D:/PACK/c.ini");
		TS_ASSERT_EQUALS(r[4], "D:/PACK/i.ini");
	}

	void test_path_option_in_the_middle_of_the_arguments() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("--fullscreen");
		a.push_back("--path=GAMES");
		a.push_back("target");
		Common::Array<Common::String> r = run(a);
		TS_ASSERT_EQUALS(r.size(), 4u);
		TS_ASSERT_EQUALS(r[2], "--path=D:/PACK/GAMES");
		TS_ASSERT_EQUALS(r[3], "target");
	}

	void test_add_detect_and_auto_detect_without_a_path_scan_the_cwd() {
		static const char *const kCommands[] = { "--add", "--detect", "--auto-detect", "-a", "-A" };
		for (uint i = 0; i < ARRAYSIZE(kCommands); ++i) {
			Common::Array<const char *> a;
			a.push_back("X");
			a.push_back(kCommands[i]);
			Common::Array<Common::String> r = run(a);
			TS_ASSERT_EQUALS(r.size(), 3u);
			TS_ASSERT_EQUALS(r[1], kCommands[i]);
			TS_ASSERT_EQUALS(r[2], "--path=D:/PACK");
		}
	}

	void test_add_with_a_path_gets_no_second_one() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("--add");
		a.push_back("-p");
		a.push_back("GAMES");
		TS_ASSERT_EQUALS(run(a).size(), 4u);
		Common::Array<const char *> b;
		b.push_back("X");
		b.push_back("--path=GAMES");
		b.push_back("--add");
		TS_ASSERT_EQUALS(run(b).size(), 3u);
	}

	void test_a_plain_target_launch_gets_no_path() {
		Common::Array<const char *> a;
		a.push_back("X");
		a.push_back("lb2ko");
		TS_ASSERT_EQUALS(run(a).size(), 2u);
	}
};
