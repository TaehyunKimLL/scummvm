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

#include "graphics/hires_text/unicode_ranges.h"

#include "common/algorithm.h"
#include "common/util.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

namespace {

const uint32 kMaxCodePoint = 0x10FFFF;
const uint32 kCodeSpaceEnd = 0x110000;

struct BlockEntry {
	const char *name;
	uint32 lo, hi;
};

// The fixed block table of design section 6.1 (20 rows).
const BlockEntry kBlocks[] = {
	{ "basic-latin",          0x0020, 0x007E },
	{ "latin-1",              0x00A0, 0x00FF },
	{ "latin-ext-a",          0x0100, 0x017F },
	{ "latin-ext-b",          0x0180, 0x024F },
	{ "thai",                 0x0E00, 0x0E7F },
	{ "hangul-jamo",          0x1100, 0x11FF },
	{ "general-punctuation",  0x2000, 0x206F },
	{ "letterlike",           0x2100, 0x214F },
	{ "arrows",               0x2190, 0x21FF },
	{ "geometric-shapes",     0x25A0, 0x25FF },
	{ "cjk-symbols",          0x3000, 0x303F },
	{ "hiragana",             0x3040, 0x309F },
	{ "katakana",             0x30A0, 0x30FF },
	{ "hangul-compat-jamo",   0x3130, 0x318F },
	{ "cjk-unified",          0x4E00, 0x9FFF },
	{ "hangul-syllables",     0xAC00, 0xD7A3 },
	{ "pua",                  0xE000, 0xF8FF },
	{ "fullwidth-forms",      0xFF00, 0xFFEF },
	{ "box-drawing",          0x2500, 0x257F },
	{ "misc-symbols",         0x2600, 0x26FF },
};

/** 1-6 hex digits, no prefix. */
bool parseHexDigits(const Common::String &s, uint32 &out) {
	if (s.empty() || s.size() > 6)
		return false;
	uint32 n = 0;
	for (uint i = 0; i < s.size(); ++i) {
		const char c = s[i];
		int digit;
		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if (c >= 'a' && c <= 'f')
			digit = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			digit = c - 'A' + 10;
		else
			return false;
		n = (n << 4) | (uint32)digit;
	}
	out = n;
	return true;
}

} // End of anonymous namespace

bool lookupUnicodeBlock(const Common::String &name, HiResSpan &out) {
	for (uint i = 0; i < ARRAYSIZE(kBlocks); ++i) {
		if (name.equalsIgnoreCase(kBlocks[i].name)) {
			out.lo = kBlocks[i].lo;
			out.hi = kBlocks[i].hi;
			return true;
		}
	}
	return false;
}

bool parseRangeSpec(const Common::String &text, HiResRangeSpec &out, Common::String &error) {
	error.clear();

	if (text.equalsIgnoreCase("wide")) {
		out.kind = kHiResSpecWide;
		out.span.lo = 0;
		out.span.hi = 0;
		out.spelled = text;
		return true;
	}

	HiResSpan block;
	if (lookupUnicodeBlock(text, block)) {
		out.kind = kHiResSpecSpan;
		out.span = block;
		out.spelled = text;
		return true;
	}

	if (!text.hasPrefixIgnoreCase("u+")) {
		error = Common::String::format("unknown range '%s'", text.c_str());
		return false;
	}

	// Past the "u+"/"U+" of the low end.
	const Common::String rest(text.c_str() + 2);
	const size_t dash = rest.findFirstOf('-');
	const Common::String loText = (dash == Common::String::npos) ? rest : Common::String(rest.c_str(), dash);

	uint32 lo, hi;
	if (!parseHexDigits(loText, lo)) {
		error = Common::String::format("malformed range '%s'", text.c_str());
		return false;
	}

	if (dash == Common::String::npos) {
		hi = lo;
	} else {
		Common::String hiText(rest.c_str() + dash + 1);
		if (hiText.hasPrefixIgnoreCase("u+"))
			hiText = Common::String(hiText.c_str() + 2);
		// A second '-' (e.g. "U+1-2-3") is not this grammar at all.
		if (hiText.findFirstOf('-') != Common::String::npos || !parseHexDigits(hiText, hi)) {
			error = Common::String::format("malformed range '%s'", text.c_str());
			return false;
		}
	}

	if (lo > hi || hi > kMaxCodePoint) {
		error = Common::String::format("malformed range '%s'", text.c_str());
		return false;
	}

	out.kind = kHiResSpecSpan;
	out.span.lo = lo;
	out.span.hi = hi;
	out.spelled = text;
	return true;
}

HiResRangeTable::HiResRangeTable() {
	for (uint i = 0; i < ARRAYSIZE(_page); ++i)
		_page[i] = 0xFFFF;
}

