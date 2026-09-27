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


#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/unicode_props.h"

#include "common/algorithm.h"

namespace Graphics {

namespace {

const uint kMaxFirstMissing = 5;

/**
 * One UTF-8 sequence at s[i..len): its code point, and the bytes it takes.
 * Overlong forms, surrogates, values past U+10FFFF and truncated sequences
 * are invalid: false, one byte consumed.
 */
bool decodeUtf8(const byte *s, uint32 len, uint32 i, uint32 &cp, uint32 &used) {
	used = 1;
	const byte b0 = s[i];
	if (b0 < 0x80) {
		cp = b0;
		return true;
	}
	int extra;
	uint32 min;
	if ((b0 & 0xE0) == 0xC0) {
		extra = 1;
		min = 0x80;
		cp = b0 & 0x1F;
	} else if ((b0 & 0xF0) == 0xE0) {
		extra = 2;
		min = 0x800;
		cp = b0 & 0x0F;
	} else if ((b0 & 0xF8) == 0xF0) {
		extra = 3;
		min = 0x10000;
		cp = b0 & 0x07;
	} else {
		return false;
	}
	if (len - i <= (uint32)extra)	// truncated: i < len always
		return false;
	for (int k = 1; k <= extra; k++) {
		const byte b = s[i + k];
		if ((b & 0xC0) != 0x80)
			return false;
		cp = (cp << 6) | (b & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return false;
	used = extra + 1;
	return true;
}

} // End of anonymous namespace

bool CodePointSet::drawn(uint32 cp) {
	if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
		return false;
	if (cp == 0xFEFF || cp > 0x10FFFF)
		return false;
	return true;
}

void CodePointSet::add(uint32 cp) {
	if (!drawn(cp))
		return;
	// Binary search for the insertion point; the set stays ascending.
	uint lo = 0, hi = _cps.size();
	while (lo < hi) {
		const uint mid = (lo + hi) / 2;
		if (_cps[mid] < cp)
			lo = mid + 1;
		else
			hi = mid;
	}
	if (lo < _cps.size() && _cps[lo] == cp)
		return;
	_cps.insert_at(lo, cp);
}

bool CodePointSet::contains(uint32 cp) const {
	uint lo = 0, hi = _cps.size();
	while (lo < hi) {
		const uint mid = (lo + hi) / 2;
		if (_cps[mid] < cp)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo < _cps.size() && _cps[lo] == cp;
}

void CodePointSet::addUtf8(const char *s, uint32 len) {
	if (!s)
		return;
	const byte *b = (const byte *)s;
	uint32 i = 0;
	while (i < len) {
		uint32 cp, used;
		if (decodeUtf8(b, len, i, cp, used))
			add(cp);
		i += used;
	}
}

void CodePointSet::addU32(const Common::U32String &s) {
	for (uint i = 0; i < s.size(); i++)
		add((uint32)s[i]);
}

void CodePointSet::sample(uint n, Common::Array<uint32> &out) const {
	out.clear();
	if (n == 0 || _cps.empty())
		return;

	// The set is ascending, so the ASCII code points are a prefix of it.
	uint asciiCount = 0;
	while (asciiCount < _cps.size() && _cps[asciiCount] < 0x80)
		asciiCount++;
	const uint total = _cps.size();

	Common::Array<bool> taken;
	taken.resize(total);
	for (uint i = 0; i < total; i++)
		taken[i] = false;
	uint count = 0;
	// Every index is tried once per pass; take() refuses once n are taken.
	struct Picker {
		Common::Array<bool> &taken;
		uint &count;
		uint n;
		void take(uint idx) {
			if (count < n && !taken[idx]) {
				taken[idx] = true;
				count++;
			}
		}
	} pick = { taken, count, n };

	// 1. The first code point of each 128-block. ASCII keeps one place; the
	// non-ASCII blocks share the rest. When there are more blocks than
	// places (Japanese kanji span ~160 blocks), evenly spaced blocks are
	// taken, always including the first and the last, so the sample is not
	// just the lowest blocks.
	Common::Array<uint> blockFirsts;	///< index of the first cp of each non-ASCII block
	uint32 prevBlock = 0xFFFFFFFF;
	for (uint i = asciiCount; i < total; i++) {
		const uint32 block = _cps[i] >> 7;
		if (block != prevBlock)
			blockFirsts.push_back(i);
		prevBlock = block;
	}
	if (asciiCount > 0)
		pick.take(0);
	const uint places = n - count;
	const uint blocks = blockFirsts.size();
	if (blocks <= places) {
		for (uint b = 0; b < blocks; b++)
			pick.take(blockFirsts[b]);
	} else if (places == 1) {
		pick.take(blockFirsts[0]);
	} else if (places > 1) {
		for (uint k = 0; k < places; k++)
			pick.take(blockFirsts[(uint)((uint64)k * (blocks - 1) / (places - 1))]);
	}

	// 2. Every size/n-th, non-ASCII then ASCII.
	const uint nonAscii = total - asciiCount;
	if (nonAscii > 0) {
		const uint step = MAX<uint>(1, nonAscii / n);
		for (uint i = asciiCount; i < total; i += step)
			pick.take(i);
	}
	if (asciiCount > 0) {
		const uint step = MAX<uint>(1, asciiCount / n);
		for (uint i = 0; i < asciiCount; i += step)
			pick.take(i);
	}

	// 3. The rest in order, non-ASCII first.
	for (uint i = asciiCount; i < total && count < n; i++)
		pick.take(i);
	for (uint i = 0; i < asciiCount && count < n; i++)
		pick.take(i);

	for (uint i = asciiCount; i < total; i++) {
		if (taken[i])
			out.push_back(_cps[i]);
	}
	for (uint i = 0; i < asciiCount; i++) {
		if (taken[i])
			out.push_back(_cps[i]);
	}
}

bool isFitMark(uint32 cp) {
	return Unicode::isCombining(cp) || cp == 0x0E33 || cp == 0x0EB3;
}

bool isStackingFitMark(uint32 cp) {
	return cp == 0x0E31 || (cp >= 0x0E33 && cp <= 0x0E3A) || (cp >= 0x0E47 && cp <= 0x0E4E) ||
		   cp == 0x0EB1 || (cp >= 0x0EB3 && cp <= 0x0EBC) || (cp >= 0x0EC8 && cp <= 0x0ECE);
}

void CodePointSet::fitProbes(uint n, Common::Array<uint32> &out) const {
	out.clear();
	for (uint i = 0; i < _cps.size() && out.size() < n; i++) {
		if (isFitMark(_cps[i]))
			out.push_back(_cps[i]);
	}
	if (out.empty()) {
		sample(n, out);
		return;
	}
	Common::Array<uint32> picks;
	sample(n, picks);
	for (uint i = 0; i < picks.size() && out.size() < n; i++) {
		if (!isFitMark(picks[i]))
			out.push_back(picks[i]);
	}
}

CoverageReport checkCoverage(UnicodeGlyphSource *src, const Common::Array<uint32> &sample) {
	CoverageReport r;
	if (!src)
		return r;
	Common::Array<uint32> missing;
	for (uint i = 0; i < sample.size(); i++) {
		const uint32 cp = sample[i];
		r.sampled++;
		GlyphMetrics m;
		if (!src->metrics(cp, m)) {
			missing.push_back(cp);
			continue;
		}
		if ((m.combining || Unicode::isCombining(cp)) && (m.advance != 0 || src->advance(cp) != 0))
			r.spacingMarks++;
	}
	r.missing = missing.size();
	Common::sort(missing.begin(), missing.end());
	for (uint i = 0; i < missing.size() && r.firstMissing.size() < kMaxFirstMissing; i++) {
		if (r.firstMissing.empty() || r.firstMissing.back() != missing[i])
			r.firstMissing.push_back(missing[i]);
	}
	return r;
}

Common::String coverageWarning(const Common::String &faceName, const CoverageReport &r,
							   const Common::String &fallbackName) {
	Common::String text;
	if (r.missing > 0) {
		Common::String list;
		for (uint i = 0; i < r.firstMissing.size(); i++) {
			if (i)
				list += ' ';
			list += Common::String::format("U+%04X", r.firstMissing[i]);
		}
		if (r.missing > r.firstMissing.size())
			list += " ...";
		text = Common::String::format("hires text: %s lacks %u of %u sampled characters of the translation "
									  "(%s); they fall back to %s", faceName.c_str(), r.missing, r.sampled,
									  list.c_str(), fallbackName.c_str());
	}
	if (r.spacingMarks > 0) {
		if (!text.empty())
			text += '\n';
		text += Common::String::format("hires text: %s draws combining marks as spacing glyphs (it needs "
									   "shaping); choose a face with zero-width marks, e.g. Sukhumvit Set",
									   faceName.c_str());
	}
	return text;
}

} // End of namespace Graphics
