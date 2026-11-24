#include <cxxtest/TestSuite.h>

#ifdef DOS_SAVE_MANAGER_TEST
#include "backends/platform/dos/dos-savefile.h"
#endif
#include "backends/platform/dos/dos-savenames.h"
#include "common/config-manager.h"
#include "common/savefile.h"

#include "../system/null_osystem.h"

#include "common/algorithm.h"
#include "common/fs.h"

class DosSaveNamesTestSuite : public CxxTest::TestSuite {
public:
	void test_mappedNamesAreDistinctPerTarget() {
		// A long base keeps 5 characters and a hash of the whole base: the shared first 8 characters no longer share a file.
		const Common::String a = DOS::dosPattern("lsl1sci-en.001"), b = DOS::dosPattern("lsl1sci-ko.001");
		TS_ASSERT(a != b);
		TS_ASSERT_EQUALS(a.size(), 12u);	// 8 + . + 3
		TS_ASSERT(a.hasPrefix("lsl1"));
		TS_ASSERT_EQUALS(DOS::dosPattern("kq1sv.###"), "kq1sv.###");	// short bases untouched
		TS_ASSERT_EQUALS(DOS::dosPattern("*"), "*");
		TS_ASSERT_EQUALS(DOS::dosPattern("k*.###"), "k*.###");
		TS_ASSERT_EQUALS(DOS::dosPattern("a?c.s##"), "a?c.s##");
		// the same name in another case maps to the same file
		TS_ASSERT_EQUALS(DOS::dosPattern("kq1-rgb565.001"), DOS::dosPattern("KQ1-RGB565.001"));
		TS_ASSERT_EQUALS(DOS::dosPattern("name.sav12"), "name.sav");
		// a dotted base has no DOS name: it is mapped, with '_' for dots
		const Common::String d = DOS::dosPattern("a.b.c.001");
		TS_ASSERT_EQUALS(d.size(), 12u);
		TS_ASSERT(d.hasPrefix("a_b_"));
	}

	void test_backToTheEngineName() {
		const Common::String listed = DOS::dosPattern("kq1-rgb565.001");
		TS_ASSERT_EQUALS(DOS::engineName("kq1-rgb565.###", listed), "kq1-rgb565.001");
		TS_ASSERT_EQUALS(DOS::engineName("kq1-rgb565.###", DOS::dosPattern("kq1-rgb565.001")), "kq1-rgb565.001");
		TS_ASSERT_EQUALS(DOS::engineName("kq1sv.###", "KQ1SV.001"), "KQ1SV.001");	// short names are left as listed
		TS_ASSERT_EQUALS(DOS::engineName("k*.###", "KQ1SV.001"), "KQ1SV.001");	// a wildcard base cannot be mapped
	}

	// Independent model, exhaustive over what the engines do: for many target names (shared prefixes, dots, case) the
	// mapped file name of "<target>.<slot>" is injective over (target lower-cased, slot), 8.3-valid, and listing's
	// back-mapping inverts it.
	void test_randomMappingIsInjectiveAndInvertible() {
		uint32 seed = 12345;
		Common::StringArray targets;
		const char *prefixes[] = { "lsl1sci-", "kq1-rgb", "monkey2-", "mi1ko-x", "a.b.c-" };
		for (uint p = 0; p < ARRAYSIZE(prefixes); ++p)
			for (int i = 0; i < 60; ++i) {
				seed = seed * 1103515245 + 12345;
				Common::String t(prefixes[p]);
				for (int k = 0; k < 1 + (int)(seed >> 28) % 6; ++k) {
					seed = seed * 1103515245 + 12345;
					t += (char)('a' + (seed >> 16) % 26);
				}
				targets.push_back(t);
			}
		Common::HashMap<Common::String, Common::String> owner;	// mapped name (lower case) -> "target.slot" lower case
		uint collisions = 0, checked = 0;
		for (uint i = 0; i < targets.size(); ++i)
			for (int slot = 0; slot < 1000; slot += 37) {
				const Common::String eng = Common::String::format("%s.%03d", targets[i].c_str(), slot);
				Common::String m = DOS::dosPattern(eng);
				m.toLowercase();
				Common::String want = eng;
				want.toLowercase();
				++checked;
				// valid 8.3: base <= 8, one dot, ext <= 3
				const size_t dot = m.findFirstOf('.');
				TS_ASSERT(dot != Common::String::npos && dot <= 8 && m.size() - dot - 1 <= 3 && m.findLastOf('.') == dot);
				if (owner.contains(m) && owner[m] != want)
					++collisions;
				owner[m] = want;
				// the listing's back-mapping returns the engine's name
				const Common::String pattern = targets[i] + ".###";
				TS_ASSERT_EQUALS(DOS::engineName(pattern, m), eng);
			}
		TS_ASSERT_EQUALS(collisions, 0u);
		TS_ASSERT(checked > 3000);
	}

