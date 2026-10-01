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

#include "graphics/hires_text/banded_plane.h"

#include "common/textconsole.h"
#include "graphics/surface.h"

namespace Graphics {

void BandedPlane::create(int w, int h, bool packable) {
	free();
	if (w <= 0 || h <= 0)
		return;
	_w = w;
	_h = h;
	_packed = packable;
	_packedPitch = (w + 1) / 2;
	_bands.resize((h + kBandRows - 1) >> kBandShift);
}

void BandedPlane::free() {
	for (uint b = 0; b < _bands.size(); ++b)
		::free(_bands[b].data);
	_bands.clear();
	_w = _h = 0;
	_packedPitch = 0;
	_packed = false;
}

byte *BandedPlane::makeBand(int b) {
	Band &band = _bands[b];
	band.data = (byte *)calloc(bandBytes(), 1);
	band.nonZero = 0;
	if (!band.data && !_bandFailed) {
		warning("Hi-res text: no memory for %u bytes of a text plane; some text is not drawn", bandBytes());
		_bandFailed = true;
	}
	return band.data;
}

void BandedPlane::dropBand(int b) {
	::free(_bands[b].data);
	_bands[b].data = nullptr;
	_bands[b].nonZero = 0;
}

void BandedPlane::unpack() {
	if (!_packed)
		return;
	const uint32 bytes = (uint32)kBandRows * _w;
	for (uint b = 0; b < _bands.size(); ++b) {
		Band &band = _bands[b];
		if (!band.data)
			continue;
		byte *wide = (byte *)malloc(bytes);
		if (!wide) {
			// No memory to keep it exactly: this band's text goes.
			if (!_bandFailed) {
				warning("Hi-res text: no memory for %u bytes of a text plane; some text is not drawn", bytes);
				_bandFailed = true;
			}
			dropBand(b);
			continue;
		}
		for (int r = 0; r < kBandRows; ++r) {
			const byte *src = band.data + r * _packedPitch;
			byte *dst = wide + r * _w;
			for (int x = 0; x < _w; ++x)
				dst[x] = (byte)(((x & 1) ? (src[x >> 1] >> 4) : (src[x >> 1] & 15)) * 17);
		}
		::free(band.data);
		band.data = wide;
	}
	_packed = false;
}

void BandedPlane::set(int x, int y, byte v) {
	if (_packed && v % 17)
		unpack();
	const int b = y >> kBandShift;
	Band &band = _bands[b];
	if (!band.data) {
		if (!v || !makeBand(b))
			return;
	}
	const int r = y & (kBandRows - 1);
	byte old;
	if (_packed) {
		byte &cell = band.data[r * _packedPitch + (x >> 1)];
		const byte n = (byte)(v / 17);
		if (x & 1) {
			old = cell >> 4;
			cell = (byte)((cell & 0x0F) | (n << 4));
		} else {
			old = cell & 15;
			cell = (byte)((cell & 0xF0) | n);
		}
	} else {
		byte &cell = band.data[r * _w + x];
		old = cell;
		cell = v;
	}
	if (!old && v)
		++band.nonZero;
	else if (old && !v && --band.nonZero == 0)
		dropBand(b);
}

const byte *BandedPlane::row(int y, byte *scratch) const {
	const Band &band = _bands[y >> kBandShift];
	if (!band.data)
		return nullptr;
	const int r = y & (kBandRows - 1);
	if (!_packed)
		return band.data + r * _w;
	const byte *src = band.data + r * _packedPitch;
	for (int x = 0; x < _w; ++x)
		scratch[x] = (byte)(((x & 1) ? (src[x >> 1] >> 4) : (src[x >> 1] & 15)) * 17);
	return scratch;
}

void BandedPlane::fill(const Common::Rect &rect, byte v) {
	if (!exists())
		return;
	Common::Rect r(rect);
	r.clip(Common::Rect(_w, _h));
	if (r.isEmpty())
		return;
	if (_packed && v % 17)
		unpack();

	for (int b = r.top >> kBandShift; b <= (r.bottom - 1) >> kBandShift; ++b) {
		const int top = MAX<int>(r.top, b << kBandShift);
		const int bottom = MIN<int>(r.bottom, (b + 1) << kBandShift);
		const int bandBottom = MIN<int>(_h, (b + 1) << kBandShift);
		Band &band = _bands[b];
		if (!band.data) {
			if (!v || !makeBand(b))
				continue;
		} else if (!v && r.left == 0 && r.right == _w && top == (b << kBandShift) && bottom == bandBottom) {
			dropBand(b);	// all of it cleared
			continue;
		}
		for (int y = top; y < bottom; ++y) {
			const int rr = y & (kBandRows - 1);
			if (_packed) {
				byte *row = band.data + rr * _packedPitch;
				const byte n = (byte)(v / 17);
				for (int x = r.left; x < r.right; ++x) {
					byte &cell = row[x >> 1];
					const byte old = (x & 1) ? (cell >> 4) : (cell & 15);
					cell = (x & 1) ? (byte)((cell & 0x0F) | (n << 4)) : (byte)((cell & 0xF0) | n);
					if (!old && n)
						++band.nonZero;
					else if (old && !n)
						--band.nonZero;
				}
			} else {
				byte *row = band.data + rr * _w;
				for (int x = r.left; x < r.right; ++x) {
					const byte old = row[x];
					row[x] = v;
					if (!old && v)
						++band.nonZero;
					else if (old && !v)
						--band.nonZero;
				}
			}
		}
		if (!band.nonZero)
			dropBand(b);
	}
}

void BandedPlane::clear() {
	for (uint b = 0; b < _bands.size(); ++b)
		dropBand(b);
}

void BandedPlane::copyFrom(const BandedPlane &other) {
	if (&other == this)
		return;
	free();
	if (!other.exists())
		return;
	_w = other._w;
	_h = other._h;
	_packed = other._packed;
	_packedPitch = other._packedPitch;
	_bands.resize(other._bands.size());
	for (uint b = 0; b < _bands.size(); ++b) {
		if (!other._bands[b].data)
			continue;
		if (!makeBand(b))
			continue;
		memcpy(_bands[b].data, other._bands[b].data, bandBytes());
		_bands[b].nonZero = other._bands[b].nonZero;
	}
}

void BandedPlane::readBytes(uint32 offset, byte *dst, uint32 n) const {
	for (uint32 i = 0; i < n; ++i) {
		const uint32 at = offset + i;
		const int y = at / _w;
		dst[i] = (y < _h) ? get(at % _w, y) : 0;
	}
}

void BandedPlane::writeBytes(uint32 offset, const byte *src, uint32 n) {
	for (uint32 i = 0; i < n; ++i) {
		const uint32 at = offset + i;
		const int y = at / _w;
		if (y < _h)
			set(at % _w, y, src[i]);
	}
}

void BandedPlane::toSurface(Surface &out) const {
	out.create(_w, _h, PixelFormat::createFormatCLUT8());
	for (int y = 0; y < _h; ++y) {
		byte *dst = (byte *)out.getBasePtr(0, y);
		const byte *src = row(y, dst);
		if (!src)
			memset(dst, 0, _w);
		else if (src != dst)
			memcpy(dst, src, _w);
	}
}

uint BandedPlane::bandsHeld() const {
	uint n = 0;
	for (uint b = 0; b < _bands.size(); ++b)
		if (_bands[b].data)
			++n;
	return n;
}

} // End of namespace Graphics