namespace {

/** Does @p span fully contain the elementary interval [lo, hi]? Boundaries
 *  are split at every span edge, so an interval is never partly inside a
 *  span: checking one endpoint would do, but both is just as cheap and
 *  documents the invariant. */
inline bool spanCovers(const HiResSpan &span, uint32 lo, uint32 hi) {
	return span.lo <= lo && hi <= span.hi;
}

/** The narrowest span of @p scope covering [lo, hi], if any. Ties (equal
 *  width) keep the first entry in the scope's order (section 6.2.1's
 *  "later line is ignored" is enforced by the loader before this point, so
 *  in practice ties do not occur from a map; here it just needs a rule). */
bool narrowestCovering(const HiResRangeScope &scope, uint32 lo, uint32 hi, int &value) {
	bool found = false;
	uint32 bestWidth = 0;
	for (uint i = 0; i < scope.specs.size(); ++i) {
		const HiResRangeSpec &s = scope.specs[i];
		if (s.kind != kHiResSpecSpan || !spanCovers(s.span, lo, hi))
			continue;
		const uint32 width = s.span.hi - s.span.lo;
		if (!found || width < bestWidth) {
			found = true;
			bestWidth = width;
			value = scope.values[i];
		}
	}
	return found;
}

/** The scope's own `wide` spec, if it has one (first, if more than one). */
bool scopeWideValue(const HiResRangeScope &scope, int &value) {
	for (uint i = 0; i < scope.specs.size(); ++i) {
		if (scope.specs[i].kind == kHiResSpecWide) {
			value = scope.values[i];
			return true;
		}
	}
	return false;
}

} // End of anonymous namespace

void HiResRangeTable::compile(const Common::Array<HiResRangeScope> &scopes) {
	_runs.clear();
	for (uint i = 0; i < ARRAYSIZE(_page); ++i)
		_page[i] = 0xFFFF;

	if (scopes.empty())
		return;

	// Every span's edges, plus the ends of the whole code space.
	Common::Array<uint32> bounds;
	bounds.push_back(0);
	bounds.push_back(kCodeSpaceEnd);
	for (uint s = 0; s < scopes.size(); ++s) {
		const HiResRangeScope &scope = scopes[s];
		for (uint i = 0; i < scope.specs.size(); ++i) {
			if (scope.specs[i].kind != kHiResSpecSpan)
				continue;
			bounds.push_back(scope.specs[i].span.lo);
			bounds.push_back(scope.specs[i].span.hi + 1);
		}
	}
	Common::sort(bounds.begin(), bounds.end());
	Common::Array<uint32> uniqueBounds;
	for (uint i = 0; i < bounds.size(); ++i) {
		if (uniqueBounds.empty() || uniqueBounds.back() != bounds[i])
			uniqueBounds.push_back(bounds[i]);
	}

	for (uint i = 0; i + 1 < uniqueBounds.size(); ++i) {
		const uint32 lo = uniqueBounds[i];
		const uint32 hi = uniqueBounds[i + 1] - 1;

		int narrowValue = -1, wideValue = -1;
		bool narrowFound = false, wideFound = false;
		for (uint s = 0; s < scopes.size() && !(narrowFound && wideFound); ++s) {
			const HiResRangeScope &scope = scopes[s];
			int covering;
			if (narrowestCovering(scope, lo, hi, covering)) {
				if (!narrowFound) {
					narrowValue = covering;
					narrowFound = true;
				}
				if (!wideFound) {
					wideValue = covering;
					wideFound = true;
				}
				continue;
			}
			if (!wideFound) {
				int w;
				if (scopeWideValue(scope, w)) {
					wideValue = w;
					wideFound = true;
				}
			}
		}

		if (!_runs.empty() && _runs.back().narrow == narrowValue && _runs.back().wide == wideValue) {
			_runs.back().hi = hi;
		} else {
			Run run;
			run.lo = lo;
			run.hi = hi;
			run.narrow = narrowValue;
			run.wide = wideValue;
			_runs.push_back(run);
		}
	}

	// Page cache: for each BMP page (cp >> 8 in 0..255), either the single
	// run that covers the whole page, or "search" (binary search at lookup
	// time). Code points above the BMP always search.
	uint32 runIdx = 0;
	for (uint page = 0; page < 256; ++page) {
		const uint32 pageLo = page << 8;
		const uint32 pageHi = pageLo + 0xFF;
		while (runIdx < _runs.size() && _runs[runIdx].hi < pageLo)
			++runIdx;
		if (runIdx < _runs.size() && _runs[runIdx].lo <= pageLo && _runs[runIdx].hi >= pageHi)
			_page[page] = (uint16)runIdx;
		else
			_page[page] = 0xFFFF;
	}
}

int HiResRangeTable::findRun(uint32 cp) const {
	uint l = 0, r = _runs.size();
	while (l < r) {
		const uint m = (l + r) / 2;
		if (cp < _runs[m].lo)
			r = m;
		else if (cp > _runs[m].hi)
			l = m + 1;
		else
			return (int)m;
	}
	return -1;
}

int HiResRangeTable::lookup(uint32 cp) const {
	if (_runs.empty())
		return -1;

	int idx;
	if (cp <= 0xFFFF && _page[cp >> 8] != 0xFFFF)
		idx = _page[cp >> 8];
	else
		idx = findRun(cp);

	if (idx < 0)
		return -1;

	const Run &run = _runs[(uint)idx];
	return Unicode::isWide(cp) ? run.wide : run.narrow;
}

bool HiResRangeTable::empty() const {
	return _runs.empty();
}

uint32 HiResRangeTable::hash() const {
	uint32 h = 2166136261u;
	for (uint i = 0; i < _runs.size(); ++i) {
		const uint32 words[4] = { _runs[i].lo, _runs[i].hi, (uint32)_runs[i].narrow, (uint32)_runs[i].wide };
		for (uint w = 0; w < 4; ++w) {
			for (uint b = 0; b < 4; ++b) {
				h ^= (words[w] >> (b * 8)) & 0xFF;
				h *= 16777619u;
			}
		}
	}
	return h;
}

} // End of namespace Graphics
