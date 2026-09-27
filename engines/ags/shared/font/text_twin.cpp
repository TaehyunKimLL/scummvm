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


// C23 T4: text records and twins, free of the engine's globals so the unit
// tests link them alone (test/engines/ags/text_twin.h).

#include "ags/shared/font/text_twin.h"
#include "graphics/surface.h"

namespace AGS3 {

void TextCapture::clear() {
	_records.clear();
	_band = Common::Rect();
	_bandPixels.clear();
	_bpp = _w = _h = 0;
}

static void copyRows(const Graphics::Surface &s, const Common::Rect &r, Common::Array<byte> &out) {
	const int bpp = s.format.bytesPerPixel;
	out.resize(r.width() * r.height() * bpp);
	byte *dst = out.begin();
	for (int y = r.top; y < r.bottom; y++, dst += r.width() * bpp)
		memcpy(dst, s.getBasePtr(r.left, y), r.width() * bpp);
}

static bool sameRows(const Graphics::Surface &s, const Common::Rect &r, const Common::Array<byte> &px) {
	const int bpp = s.format.bytesPerPixel;
	const byte *src = px.begin();
	for (int y = r.top; y < r.bottom; y++, src += r.width() * bpp)
		if (memcmp(src, s.getBasePtr(r.left, y), r.width() * bpp))
			return false;
	return true;
}

static void putRows(Graphics::Surface &s, const Common::Rect &r, const Common::Array<byte> &px) {
	const int bpp = s.format.bytesPerPixel;
	const byte *src = px.begin();
	for (int y = r.top; y < r.bottom; y++, src += r.width() * bpp)
		memcpy(s.getBasePtr(r.left, y), src, r.width() * bpp);
}

void TextCapture::beginDraw(const Graphics::Surface &s, const Common::Rect &band) {
	if (_bpp && (_w != s.w || _h != s.h || _format != s.format))
		_records.clear();			// another bitmap: start again
	_bpp = s.format.bytesPerPixel;
	_w = s.w;
	_h = s.h;
	_format = s.format;
	_band = band;
	_band.clip(Common::Rect(0, 0, s.w, s.h));
	if (_band.isEmpty()) {
		_bandPixels.clear();
		return;
	}
	copyRows(s, _band, _bandPixels);
}

bool TextCapture::endDraw(const Graphics::Surface &s, const TextDraw &draw) {
	if (_band.isEmpty() || s.w != _w || s.h != _h || s.format != _format)
		return false;
	// The bounding box of what changed inside the band
	const int bpp = _bpp;
	Common::Rect box;
	bool any = false;
	const byte *old = _bandPixels.begin();
	for (int y = _band.top; y < _band.bottom; y++) {
		const byte *now = (const byte *)s.getBasePtr(_band.left, y);
		for (int x = 0; x < _band.width(); x++, old += bpp, now += bpp) {
			if (!memcmp(old, now, bpp))
				continue;
			const Common::Rect p(_band.left + x, y, _band.left + x + 1, y + 1);
			if (any)
				box.extend(p);
			else
				box = p;
			any = true;
		}
	}
	if (!any)
		return false;

	TextRecord rec;
	rec.draw = draw;
	rec.rect = Common::Rect(box.left - 1, box.top - 1, box.right + 1, box.bottom + 1);
	rec.rect.clip(Common::Rect(0, 0, s.w, s.h));
	copyRows(s, rec.rect, rec.after);
	// "before": the band's copy where it has one, else (the one-pixel
	// margin past the band) the pixels now, which the draw did not change
	rec.before = rec.after;
	for (int y = rec.rect.top; y < rec.rect.bottom; y++) {
		for (int x = rec.rect.left; x < rec.rect.right; x++) {
			if (!_band.contains(x, y))
				continue;
			memcpy(&rec.before[((y - rec.rect.top) * rec.rect.width() + (x - rec.rect.left)) * bpp],
				   &_bandPixels[((y - _band.top) * _band.width() + (x - _band.left)) * bpp], bpp);
		}
	}
	_records.push_back(rec);
	return true;
}

uint TextCapture::finish(Graphics::Surface &textFree) {
	uint valid = 0;
	const bool same = textFree.w == _w && textFree.h == _h && textFree.format == _format;
	for (int i = (int)_records.size() - 1; i >= 0; i--) {
		TextRecord &rec = _records[i];
		rec.valid = same && sameRows(textFree, rec.rect, rec.after);
		if (!rec.valid)
			continue;
		putRows(textFree, rec.rect, rec.before);
		valid++;
	}
	return valid;
}

void TextCapture::deriveRegion(const Common::Rect &area, TextCapture &out) const {
	out.clear();
	out._bpp = _bpp;
	out._w = area.width();
	out._h = area.height();
	out._format = _format;
	for (uint i = 0; i < _records.size(); i++) {
		const TextRecord &rec = _records[i];
		if (!area.contains(rec.rect))
			continue;
		TextRecord moved = rec;
		moved.rect.translate(-area.left, -area.top);
		moved.draw.x -= area.left;
		moved.draw.y -= area.top;
		moved.draw.clip.translate(-area.left, -area.top);
		moved.draw.clip.clip(Common::Rect(0, 0, area.width(), area.height()));
		moved.valid = false;
		out._records.push_back(moved);
	}
}

uint32 TextTwin::toArgb(uint32 colour, const Graphics::PixelFormat &fmt) {
	byte a, r, g, b;
	fmt.colorToARGB(colour, a, r, g, b);
	return (0xFFu << 24) | (r << 16) | (g << 8) | b;
}

void TextTwin::upscaleToArgb(const Graphics::Surface &src, bool srcHasAlpha, Graphics::Surface &dst, int scale) {
	const Graphics::PixelFormat &sf = src.format;
	const int bpp = sf.bytesPerPixel;
	for (int y = 0; y < src.h; y++) {
		uint32 *out = (uint32 *)dst.getBasePtr(0, y * scale);
		const byte *in = (const byte *)src.getBasePtr(0, y);
		for (int x = 0; x < src.w; x++, in += bpp) {
			const uint32 c = bpp == 2 ? *(const uint16 *)in : *(const uint32 *)in;
			byte a, r, g, b;
			sf.colorToARGB(c, a, r, g, b);
			uint32 pixel;
			if ((r == 255 && g == 0 && b == 255) || (srcHasAlpha && a == 0))
				pixel = kTransparent;
			else
				pixel = ((uint32)(srcHasAlpha && sf.aBits() ? a : 255) << 24) | (r << 16) | (g << 8) | b;
			for (int i = 0; i < scale; i++)
				*out++ = pixel;
		}
		for (int i = 1; i < scale; i++)
			memcpy(dst.getBasePtr(0, y * scale + i), dst.getBasePtr(0, y * scale), src.w * scale * 4);
	}
}

} // namespace AGS3
