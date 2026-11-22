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

#ifndef TEST_GRAPHICS_SVF_TEST_UTIL_H
#define TEST_GRAPHICS_SVF_TEST_UTIL_H

#include "common/array.h"
#include "common/memstream.h"
#include "common/str.h"

#include "graphics/hires_text/svfchain.h"

#include "test/graphics/svf_fixture.h"

namespace SvfTest {

// The chain the golden lines are drawn on: { marks, jamo }, both mapped from the fixture.
inline Graphics::SvfChain *makeChain(uint num = 1, uint den = 1, uint8 missing = 0, bool streamJamo = false, uint16 slots = 8) {
	Graphics::SvfChain *chain = new Graphics::SvfChain();
	Graphics::SvfFace *marks = new Graphics::SvfFace();
	Graphics::SvfFace *jamo = new Graphics::SvfFace();
	marks->loadFromMemory(kSvfFixtureMarks, sizeof(kSvfFixtureMarks), "marks");
	if (streamJamo)
		jamo->loadStreamed(new Common::MemoryReadStream(kSvfFixtureJamo, sizeof(kSvfFixtureJamo)), DisposeAfterUse::YES, "jamo", 1024, slots);
	else
		jamo->loadFromMemory(kSvfFixtureJamo, sizeof(kSvfFixtureJamo), "jamo");
	chain->addFace(marks, DisposeAfterUse::YES);
	chain->addFace(jamo, DisposeAfterUse::YES);
	chain->setScale(num, den);
	chain->setMissingAdvance(missing);
	return chain;
}

inline byte hexNibble(char c) {
	return (byte)(c <= '9' ? c - '0' : c - 'a' + 10);
}

// The golden rows (one hex byte a pixel) as w * h bytes.
inline Common::Array<byte> goldenBytes(const char *const *rows, int w, int h) {
	Common::Array<byte> out;
	out.resize(w * h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			out[y * w + x] = (byte)(hexNibble(rows[y][2 * x]) << 4 | hexNibble(rows[y][2 * x + 1]));
	return out;
}

// Rows of '.' (0), '#' (255) and '+' (anything else), for TS_TRACE on a mismatch.
inline Common::String asciiArt(const byte *px, int w, int h) {
	Common::String s;
	for (int y = 0; y < h; ++y) {
		s += '\n';
		for (int x = 0; x < w; ++x)
			s += px[y * w + x] == 0 ? '.' : px[y * w + x] == 255 ? '#' : '+';
	}
	return s;
}

} // End of namespace SvfTest

#endif
