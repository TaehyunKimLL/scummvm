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

#include "graphics/hires_text/font_set.h"

#include "common/algorithm.h"
#include "common/ptr.h"
#include "common/stream.h"
#include "common/util.h"

#include "graphics/fonts/svffont.h"
#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/text_plane.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

HiResFaceCache::HiResFaceCache(HiResFiles &files, uint32 wholeLimit) : _files(files), _wholeLimit(wholeLimit) {
}

HiResFaceCache::~HiResFaceCache() {
	for (Common::HashMap<Common::String, SvfFace *>::iterator it = _faces.begin(); it != _faces.end(); ++it)
		delete it->_value;
}

SvfFace *HiResFaceCache::get(const HiResFaceRef &ref, HiResLoadMode mode, Common::Array<Common::String> &warnings) {
	const Common::String key = ref.key();
	if (_faces.contains(key))
		return _faces[key];
	SvfFace *face = nullptr;
	const Common::Path path = _files.locate(ref);
	Common::SeekableReadStream *stream = path.empty() ? nullptr : _files.open(path);
	if (!stream) {
		warnings.push_back(Common::String::format("face %s (%s): not found", ref.name.c_str(), key.c_str()));
	} else {
		face = new SvfFace();
		bool ok;
		if (mode == kHiResLoadWhole) {
			ok = face->loadWhole(*stream, ref.name);
			delete stream;
		} else if (mode == kHiResLoadStream) {
			ok = face->loadStreamed(stream, DisposeAfterUse::YES, ref.name);
		} else {
			ok = face->load(stream, DisposeAfterUse::YES, ref.name, _wholeLimit);
		}
		if (!ok) {
			warnings.push_back(Common::String::format("face %s (%s): not an SVF v3 face (error %d)", ref.name.c_str(),
			                                          key.c_str(), (int)face->getError()));
			delete face;
			face = nullptr;
		} else if (face->getStatus()) {
			warnings.push_back(Common::String::format("face %s (%s): status 0x%x, some glyphs may not draw", ref.name.c_str(),
			                                          key.c_str(), face->getStatus()));
		}
	}
	_faces[key] = face;
	return face;
}

void HiResFaceCache::warm(SvfFace *face, const HiResFaceRef &ref, const HiResMap &map, Common::Array<Common::String> &warnings) {
	// [preload] lists the characters a game draws with a streamed face: reading their glyphs once at load fills the block
	// cache, so the first dialogue line does not seek the font file glyph by glyph (DESIGN 5.2, 8).
	if (!face || !face->isStreamed() || _warmed.contains(ref.key()) || !map.preloads.contains(ref.name))
		return;
	_warmed[ref.key()] = true;
	const HiResFaceRef &list = map.preloads[ref.name];
	const Common::Path path = _files.locate(list);
	Common::ScopedPtr<Common::SeekableReadStream> s(path.empty() ? nullptr : _files.open(path));
	if (!s) {
		warnings.push_back(Common::String::format("preload %s (%s): not found", list.name.c_str(), list.key().c_str()));
		return;
	}
	Common::Array<byte> cell(face->getCellBytes() * 2);	// a composed syllable of a streamed face reads pieces into the second half
	while (!s->eos() && !s->err()) {
		const Common::String line = s->readLine();
		if (line.hasPrefix("#"))
			continue;
		for (const char *p = line.c_str(), *end = p + line.size(); p < end;) {
			uint32 cp;
			bool invalid;
			p += decodeUtf8(p, end, cp, invalid);
			if (!invalid && face->has(cp))
				svf_glyph(face->raw(), cp, cell.data(), cell.size());
		}
	}
}

HiResFontSet::HiResFontSet() {
}

HiResFontSet::~HiResFontSet() {
	clearChains();
}

void HiResFontSet::clearChains() {
	for (uint i = 0; i < _chains.size(); ++i)
		if (_dispose[i] == DisposeAfterUse::YES)
			delete _chains[i];
	_chains.clear();
	_original.clear();
	_dispose.clear();
}

void HiResFontSet::addChain(SvfChain *chain, bool original, DisposeAfterUse::Flag dispose) {
	_chains.push_back(chain);
	_original.push_back(original);
	_dispose.push_back(dispose);
}

bool HiResFontSet::build(const HiResIdPlan &plan, const HiResMap &map, HiResFaceCache &faces,
                         Common::Array<Common::String> &warnings) {
	clearChains();
	_plan = plan;
	for (uint c = 0; c < plan.chains.size(); ++c) {
		SvfChain *chain = new SvfChain();
		int tallest = 0;
		for (uint f = 0; f < plan.chains[c].faces.size(); ++f) {
			const HiResFaceRef &ref = plan.chains[c].faces[f];
			SvfFace *face = faces.get(ref, map.loadModeFor(ref), warnings);
			faces.warm(face, ref, map, warnings);
			if (face && chain->addFace(face))
				tallest = MAX<int>(tallest, face->getCellHeight());
		}
		if (plan.size && tallest && plan.size != tallest) {
			const int g = Common::gcd(plan.size, tallest);
			if (!chain->setScale(plan.size / g, tallest / g))
				warnings.push_back(Common::String::format("font %d: size=%d cannot scale %d px cells (num and den 1..16)",
				                                          plan.id, plan.size, tallest));
		}
		if (chain->getFaceCount())
			chain->setSyllableCache(kSyllableCacheSlots);
		addChain(chain, plan.chains[c].original, DisposeAfterUse::YES);
	}
	return !_chains.empty() && (_chains[0]->getFaceCount() || _original[0]);
}

