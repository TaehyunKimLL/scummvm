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

#ifndef BACKENDS_PLATFORM_DOS_SOFT_CURSOR_H
#define BACKENDS_PLATFORM_DOS_SOFT_CURSOR_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

namespace DOS {

/**
 * A cursor drawn into the frame we send, since SDL3's direct-framebuffer
 * path draws none. It keeps what it covered so moving it rewrites only its
 * own rectangle. Image and destination share one pixel size (1, 2 or 4
 * bytes); converting to the screen format is the caller's job.
 */
class SoftCursor {
public:
	SoftCursor() : _w(0), _h(0), _hotX(0), _hotY(0), _key(0), _bpp(1) {}

	bool hasImage() const { return _w && _h; }

	void setImage(const byte *buf, uint w, uint h, int hotX, int hotY, uint32 key, uint bpp) {
		_w = w; _h = h; _hotX = hotX; _hotY = hotY; _key = key; _bpp = bpp;
		_image.resize(w * h * bpp);
		if (!_image.empty())
			memcpy(&_image[0], buf, _image.size());
	}

	Common::Rect draw(byte *dst, int pitch, int dstW, int dstH, int x, int y) {
		Common::Rect r(x - _hotX, y - _hotY, x - _hotX + _w, y - _hotY + _h);
		r.clip(Common::Rect(0, 0, dstW, dstH));
		_saved = r;
		if (r.isEmpty() || !hasImage())
			return _saved = Common::Rect();
		_under.resize(r.width() * r.height() * _bpp);
		for (int row = 0; row < r.height(); ++row) {
			byte *d = dst + (r.top + row) * pitch + r.left * _bpp;
			memcpy(&_under[row * r.width() * _bpp], d, r.width() * _bpp);
			const int sy = r.top + row - (y - _hotY);
			for (int col = 0; col < r.width(); ++col) {
				const int sx = r.left + col - (x - _hotX);
				const byte *s = &_image[(sy * _w + sx) * _bpp];
				if (read(s) != _key)
					memcpy(d + col * _bpp, s, _bpp);
			}
		}
		return _saved;
	}

	Common::Rect restore(byte *dst, int pitch) {
		const Common::Rect r = _saved;
		for (int row = 0; row < r.height(); ++row)
			memcpy(dst + (r.top + row) * pitch + r.left * _bpp, &_under[row * r.width() * _bpp], r.width() * _bpp);
		_saved = Common::Rect();
		return r;
	}

private:
	uint32 read(const byte *p) const {
		switch (_bpp) {
		case 1: return *p;
		case 2: return *(const uint16 *)p;
		default: return *(const uint32 *)p;
		}
	}

	uint _w, _h;
	int _hotX, _hotY;
	uint32 _key;
	uint _bpp;
	Common::Array<byte> _image, _under;
	Common::Rect _saved;
};

} // End of namespace DOS

#endif
