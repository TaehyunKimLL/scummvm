/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/ptr.h"
#include "common/stream.h"

#include "graphics/hires_text/hires_files.h"

#include "test/system/null_osystem.h"

class HiResTextShippedMapsTestSuite : public CxxTest::TestSuite {
	// test/graphics/<this file> -> the source tree
	static Common::Path tree() {
		return Common::Path(__FILE__, '/').getParent().getParent().getParent();
	}

	// Every .map in @p dir loads without a warning for every qualifier set; returns how many there were.
	int checkDir(const Common::Path &dir) {
		Common::FSList files;
		if (!Common::FSNode(dir).getChildren(files, Common::FSNode::kListFilesOnly))
			return 0;
		int n = 0;
		const char *engines[] = { "", "sci", "scumm", "agi" };
		const Graphics::HiResRenderTarget targets[] = { Graphics::kHiResTargetAuto, Graphics::kHiResTargetClut8,
		                                                Graphics::kHiResTargetRgb565 };
		for (uint i = 0; i < files.size(); ++i) {
			const Common::String name = files[i].getName();
			if (!name.hasSuffixIgnoreCase(".map"))
				continue;
			++n;
			for (int e = 0; e < 4; ++e) {
				for (int t = 0; t < 3; ++t) {
					Common::ScopedPtr<Common::SeekableReadStream> s(files[i].createReadStream());
					Graphics::HiResMapQualifiers q;
					q.engine = engines[e];
					q.target = targets[t];
					Graphics::HiResMap m;
					TSM_ASSERT(name.c_str(), s && Graphics::HiResFontMap::load(*s, name, dir, q, m));
					for (uint w = 0; w < m.warnings.size(); ++w)
						TS_FAIL(m.warnings[w].c_str());
				}
			}
		}
		return n;
	}

public:
	void setUp() override {
		Common::install_null_g_system();
	}

	void tearDown() override {
		Common::uninstall_null_g_system();
	}

	void test_shippedMapsAreVersion3AndClean() {
		TS_ASSERT(checkDir(tree().join("dists/engine-data/hires_text/maps")) >= 1);
	}

	// Developer aid: maps converted for the packs (harness), when a directory or link hires-maps is in the working directory.
	void test_packMapsWhenGiven() {
		if (Common::FSNode(Common::Path("hires-maps")).isDirectory())
			TS_ASSERT(checkDir(Common::Path("hires-maps")) >= 1);
	}

	void test_diskFiles() {
		Graphics::HiResDiskFiles files;
		const Common::Path data = tree().join("dists/engine-data");
		files.addDataRoot(data);
		TS_ASSERT_EQUALS(files.find(data, "HIRES_TEXT/maps/KOREAN-DEFAULT.MAP"), data.join("hires_text/maps/korean-default.map"));
		TS_ASSERT(files.find(data, "hires_text/maps/none.map").empty());
		const Graphics::HiResFaceRef ref = Graphics::HiResFontMap::resolveFace("data:hires_text/NEODG16A.SVF", Common::Path());
		TS_ASSERT_EQUALS(files.locate(ref), data.join("hires_text/NEODG16A.SVF"));
		TS_ASSERT(files.locate(Graphics::HiResFontMap::resolveFace("data:../engine-data/hires_text/NEODG16A.SVF", Common::Path())).empty());
		TS_ASSERT(!Graphics::HiResDiskFiles::isSafeDataPath(Common::Path("/etc/passwd")));
		TS_ASSERT(Graphics::HiResDiskFiles::isSafeDataPath(Common::Path("hires_text/X.SVF")));
		TS_ASSERT(!Graphics::HiResDiskFiles::isSafeDataPath(Common::Path("hires_text\\..\\..\\X.SVF", '/')));
		TS_ASSERT(!Graphics::HiResDiskFiles::isSafeDataPath(Common::Path("hires_text/C:X.SVF", '/')));
	}
};
