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

#include "graphics/hires_text/jamo_set.h"

#include "common/endian.h"

namespace Graphics {

// JSET header: "JSET", u16 version, then one byte each: lead classes, tail
// classes, initial, medial and final sets, advance.
static const uint32 kHeaderSize = 12;
static const uint16 kVersion = 1;
static const uint kCompat = kJamoCompat;

JamoSetTable::JamoSetTable() : leadClasses(0), tailClasses(0), initSets(0), medSets(0), finSets(0), advance(0) {
	for (int i = 0; i < 19; ++i)
		leadClass[i] = 0;
	for (int i = 0; i < 28; ++i)
		tailClass[i] = 0;
	for (uint i = 0; i < kCompat; ++i)
		compatPiece[i] = 0xFFFF;
}

uint32 jamoSetSize(uint lead, uint tail, uint init, uint med, uint fin) {
	return kHeaderSize + 19 + 28 + 3 * lead * 21 * tail + 2 * (init * 19 + med * 21 + fin * 27 + kCompat);
}

bool JamoSetTable::pieces(uint32 cp, uint16 out[3], int &count) const {
	if (cp < 0xAC00 || cp > 0xD7A3 || !leadClasses)
		return false;
	const uint32 s = cp - 0xAC00;
	const uint lead = s / 588, vowel = (s / 28) % 21, tail = s % 28;
	const uint cell = ((uint)leadClass[lead] * 21 + vowel) * tailClasses + tailClass[tail];
	out[0] = initPiece[(uint)initSet[cell] * 19 + lead];
	out[1] = medPiece[(uint)medSet[cell] * 21 + vowel];
	count = 2;
	// With no final the final set's entry is never looked at.
	if (tail) {
		out[2] = finPiece[(uint)finSet[cell] * 27 + tail - 1];
		count = 3;
	}
	return true;
}

uint16 JamoSetTable::compat(uint32 cp) const {
	if (cp < 0x3131 || cp >= 0x3131 + kCompat)
		return 0xFFFF;
	return compatPiece[cp - 0x3131];
}

bool parseJamoSetTable(const byte *data, uint32 size, JamoSetTable &out, Common::String &error) {
	if (size < kHeaderSize) {
		error = Common::String::format("JSET length %u is shorter than its header", size);
		return false;
	}
	if (READ_BE_UINT32(data) != MKTAG('J', 'S', 'E', 'T')) {
		error = "JSET magic is wrong";
		return false;
	}
	const uint version = READ_LE_UINT16(data + 4);
	if (version != kVersion) {
		error = Common::String::format("JSET version %u is not supported", version);
		return false;
	}
	const uint lc = data[6], tc = data[7], ni = data[8], nm = data[9], nf = data[10];
	if (lc < 1 || lc > kJamoMaxLeadClasses) {
		error = Common::String::format("JSET lead classes %u is not 1..%u", lc, kJamoMaxLeadClasses);
		return false;
	}
	if (tc < 1 || tc > kJamoMaxTailClasses) {
		error = Common::String::format("JSET tail classes %u is not 1..%u", tc, kJamoMaxTailClasses);
		return false;
	}
	if (ni < 1 || ni > kJamoMaxInitSets) {
		error = Common::String::format("JSET initial sets %u is not 1..%u", ni, kJamoMaxInitSets);
		return false;
	}
	if (nm < 1 || nm > kJamoMaxMedSets) {
		error = Common::String::format("JSET medial sets %u is not 1..%u", nm, kJamoMaxMedSets);
		return false;
	}
	if (nf < 1 || nf > kJamoMaxFinSets) {
		error = Common::String::format("JSET final sets %u is not 1..%u", nf, kJamoMaxFinSets);
		return false;
	}
	const uint32 want = jamoSetSize(lc, tc, ni, nm, nf);
	if (size != want) {
		error = Common::String::format("JSET length %u does not match its counts (%u)", size, want);
		return false;
	}

	JamoSetTable t;
	t.leadClasses = (byte)lc;
	t.tailClasses = (byte)tc;
	t.initSets = (byte)ni;
	t.medSets = (byte)nm;
	t.finSets = (byte)nf;
	t.advance = data[11];
	const byte *p = data + kHeaderSize;
	for (int i = 0; i < 19; ++i)
		if ((t.leadClass[i] = p[i]) >= lc) {
			error = Common::String::format("JSET leadClass[%d] = %u is not below %u", i, p[i], lc);
			return false;
		}
	p += 19;
	// Which tail classes a final can be in: the final set matters only there.
	bool tailUsed[kJamoMaxTailClasses] = { false };
	for (int i = 0; i < 28; ++i) {
		if ((t.tailClass[i] = p[i]) >= tc) {
			error = Common::String::format("JSET tailClass[%d] = %u is not below %u", i, p[i], tc);
			return false;
		}
		if (i)
			tailUsed[p[i]] = true;
	}
	p += 28;

	const uint cells = lc * 21 * tc;
	const uint counts[3] = { ni, nm, nf };
	const char *names[3] = { "initSet", "medSet", "finSet" };
	Common::Array<byte> *sets[3] = { &t.initSet, &t.medSet, &t.finSet };
	for (int w = 0; w < 3; ++w) {
		sets[w]->resize(cells);
		for (uint i = 0; i < cells; ++i) {
			(*sets[w])[i] = p[i];
			// A final set is read only for syllables with a final: for the
			// no-final class alone, whatever is there stays unread.
			if (w == 2 && !tailUsed[i % tc])
				continue;
			if (p[i] >= counts[w]) {
				error = Common::String::format("JSET %s[%u] = %u is not below %u", names[w], i, p[i], counts[w]);
				return false;
			}
		}
		p += cells;
	}

	t.initPiece.resize(ni * 19);
	t.medPiece.resize(nm * 21);
	t.finPiece.resize(nf * 27);
	Common::Array<uint16> *pieces[3] = { &t.initPiece, &t.medPiece, &t.finPiece };
	for (int w = 0; w < 3; ++w)
		for (uint i = 0; i < pieces[w]->size(); ++i, p += 2)
			(*pieces[w])[i] = READ_LE_UINT16(p);
	for (uint i = 0; i < kCompat; ++i, p += 2)
		t.compatPiece[i] = READ_LE_UINT16(p);

	out = t;
	return true;
}

} // End of namespace Graphics