int HiResFontSet::getAscent() const {
	int a = 0;
	for (uint i = 0; i < _chains.size(); ++i)
		if (_chains[i]->getFaceCount())
			a = MAX(a, _chains[i]->getAscent());
	return a;
}

int HiResFontSet::getDescent() const {
	int d = 0;
	for (uint i = 0; i < _chains.size(); ++i)
		if (_chains[i]->getFaceCount())
			d = MAX(d, _chains[i]->getDescent());
	return d;
}

int HiResFontSet::route(uint32 cp) const {
	if (_chains.empty())
		return -1;
	uint c = _plan.chainFor(cp);
	if (c >= _chains.size())
		c = 0;
	const SvfChain *chain = _chains[c];
	if (chain->getFaceCount() && chain->has(cp))
		return (int)c;
	if (_original[c])
		return -1;
	return chain->getFaceCount() ? (int)c : -1;
}

bool HiResFontSet::hasReplacement() const {
	const int c = route(0xFFFD);
	return c >= 0 && _chains[c]->has(0xFFFD);
}

uint32 HiResFontSet::substitute(uint32 cp, int chain) const {
	const SvfChain *c = _chains[chain];
	if (!c->has(cp) && _plan.missing && c->has(_plan.missing))
		return _plan.missing;
	return cp;
}

int HiResFontSet::getAdvance(uint32 cp, const HiResOriginalFont *original) const {
	const int c = route(cp);
	if (c < 0)
		return original ? original->getAdvance(cp) : 0;
	switch (_plan.advanceFor(cp)) {
	case kHiResAdvanceGame:
		if (original)
			return original->getAdvance(cp);
		break;
	case kHiResAdvanceCell:
		return Unicode::isCombining(cp) ? 0 : _chains[c]->getMaxCellWidth();
	default:
		break;
	}
	char utf8[4];
	const uint n = SvfFont::encodeUtf8(substitute(cp, c), utf8);
	return _chains[c]->getWidth(utf8, n);
}

namespace {

struct PlotTarget {
	TextPlane *plane;
	byte index;
};

void plotToPlane(void *ctx, int16_t x, int16_t y, uint8_t coverage) {
	PlotTarget *t = (PlotTarget *)ctx;
	t->plane->plot(x, y, coverage, t->index);
}

} // End of anonymous namespace

int HiResFontSet::drawChar(uint32 cp, int x, int yBaseline, TextPlane &plane, byte index, const Common::Rect *clip,
                           const HiResOriginalFont *original) const {
	const int c = route(cp);
	if (c < 0) {
		if (original)
			original->drawChar(cp, x, yBaseline);
	} else {
		char utf8[4];
		const uint n = SvfFont::encodeUtf8(substitute(cp, c), utf8);
		PlotTarget t = { &plane, index };
		_chains[c]->drawCoverage(utf8, n, x, yBaseline + _plan.shift, plane.getWidth(), plane.getHeight(), plotToPlane, &t, clip);
	}
	return getAdvance(cp, original);
}

int decodeUtf8(const char *text, const char *end, uint32 &cp, bool &invalid) {
	const byte *p = (const byte *)text;
	const byte b = p[0];
	invalid = false;
	int n;
	uint32 min;
	if (b < 0x80) {
		cp = b;
		return 1;
	} else if (b >= 0xC2 && b <= 0xDF) {
		n = 2;
		cp = b & 0x1F;
		min = 0x80;
	} else if (b >= 0xE0 && b <= 0xEF) {
		n = 3;
		cp = b & 0x0F;
		min = 0x800;
	} else if (b >= 0xF0 && b <= 0xF4) {
		n = 4;
		cp = b & 0x07;
		min = 0x10000;
	} else {
		n = 0;
		min = 0;
	}
	if (n == 0 || end - text < n) {
		invalid = true;
		cp = 0xFFFD;
		return 1;
	}
	for (int i = 1; i < n; ++i) {
		if ((p[i] & 0xC0) != 0x80) {
			invalid = true;
			cp = 0xFFFD;
			return 1;
		}
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
		invalid = true;
		cp = 0xFFFD;
		return 1;
	}
	return n;
}

int HiResFontSet::measure(const char *text, uint32 len, const HiResOriginalFont *original) const {
	int w = 0;
	const char *end = text + len;
	while (text < end) {
		uint32 cp;
		bool invalid;
		text += decodeUtf8(text, end, cp, invalid);
		if (invalid && !hasReplacement())
			continue;
		w += getAdvance(cp, original);
	}
	return w;
}

int HiResFontSet::drawText(const char *text, uint32 len, int x, int yBaseline, TextPlane &plane, byte index,
                           const Common::Rect *clip, const HiResOriginalFont *original) const {
	const char *end = text + len;
	while (text < end) {
		uint32 cp;
		bool invalid;
		text += decodeUtf8(text, end, cp, invalid);
		// The renderer's rule: a bad sequence is U+FFFD where a face has it, else nothing.
		if (invalid && !hasReplacement())
			continue;
		x += drawChar(cp, x, yBaseline, plane, index, clip, original);
	}
	return x;
}

int HiResFontMetrics::advance(uint32 cp) {
	if (cp == kControlUnit || cp == '\n')
		return 0;
	return _set.getAdvance(cp, _original);
}

} // End of namespace Graphics
