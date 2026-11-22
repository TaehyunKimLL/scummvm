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

#include "graphics/hires_text/svfface.h"

#include "common/stream.h"
#include "common/textconsole.h"

namespace Graphics {

SvfFace::SvfFace() : _open(false), _error(SVF_E_ARG), _stream(nullptr), _disposeStream(DisposeAfterUse::NO) {
	memset(&_face, 0, sizeof(_face));
}

SvfFace::~SvfFace() {
	close();
}

void SvfFace::close() {
	memset(&_face, 0, sizeof(_face));
	_open = false;
	_data.clear();
	_tables.clear();
	_cache.clear();
	if (_stream && _disposeStream == DisposeAfterUse::YES)
		delete _stream;
	_stream = nullptr;
	_disposeStream = DisposeAfterUse::NO;
}

bool SvfFace::loadFromMemory(const byte *data, uint32 size, const Common::String &name) {
	close();
	_name = name;
	if (isAbiMismatch())
		return finishOpen(SVF_E_ARG);
	return finishOpen(svf_open_mapped(&_face, data, size));
}

bool SvfFace::loadWhole(Common::SeekableReadStream &stream, const Common::String &name) {
	close();
	_name = name;
	if (isAbiMismatch())
		return finishOpen(SVF_E_ARG);
	const int64 size = stream.size();
	if (size <= 0 || size > kMaxWholeSize)
		return finishOpen(size <= 0 ? SVF_E_IO : SVF_E_NOMEM);
	_data.resize((uint)size);
	if (!stream.seek(0) || stream.read(_data.data(), (uint32)size) != (uint32)size)
		return finishOpen(SVF_E_IO);
	return finishOpen(svf_open_mapped(&_face, _data.data(), _data.size()));
}

bool SvfFace::loadStreamed(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
                           uint16 blockSize, uint16 slots) {
	close();
	_name = name;
	if (!stream)
		return finishOpen(SVF_E_ARG);
	_stream = stream;
	_disposeStream = dispose;
	if (isAbiMismatch())
		return finishOpen(SVF_E_ARG);
	const int64 size = stream->size();
	if (size <= 0 || size > (int64)0xFFFFFFFF)
		return finishOpen(SVF_E_RANGE);

	svf_err err = SVF_OK;
	const size_t need = svf_stream_tables_size(readCallback, _stream, (uint32)size, &err);
	if (need == 0)
		return finishOpen(err != SVF_OK ? err : SVF_E_NOMEM);
	_tables.resize(need);
	if (slots >= 2)
		_cache.resize((uint)slots * ((uint)blockSize + 8));
	err = svf_open_stream(&_face, readCallback, _stream, (uint32)size, _tables.data(), _tables.size(),
	                      _cache.empty() ? nullptr : _cache.data(), _cache.size(), blockSize);
	return finishOpen(err);
}

bool SvfFace::load(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
                   uint32 wholeLimit) {
	if (stream && stream->size() > 0 && stream->size() <= (int64)wholeLimit) {
		const bool ok = loadWhole(*stream, name);
		if (dispose == DisposeAfterUse::YES)
			delete stream;
		return ok;
	}
	return loadStreamed(stream, dispose, name);
}

bool SvfFace::isAbiMismatch() const {
	if (scummvm_svf_face_size == sizeof(svf_face))
		return false;
	warning("SVF %s: the C renderer sees svf_face as %u bytes, the C++ code as %u: built with different SVF_* switches", _name.c_str(),
	        (uint)scummvm_svf_face_size, (uint)sizeof(svf_face));
	return true;
}

bool SvfFace::finishOpen(svf_err err) {
	_error = err;
	if (err != SVF_OK) {
		if (err == SVF_E_VERSION)
			warning("SVF %s: not an SVFN version 3 face (convert version 2 with tools/svf2to3.py)", _name.c_str());
		else
			warning("SVF %s: cannot open the face (svf_err %d)", _name.c_str(), (int)err);
		close();
		return false;
	}
	_open = true;
	const uint16 status = svf_status(&_face);
	if (status & (SVF_FACE_JSET_BAD | SVF_FACE_JSET_UNREAD | SVF_FACE_JMAP_BAD | SVF_FACE_JMAP_UNREAD))
		warning("SVF %s: its jamo block is not read (status 0x%04x): Hangul syllables are not composed", _name.c_str(), status);
	return true;
}

long SvfFace::readCallback(void *ctx, uint32_t offset, uint8_t *dst, uint32_t len) {
	Common::SeekableReadStream *stream = (Common::SeekableReadStream *)ctx;
	if (len > 0x7FFFFFFF || !stream->seek(offset))
		return -1;
	uint32 got = 0;
	while (got < len) {	// a stream may return fewer bytes than asked; the renderer takes only len
		const uint32 n = stream->read(dst + got, len - got);
		if (stream->err()) {
			stream->clearErr();
			return -1;
		}
		if (n == 0)
			break;
		got += n;
	}
	return (long)got;
}

} // End of namespace Graphics
