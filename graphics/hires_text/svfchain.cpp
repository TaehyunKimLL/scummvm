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

#include "graphics/hires_text/svfchain.h"

#include "common/util.h"

namespace Graphics {

namespace {

uint gcd(uint a, uint b) {
	while (b) {
		const uint t = a % b;
		a = b;
		b = t;
	}
	return a;
}

bool fitsInt16(int v) {
	return v >= -32768 && v <= 32767;
}

} // End of anonymous namespace

SvfChain::SvfChain() : _cacheSlots(0) {
	memset(&_chain, 0, sizeof(_chain));
}

SvfChain::~SvfChain() {
	memset(&_chain, 0, sizeof(_chain));
	for (uint i = 0; i < _faces.size(); ++i) {
		if (_dispose[i] == DisposeAfterUse::YES)
			delete _faces[i];
	}
}

bool SvfChain::addFace(SvfFace *face, DisposeAfterUse::Flag dispose) {
	if (!face || !face->isOpen() || _faces.size() >= 255)
		return false;
	_faces.push_back(face);
	_dispose.push_back(dispose);
	_rawFaces.push_back(face->raw());
	update();
	return true;
}

void SvfChain::setMissingAdvance(uint8 advance) {
	_chain.missing_advance = advance;
}

bool SvfChain::setScale(uint num, uint den) {
	if (num < 1 || num > 16 || den < 1 || den > 16)
		return false;
	const uint g = gcd(num, den);
	_chain.scale_num = (uint8)(num / g);
	_chain.scale_den = (uint8)(den / g);
	update();
	return true;
}

bool SvfChain::setSyllableCache(uint slots) {
	_cacheSlots = slots;
	update();
	return slots == 0 || !_syllableCache.empty();
}

void SvfChain::update() {
	_chain.faces = _rawFaces.empty() ? nullptr : _rawFaces.data();
	_chain.count = (uint8)_rawFaces.size();
	const size_t scratch = svf_chain_scratch_bytes(&_chain);
	_scratch.resize((uint)scratch);

	_syllableCache.clear();
	if (_cacheSlots && !_rawFaces.empty()) {
		uint cell = 0;
		for (uint i = 0; i < _faces.size(); ++i)
			cell = MAX(cell, _faces[i]->getCellBytes());
		_syllableCache.resize(_cacheSlots * (8 + cell));
		if (svf_chain_cache(&_chain, _syllableCache.data(), _syllableCache.size()) != SVF_OK)
			_syllableCache.clear();
	} else {
		svf_chain_cache(&_chain, nullptr, 0);
	}
}

int SvfChain::scaleLength(int v, uint num, uint den) {
	if (v < 0)
		return -scaleLength(-v, num, den);
	return (int)(((uint)v * num + den / 2) / den);
}

int SvfChain::scaleDimension(int v, uint num, uint den) {
	return (int)(((uint)v * num + den - 1) / den);
}

int SvfChain::getAscent() const {
	int ascent = 0;
	for (uint i = 0; i < _faces.size(); ++i)
		ascent = MAX(ascent, scaleLength(_faces[i]->getAscent(), getScaleNum(), getScaleDen()));
	return ascent;
}

int SvfChain::getDescent() const {
	int descent = 0;
	for (uint i = 0; i < _faces.size(); ++i) {
		const int h = scaleDimension(_faces[i]->getCellHeight(), getScaleNum(), getScaleDen());
		descent = MAX(descent, h - scaleLength(_faces[i]->getAscent(), getScaleNum(), getScaleDen()));
	}
	return descent;
}

int SvfChain::getCellHeight() const {
	int h = 0;
	for (uint i = 0; i < _faces.size(); ++i)
		h = MAX(h, scaleDimension(_faces[i]->getCellHeight(), getScaleNum(), getScaleDen()));
	return h;
}

int SvfChain::getMaxCellWidth() const {
	int w = scaleLength(_chain.missing_advance, getScaleNum(), getScaleDen());
	for (uint i = 0; i < _faces.size(); ++i)
		w = MAX(w, scaleDimension(_faces[i]->getCellWidth(), getScaleNum(), getScaleDen()));
	return w;
}

bool SvfChain::has(uint32 cp) const {
	for (uint i = 0; i < _faces.size(); ++i) {
		if (_faces[i]->has(cp))
			return true;
	}
	return false;
}

svf_err SvfChain::measure(const char *text, uint32 len, svf_extent &extent) const {
	return svf_measure_utf8(&_chain, text, len, &extent);
}

int SvfChain::getWidth(const char *text, uint32 len) const {
	svf_extent e;
	return measure(text, len, e) == SVF_OK ? e.width : 0;
}

svf_err SvfChain::draw(const char *text, uint32 len, int x, int yBaseline, svf_target &target) const {
	if (!fitsInt16(x) || !fitsInt16(yBaseline))
		return SVF_OK;
	return svf_draw_utf8(&_chain, text, len, (int16_t)x, (int16_t)yBaseline, &target, _scratch.data(), _scratch.size());
}

svf_err SvfChain::drawCoverage(const char *text, uint32 len, int x, int yBaseline, int w, int h, PlotFunc plot, void *ctx,
                               const Common::Rect *clip) const {
	svf_target t;
	const svf_err err = svf_target_init(&t, SVF_T_COVERAGE_CB, nullptr, 0, (int16_t)MIN(w, 32767), (int16_t)MIN(h, 32767), 0);
	if (err != SVF_OK)
		return err;
	t.plot = plot;
	t.ctx = ctx;
	if (clip) {
		t.clip_x0 = clip->left;
		t.clip_y0 = clip->top;
		t.clip_x1 = clip->right;
		t.clip_y1 = clip->bottom;
	}
	return draw(text, len, x, yBaseline, t);
}

} // End of namespace Graphics
