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

#include "graphics/surface.h"
#include "ags/engine/ac/display.h"
#include "ags/engine/ac/hires_text_twin.h"
#include "ags/engine/gfx/ddb.h"
#include "ags/engine/gfx/hires_twin.h"
#include "ags/shared/font/fonts.h"
#include "ags/shared/gfx/allegro_bitmap.h"
#include "ags/globals.h"

namespace AGS3 {

using namespace AGS::Shared;

// Bitmaps with records kept at most; the least recently used go first
static const uint kMaxEntries = 64;

HiResTextTwins *hires_text_twins() {
	if (_G(hiresTextScale) < 2)
		return nullptr;
	if (!_G(hiresTextTwins))
		_G(hiresTextTwins) = new HiResTextTwins();
	return _G(hiresTextTwins);
}

HiResTextScope::HiResTextScope() : _open(false) {
	HiResTextTwins *tw = hires_text_twins();
	if (tw) {
		tw->beginScope();
		_open = true;
	}
}

HiResTextScope::~HiResTextScope() {
	// The scale cannot go from 1 to N inside a scope; it can drop to 1
	// (HiResTextTwins::clear() then resets the depth)
	if (_open && _G(hiresTextTwins))
		_G(hiresTextTwins)->endScope();
}

void HiResTextTwins::beginScope() {
	if (_depth++ == 0)
		_generation++;
}

void HiResTextTwins::endScope() {
	if (_depth > 0)
		_depth--;
}

void HiResTextTwins::clear() {
	_entries.clear();
	_depth = 0;
}

bool HiResTextTwins::wantsDraw(const Bitmap *ds, int font) const {
	return _depth > 0 && ds && ds->GetColorDepth() >= 16 && is_font_hires_mapped(font);
}

HiResTextTwins::Entry &HiResTextTwins::entryFor(const Bitmap *ds) {
	Entry &e = _entries[ds];
	if (e.generation != _generation) {
		// First draw into this bitmap in this scope: whatever it had is old
		e.capture.clear();
		e.generation = _generation;
	}
	e.lastUse = ++_clock;
	return e;
}

void HiResTextTwins::trim() {
	while (_entries.size() > kMaxEntries) {
		auto oldest = _entries.begin();
		for (auto it = _entries.begin(); it != _entries.end(); ++it)
			if (it->_value.lastUse < oldest->_value.lastUse)
				oldest = it;
		_entries.erase(oldest);
	}
}

void HiResTextTwins::beginDraw(Bitmap *ds, const Common::Rect &band) {
	Entry &e = entryFor(ds);
	e.capture.beginDraw(ds->GetAllegroBitmap()->getSurface().rawSurface(), band);
}

void HiResTextTwins::endDraw(Bitmap *ds, const TextDraw &draw) {
	Entry &e = entryFor(ds);
	e.capture.endDraw(ds->GetAllegroBitmap()->getSurface().rawSurface(), draw);
	trim();
}

void HiResTextTwins::derive(const Bitmap *from, const Bitmap *to, const Point &offset) {
	auto it = _entries.find(from);
	if (it == _entries.end()) {
		_entries.erase(to);
		return;
	}
	Entry out;
	it->_value.capture.deriveRegion(Common::Rect(offset.X, offset.Y, offset.X + to->GetWidth(), offset.Y + to->GetHeight()),
									out.capture);
	out.generation = it->_value.generation;
	out.lastUse = ++_clock;
	_entries[to] = out;
	trim();
}

void HiResTextTwins::attach(AGS::Engine::IDriverDependantBitmap *ddb, Bitmap *bmp, bool hasAlpha) {
	if (!ddb)
		return;
	auto it = bmp ? _entries.find(bmp) : _entries.end();
	if (it == _entries.end() || it->_value.capture.empty()) {
		ddb->SetHiResTwin(nullptr);
		return;
	}
	TextCapture &cap = it->_value.capture;
	const int n = _G(hiresTextScale);
	const Graphics::Surface &src = bmp->GetAllegroBitmap()->getSurface().rawSurface();
	Graphics::Surface textFree;
	textFree.copyFrom(src);
	const uint valid = cap.finish(textFree);
	if (valid == 0) {
		textFree.free();
		_entries.erase(it);			// nothing of it is on the bitmap now
		ddb->SetHiResTwin(nullptr);
		return;
	}

	std::shared_ptr<AGS::Engine::HiResTwin> twin(new AGS::Engine::HiResTwin(src.w * n, src.h * n));
	Graphics::Surface &ts = *twin->GetAllegroBitmap()->getSurface().surfacePtr();
	TextTwin::upscaleToArgb(textFree, hasAlpha, ts, n);
	textFree.free();
	// A keyed bitmap is drawn ignoring alpha: keep what was opaque opaque
	Graphics::Surface picture;
	if (!hasAlpha)
		picture.copyFrom(ts);
	for (const TextRecord &rec : cap.records()) {
		if (!rec.valid)
			continue;
		// Never outside what was checked: the record's rect, and the clip
		// the native text had
		Common::Rect r = rec.rect;
		r.clip(rec.draw.clip);
		if (r.isEmpty())
			continue;
		twin->Rects.push_back(r);
		twin->SetClip(Rect(r.left * n, r.top * n, r.right * n - 1, r.bottom * n - 1));
		wouttext_outline_scaled(twin.get(), rec.draw.x, rec.draw.y, rec.draw.font,
								TextTwin::toArgb(rec.draw.colour, src.format), TextTwin::toArgb(rec.draw.outlineColour, src.format),
								rec.draw.text.c_str(), n);
	}
	twin->ResetClip();
	if (!hasAlpha) {
		for (const Common::Rect &r : twin->Rects)
			TextTwin::keepOpaque(picture, ts, Common::Rect(r.left * n, r.top * n, r.right * n, r.bottom * n));
		picture.free();
	}
	it->_value.lastUse = ++_clock;
	ddb->SetHiResTwin(twin);
}

} // namespace AGS3
