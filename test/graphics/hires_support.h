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

#ifndef TEST_GRAPHICS_HIRES_SUPPORT_H
#define TEST_GRAPHICS_HIRES_SUPPORT_H

#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/str.h"

#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/text_plane.h"

#include "test/graphics/svf_fixture.h"

namespace HiResTest {

// Files held in memory, by path ("game/ko/HIRESVF.MAP"); names matched ignoring case.
class MemoryFiles : public Graphics::HiResFiles {
public:
	MemoryFiles() : opens(0) {
		addBytes("data/fixture/MARKS.SVF", kSvfFixtureMarks, sizeof(kSvfFixtureMarks));
		addBytes("data/fixture/JAMO.SVF", kSvfFixtureJamo, sizeof(kSvfFixtureJamo));
	}

	void addText(const char *path, const char *text) {
		addBytes(path, (const byte *)text, strlen(text));
	}
	void addBytes(const char *path, const byte *data, uint32 size) {
		Entry e;
		e.data = data;
		e.size = size;
		_files[Common::String(path)] = e;
	}

	Common::Path find(const Common::Path &dir, const Common::String &name) override {
		const Common::String want = dir.join(Common::Path(name, '/')).toString('/');
		for (Map::const_iterator it = _files.begin(); it != _files.end(); ++it)
			if (it->_key.equalsIgnoreCase(want))
				return Common::Path(it->_key, '/');
		return Common::Path();
	}
	Common::Path locate(const Graphics::HiResFaceRef &ref) override {
		const Common::Path p = ref.data ? Common::Path("data").join(ref.path) : ref.path;
		return _files.contains(p.toString('/')) ? p : Common::Path();
	}
	Common::SeekableReadStream *open(const Common::Path &path) override {
		Map::const_iterator it = _files.find(path.toString('/'));
		if (it == _files.end())
			return nullptr;
		++opens;
		return new Common::MemoryReadStream(it->_value.data, it->_value.size);
	}

	int opens;

private:
	struct Entry {
		const byte *data;
		uint32 size;
	};
	typedef Common::HashMap<Common::String, Entry> Map;
	Map _files;
};

// The coverage plane of @p plane as w x h bytes (for the golden rows).
inline Common::Array<byte> coverageOf(const Graphics::TextPlane &plane) {
	Common::Array<byte> out;
	out.resize(plane.getWidth() * plane.getHeight());
	for (int y = 0; y < plane.getHeight(); ++y)
		for (int x = 0; x < plane.getWidth(); ++x)
			out[y * plane.getWidth() + x] = plane.getCoverage(x, y);
	return out;
}

} // End of namespace HiResTest

#endif
