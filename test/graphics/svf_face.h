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

#include "common/memstream.h"

#include "graphics/hires_text/svfface.h"

#include "test/graphics/svf_fixture.h"

namespace {

// A memory stream whose reads fail once fail is set.
class SvfFlakyStream : public Common::MemoryReadStream {
public:
	SvfFlakyStream(const byte *data, uint32 size) : Common::MemoryReadStream(data, size), fail(false) {}
	uint32 read(void *dataPtr, uint32 dataSize) override {
		return fail ? 0 : Common::MemoryReadStream::read(dataPtr, dataSize);
	}
	bool fail;
};

} // End of anonymous namespace

class SvfFaceTestSuite : public CxxTest::TestSuite {
public:
	void test_abi() {
		TS_ASSERT_EQUALS(scummvm_svf_face_size, sizeof(svf_face));
		TS_ASSERT_EQUALS(SVF_JMAP, 1);
		TS_ASSERT_EQUALS(SVF_JSET_BIG, 1);
		TS_ASSERT_EQUALS(SVF_JMAP_RECOG, 0);
	}

	void test_jmapFaceOpensClean() {
		Graphics::SvfFace face;
		TS_ASSERT(face.loadFromMemory(kSvfFixtureJamo, sizeof(kSvfFixtureJamo), "jamo"));
		TS_ASSERT_EQUALS(face.getError(), SVF_OK);
		TS_ASSERT_EQUALS(face.getStatus(), 0);
		TS_ASSERT(face.isJamo());
		TS_ASSERT(face.has(0xAC00));	// listed by the JMAP block
		TS_ASSERT(face.has(0xD55C));	// answered by the fallback JSET
		TS_ASSERT(face.has('a'));
		TS_ASSERT(!face.has(0x2603));
		TS_ASSERT_EQUALS(face.getCellWidth(), 16u);
		TS_ASSERT_EQUALS(face.getCellHeight(), 16u);
		TS_ASSERT_EQUALS(face.getAscent(), 12u);
		TS_ASSERT_EQUALS(face.getBpp(), 1u);
		TS_ASSERT_EQUALS(face.getCellBytes(), 32u);
		TS_ASSERT(!face.isStreamed());
	}

	void test_marksFace() {
		Graphics::SvfFace face;
		TS_ASSERT(face.loadFromMemory(kSvfFixtureMarks, sizeof(kSvfFixtureMarks), "marks"));
		TS_ASSERT_EQUALS(face.getStatus(), 0);
		TS_ASSERT(!face.isJamo());
		TS_ASSERT(face.has(0x0301));
		TS_ASSERT(face.has(0xFFFD));
	}

	void test_refusals() {
		Graphics::SvfFace face;
		TS_ASSERT(!face.loadFromMemory(kSvfFixtureJamo, 100, "cut"));
		TS_ASSERT_DIFFERS(face.getError(), SVF_OK);
		TS_ASSERT(!face.isOpen());

		byte v2[sizeof(kSvfFixtureMarks)];
		memcpy(v2, kSvfFixtureMarks, sizeof(v2));
		v2[4] = 2;
		TS_ASSERT(!face.loadFromMemory(v2, sizeof(v2), "v2"));
		TS_ASSERT_EQUALS(face.getError(), SVF_E_VERSION);
	}

	void test_loadWhole() {
		Common::MemoryReadStream stream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo));
		Graphics::SvfFace face;
		TS_ASSERT(face.loadWhole(stream, "jamo"));
		TS_ASSERT_EQUALS(face.getStatus(), 0);
		TS_ASSERT(!face.isStreamed());
	}

	void test_loadStreamed() {
		Graphics::SvfFace face;
		TS_ASSERT(face.loadStreamed(new Common::MemoryReadStream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo)),
		                            DisposeAfterUse::YES, "jamo", 1024, 8));
		TS_ASSERT(face.isStreamed());
		TS_ASSERT_EQUALS(face.getStatus(), 0);
		TS_ASSERT(face.isJamo());
		TS_ASSERT(face.getStreamReads() > 0);
	}

	void test_loadPicksMappedOrStreamed() {
		Graphics::SvfFace small, big;
		TS_ASSERT(small.load(new Common::MemoryReadStream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo)), DisposeAfterUse::YES, "a"));
		TS_ASSERT(!small.isStreamed());
		TS_ASSERT(big.load(new Common::MemoryReadStream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo)), DisposeAfterUse::YES, "b", 4096));
		TS_ASSERT(big.isStreamed());
	}

	void test_streamReadFailure() {
		SvfFlakyStream stream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo));
		Graphics::SvfFace face;
		TS_ASSERT(face.loadStreamed(&stream, DisposeAfterUse::NO, "flaky", 1024, 0));
		stream.fail = true;
		byte cell[64];
		TS_ASSERT_EQUALS(svf_glyph(face.raw(), 'a', cell, sizeof(cell)), SVF_E_IO);
	}
};