	// Randomized isolation through the REAL manager: many targets (shared 8-character prefixes, dots, case), each saves
	// slots with its own content in one session; a new session sees exactly its own slots by its own pattern, loads its
	// own bytes, and rename/copy between colliding or equal names never loses a file.
	void test_randomTargetsStayIsolated() {
#if NULL_OSYSTEM_IS_AVAILABLE && defined(DOS_SAVE_MANAGER_TEST)
		Common::install_null_g_system();
		const Common::Path dir("test/dos-savenames-tmp", '/');
		Common::FSNode dirNode(dir);
		if (!dirNode.exists())
			TS_ASSERT(dirNode.createDirectory());
		ConfMan.setPath("savepath", dir, Common::ConfigManager::kApplicationDomain);
		{
			DOS::SaveFileManager clean(dir);
			const Common::StringArray old = clean.listSavefiles("*");
			for (uint i = 0; i < old.size(); ++i)
				clean.removeSavefile(old[i]);
		}
		const char *prefixes[] = { "lsl1sci-", "kq1-rgb", "monkey2-", "mi1ko-x", "a.b.c-", "short" };
		Common::StringArray targets;
		uint32 seed = 777;
		for (uint p = 0; p < ARRAYSIZE(prefixes); ++p)
			for (int i = 0; i < 8; ++i) {
				seed = seed * 1103515245 + 12345;
				Common::String t(prefixes[p]);
				t += (char)('a' + (seed >> 16) % 26);
				if (seed & 0x100000)
					t += "-ko";
				bool dup = false;
				for (uint k = 0; k < targets.size(); ++k)
					dup = dup || targets[k] == t;
				if (!dup)
					targets.push_back(t);
			}
		{
			DOS::SaveFileManager first(dir);
			for (uint t = 0; t < targets.size(); ++t)
				for (int slot = 1; slot <= 3; ++slot) {
					Common::OutSaveFile *out = first.openForSaving(Common::String::format("%s.%03d", targets[t].c_str(), slot), false);
					TS_ASSERT(out != nullptr);
					if (out) {
						out->writeString(Common::String::format("%s#%d|\xed\x95\x9c\xea\xb8\x80", targets[t].c_str(), slot));
						out->finalize();
						delete out;
					}
				}
		}
		{
			DOS::SaveFileManager second(dir);
			for (uint t = 0; t < targets.size(); ++t) {
				const Common::StringArray listed = second.listSavefiles(targets[t] + ".###");
				TS_ASSERT_EQUALS(listed.size(), 3u);
				for (uint i = 0; i < listed.size(); ++i) {
					TS_ASSERT(listed[i].hasPrefix(targets[t] + "."));
					Common::InSaveFile *in = second.openForLoading(listed[i]);
					TS_ASSERT(in != nullptr);
					if (in) {
						const Common::String got = in->readString(0, 200);
						TS_ASSERT(got.hasPrefix(targets[t] + "#"));
						delete in;
					}
				}
				// the same by the upper-case name
				Common::String up = targets[t];
				up.toUppercase();
				TS_ASSERT(second.exists(up + ".001"));
			}
			// rename and copy between two names that DOS keeps as one file: refused or a no-op, the file survives
			const Common::String a = targets[0] + ".001";
			TS_ASSERT(second.renameSavefile(a, a));
			TS_ASSERT(second.copySavefile(a, a));
			Common::String up = a;
			up.toUppercase();
			TS_ASSERT(second.renameSavefile(a, up));
			TS_ASSERT(second.exists(a));
			Common::InSaveFile *in = second.openForLoading(a);
			TS_ASSERT(in != nullptr);
			if (in) {
				TS_ASSERT(in->readString(0, 200).hasPrefix(targets[0] + "#1"));
				delete in;
			}
			// a rename to another target's name works and leaves the others alone
			const Common::String to = targets[1] + ".009";
			TS_ASSERT(second.renameSavefile(a, to));
			TS_ASSERT(!second.exists(a));
			TS_ASSERT(second.exists(to));
			TS_ASSERT_EQUALS(second.listSavefiles(targets[0] + ".###").size(), 2u);
			TS_ASSERT_EQUALS(second.listSavefiles(targets[1] + ".###").size(), 4u);
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

	// The save manager end to end on a real folder: the engine's long name is written, listed in a NEW session (a new
	// manager over the folder), loaded, found and removed by that same long name - SCI restores by the name it listed.
	void test_longTargetSurvivesASession() {
#if NULL_OSYSTEM_IS_AVAILABLE && defined(DOS_SAVE_MANAGER_TEST)
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
			TS_ASSERT_EQUALS(onDisk[0], DOS::dosPattern("kq1-rgb565.001"));
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
