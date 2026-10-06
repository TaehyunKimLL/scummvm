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

#ifndef BACKENDS_PLATFORM_DOS_SAVEFILE_H
#define BACKENDS_PLATFORM_DOS_SAVEFILE_H

#include "backends/saves/default/default-saves.h"
#include "common/savefile.h"
#include "common/stream.h"
#include "common/system.h"

namespace DOS {

/**
 * A save written by the main thread holds it for as long as it takes to
 * serialize and write, and SDL3's audio thread (the Sound Blaster driver's
 * refill) runs only when the main thread yields: the driver's ring holds
 * about 465 ms, a 100 KB save on a Pentium 75 is 600-800 ms of work, and the
 * ring ran dry at every autosave. This stream yields (delayMillis(0) runs
 * the other threads once) whenever kYieldMs have passed since the last
 * yield. Everything else is forwarded unchanged.
 */
class YieldingWriteStream : public Common::SeekableWriteStream {
public:
	// A yield refills one 93 ms chunk, so one per 30 ms of work outpaces the
	// ring's drain; each costs a mixer pass.
	enum { kYieldMs = 30 };

	YieldingWriteStream(Common::WriteStream *w) : _wrapped(w),
		_seekable(dynamic_cast<Common::SeekableWriteStream *>(w)), _last(g_system->getMillis()) {}
	~YieldingWriteStream() override { delete _wrapped; }

	bool err() const override { return _wrapped->err(); }
	void clearErr() override { _wrapped->clearErr(); }
	bool flush() override { return _wrapped->flush(); }
	void finalize() override { _wrapped->finalize(); }
	int64 pos() const override { return _wrapped->pos(); }

	bool seek(int64 offset, int whence) override {
		if (_seekable)
			return _seekable->seek(offset, whence);
		warning("Seeking isn't supported for compressed save files");
		return false;
	}

	int64 size() const override {
		if (_seekable)
			return _seekable->size();
		warning("Size isn't supported for compressed save files");
		return -1;
	}

	uint32 write(const void *dataPtr, uint32 dataSize) override {
		const uint32 n = _wrapped->write(dataPtr, dataSize);
		maybeYield();
		return n;
	}

	void maybeYield() {
		const uint32 now = g_system->getMillis();
		if (now - _last >= kYieldMs) {
			g_system->delayMillis(0);
			_last = g_system->getMillis();
		}
	}

private:
	Common::WriteStream *_wrapped;
	Common::SeekableWriteStream *_seekable;
	uint32 _last;
};

class SaveFileManager : public DefaultSaveFileManager {
public:
	SaveFileManager(const Common::Path &dir) : DefaultSaveFileManager(dir) {}

	Common::OutSaveFile *openForSaving(const Common::String &filename, bool compress = true) override {
		// The lookups and the file's creation (FAT: ~30 ms on a P75) are
		// work of their own: yield on either side of them.
		g_system->delayMillis(0);
		Common::OutSaveFile *const sf = DefaultSaveFileManager::openForSaving(filename, compress);
		g_system->delayMillis(0);
		if (!sf)
			return nullptr;
		return new Common::OutSaveFile(new YieldingWriteStream(sf));
	}
};

} // End of namespace DOS

#endif
