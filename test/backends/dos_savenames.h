#include <cxxtest/TestSuite.h>

#include "backends/platform/dos/dos-savefile.h"
#include "backends/platform/dos/dos-savenames.h"
#include "common/config-manager.h"
#include "common/savefile.h"

#include "../system/null_osystem.h"

#include "common/algorithm.h"
#include "common/fs.h"

class DosSaveNamesTestSuite : public CxxTest::TestSuite {
public:
	void test_patternTruncated() {
		TS_ASSERT_EQUALS(DOS::dosPattern("kq1-rgb565.###"), "kq1-rgb5.###");
		TS_ASSERT_EQUALS(DOS::dosPattern("kq1sv.###"), "kq1sv.###");
		TS_ASSERT_EQUALS(DOS::dosPattern("monkey2-ko.s##"), "monkey2-.s##");
		TS_ASSERT_EQUALS(DOS::dosPattern("*"), "*");
	}

	void test_backToTheEngineName() {
		TS_ASSERT_EQUALS(DOS::engineName("kq1-rgb565.###", "KQ1-RGB5.001"), "kq1-rgb565.001");
		TS_ASSERT_EQUALS(DOS::engineName("monkey2-ko.s##", "MONKEY2-.S12"), "monkey2-ko.s12");
		TS_ASSERT_EQUALS(DOS::engineName("kq1sv.###", "KQ1SV.001"), "KQ1SV.001");	// short names are left as listed
		TS_ASSERT_EQUALS(DOS::engineName("k*.###", "KQ1-RGB5.001"), "KQ1-RGB5.001");	// a wildcard base cannot be mapped
	}

	// The save manager end to end on a real folder: the engine's long name is written, listed in a NEW session (a new
	// manager over the folder), loaded, found and removed by that same long name - SCI restores by the name it listed.
	void test_longTargetSurvivesASession() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		// A folder of the test's own under the build directory (the runner's working directory; `make clean` removes it).
		const Common::Path dir("test/dos-savenames-tmp", '/');
		Common::FSNode dirNode(dir);
		if (!dirNode.exists())
			TS_ASSERT(dirNode.createDirectory());
		ConfMan.setPath("savepath", dir, Common::ConfigManager::kApplicationDomain);
		{	// a failed earlier run leaves files behind: start empty
			DOS::SaveFileManager clean(dir);
			const Common::StringArray old = clean.listSavefiles("*");
			for (uint i = 0; i < old.size(); ++i)
				clean.removeSavefile(old[i]);
		}

		{
			DOS::SaveFileManager first(dir);
			Common::OutSaveFile *out = first.openForSaving("kq1-rgb565.001", false);
			TS_ASSERT(out != nullptr);
			if (out) {
				out->writeString("hello");
				out->finalize();
				delete out;
			}
			// A short target is not touched.
			out = first.openForSaving("kq1sv.001", false);
			TS_ASSERT(out != nullptr);
			if (out) {
				out->writeString("short");
				out->finalize();
				delete out;
			}
		}
		// What is on the disk is the 8.3 name: the file DOS would have made.
		Common::StringArray onDisk;
		Common::FSList children;
		TS_ASSERT(dirNode.getChildren(children, Common::FSNode::kListFilesOnly));
		for (uint i = 0; i < children.size(); ++i)
			onDisk.push_back(children[i].getName());
		Common::sort(onDisk.begin(), onDisk.end());
		TS_ASSERT_EQUALS(onDisk.size(), 2u);
		if (onDisk.size() == 2) {
			TS_ASSERT_EQUALS(onDisk[0], "kq1-rgb5.001");
			TS_ASSERT_EQUALS(onDisk[1], "kq1sv.001");
		}

		{
			DOS::SaveFileManager second(dir);
			const Common::StringArray listed = second.listSavefiles("kq1-rgb565.###");
			TS_ASSERT_EQUALS(listed.size(), 1u);
			if (listed.size() == 1)
				TS_ASSERT_EQUALS(listed[0], "kq1-rgb565.001");
			TS_ASSERT(second.exists("kq1-rgb565.001"));
			Common::InSaveFile *in = second.openForLoading("kq1-rgb565.001");
			TS_ASSERT(in != nullptr);
			if (in) {
				TS_ASSERT_EQUALS(in->readString(0, 5), "hello");
				delete in;
			}
			TS_ASSERT_EQUALS(second.listSavefiles("kq1sv.###").size(), 1u);
			TS_ASSERT_EQUALS(second.listSavefiles("kq1sv.###")[0], "kq1sv.001");
			// A pattern of another long target does not find them.
			TS_ASSERT_EQUALS(second.listSavefiles("lb1-other.###").size(), 0u);
			TS_ASSERT(second.removeSavefile("kq1-rgb565.001"));
			TS_ASSERT(!second.exists("kq1-rgb565.001"));
			TS_ASSERT_EQUALS(second.listSavefiles("kq1-rgb565.###").size(), 0u);
		}
		{
			DOS::SaveFileManager last(dir);
			const Common::StringArray rest = last.listSavefiles("*");
			for (uint i = 0; i < rest.size(); ++i)
				last.removeSavefile(rest[i]);
		}
		Common::uninstall_null_g_system();
#endif
	}
};
