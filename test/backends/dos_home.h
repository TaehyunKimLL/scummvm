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

	void test_is_absolute() {
		TS_ASSERT(DOS::isAbsolutePath("D:\\GAMES"));
		TS_ASSERT(DOS::isAbsolutePath("d:/games"));
		TS_ASSERT(DOS::isAbsolutePath("\\GAMES"));
		TS_ASSERT(!DOS::isAbsolutePath("GAMES\\LB2KO"));
		TS_ASSERT(!DOS::isAbsolutePath("D:GAMES"));
		TS_ASSERT(!DOS::isAbsolutePath(""));
		TS_ASSERT(!DOS::isAbsolutePath(nullptr));
	}

	void test_absolute_path_joins_to_the_cwd() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:\\", "GAMES\\LB2KO"), "D:\\GAMES\\LB2KO");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:\\PACK", "GAMES"), "D:\\PACK\\GAMES");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:/PACK/", "GAMES"), "D:/PACK/GAMES");
		TS_ASSERT_EQUALS(DOS::absolutePath("D:\\PACK", "."), "D:\\PACK\\.");
	}

	void test_absolute_path_keeps_a_full_path() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:\\PACK", "C:\\GAMES\\X"), "C:\\GAMES\\X");
	}

	void test_rooted_path_takes_the_drive_of_the_cwd() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:\\PACK", "\\GAMES"), "D:\\GAMES");
	}

	void test_drive_relative_path_is_left_alone() {
		TS_ASSERT_EQUALS(DOS::absolutePath("D:\\PACK", "E:GAMES"), "E:GAMES");
	}

	void test_same_path_ignores_case_separators_and_a_trailing_one() {
		TS_ASSERT(DOS::samePath("C:\\SCUMMVM", "c:/scummvm/"));
		TS_ASSERT(DOS::samePath("C:\\", "C:/"));
		TS_ASSERT(!DOS::samePath("C:\\SCUMMVM", "D:\\SCUMMVM"));
		TS_ASSERT(!DOS::samePath("C:\\SCUMMVM", "C:\\SCUMMVM\\GAMES"));
	}

	static Common::Array<Common::String> run(const Common::Array<const char *> &a, const char *cwd = "D:\\") {
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
		TS_ASSERT_EQUALS(r[1], "--path=D:\\GAMES\\LB2KO");
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
		TS_ASSERT_EQUALS(r[1], "-pD:\\GAMES");
		TS_ASSERT_EQUALS(r[2], "-p");
		TS_ASSERT_EQUALS(r[3], "D:\\MORE");
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
		TS_ASSERT_EQUALS(r[1], "--savepath=D:\\S");
		TS_ASSERT_EQUALS(r[2], "--extrapath=D:\\E");
		TS_ASSERT_EQUALS(r[3], "-cD:\\my.ini");
		TS_ASSERT_EQUALS(r[5], "D:\\log.txt");
		TS_ASSERT_EQUALS(r[6], "--soundfont=D:\\a.sf2");
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
};
