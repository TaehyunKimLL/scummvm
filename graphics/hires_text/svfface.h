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

#ifndef GRAPHICS_HIRES_TEXT_SVFFACE_H
#define GRAPHICS_HIRES_TEXT_SVFFACE_H

#include "common/array.h"
#include "common/noncopyable.h"
#include "common/str.h"
#include "common/types.h"

#include "graphics/svf/svf.h"

namespace Common {
class SeekableReadStream;
}

// configure adds these switches to DEFINES, so every C and C++ unit sees one svf_face layout.
static_assert(SVF_JMAP == 1, "graphics/svf needs -DSVF_JMAP=1: re-run configure");
static_assert(SVF_JSET_BIG == 1, "graphics/svf needs -DSVF_JSET_BIG=1: re-run configure");
static_assert(SVF_JMAP_RECOG == 0, "graphics/svf needs -DSVF_JMAP_RECOG=0: re-run configure");
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
static_assert(sizeof(svf_face) == 96, "svf_face is not the SVF_JMAP layout");
#elif defined(__i386__) || defined(_M_IX86)
static_assert(sizeof(svf_face) == 76, "svf_face is not the SVF_JMAP layout");
#endif

extern "C" const size_t scummvm_svf_face_size;

namespace Graphics {

/**
 * One SVF face (svf-fonts-open, SVFN v3) opened with the C renderer in graphics/svf.
 *
 * The face is read whole into memory, used in place from caller memory, or streamed: then only its
 * tables are read at open and glyph rows through a block cache when drawn. An SvfFace must not move
 * while it is open (the library keeps pointers into it): never store one by value in a container.
 */
class SvfFace : Common::NonCopyable {
public:
	enum {
		kDefaultWholeLimit = 256 * 1024,
		kMaxWholeSize = 16 * 1024 * 1024,
		kDefaultBlockSize = 1024,
		kDefaultSlots = 64
	};

	SvfFace();
	~SvfFace();

	/** Opens @p data in place; the caller keeps it alive and unchanged while the face is open. */
	bool loadFromMemory(const byte *data, uint32 size, const Common::String &name);
	/** Reads the whole stream (at most kMaxWholeSize bytes) into memory and opens it. */
	bool loadWhole(Common::SeekableReadStream &stream, const Common::String &name);
	/**
	 * Opens a streamed face. The face reads @p stream while it is open; with DisposeAfterUse::YES it
	 * deletes it on close(), also when the open fails. @p slots below 2 means no block cache.
	 */
	bool loadStreamed(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
	                  uint16 blockSize = kDefaultBlockSize, uint16 slots = kDefaultSlots);
	/** loadWhole() for a stream of at most @p wholeLimit bytes, else loadStreamed() with the defaults. */
	bool load(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, const Common::String &name,
	          uint32 wholeLimit = kDefaultWholeLimit);
	void close();

	bool isOpen() const { return _open; }
	bool isStreamed() const { return _stream != nullptr; }
	svf_err getError() const { return _error; }
	const Common::String &getName() const { return _name; }

	uint16 getStatus() const { return svf_status(&_face); }
	bool isJamo() const { return svf_is_jamo(&_face) != 0; }
	bool has(uint32 cp) const { return svf_has(&_face, cp) != 0; }
	uint getBpp() const { return svf_bpp(&_face); }
	uint getCellWidth() const { return svf_cell_w(&_face); }
	uint getCellHeight() const { return svf_cell_h(&_face); }
	uint getAscent() const { return svf_ascent(&_face); }
	uint getCellBytes() const { return (uint)svf_cell_bytes(&_face); }
	uint32 getStreamReads() const { return svf_stream_reads(&_face); }

	svf_face *raw() { return &_face; }
	const svf_face *raw() const { return &_face; }

private:
	bool finishOpen(svf_err err);
	static long readCallback(void *ctx, uint32_t offset, uint8_t *dst, uint32_t len);

	svf_face _face;
	bool _open;
	svf_err _error;
	Common::String _name;
	Common::Array<byte> _data;
	Common::Array<byte> _tables;
	Common::Array<byte> _cache;
	Common::SeekableReadStream *_stream;
	DisposeAfterUse::Flag _disposeStream;
};

} // End of namespace Graphics

#endif
