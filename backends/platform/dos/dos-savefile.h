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

#include "backends/platform/dos/dos-savenames.h"
#include "backends/saves/default/default-saves.h"
#include "common/fs.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
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

/**
 * Saves on an 8.3 disk. The default manager keeps its cache under the names the directory lists and finds a file only by
 * exactly such a name, while an engine asks by the name it made from the target (kq1-rgb565.001). Every single-file
 * operation goes to the default manager under the name DOS gives it (dosPattern(): a long base becomes 4 characters and a
 * hash, see dos-savenames.h), so targets never share a file.
 *
 * A listing must still answer wildcard patterns against the ENGINE's names ("kq1-ko-*.sav", "qfg1-*": SCI's file search
 * and the QFG1 -> QFG2 import). The hashed name cannot be inverted, so every save written under a mapped name is entered
 * in SAVEIDX.TXT in the save folder ("dosname<TAB>enginename" lines). The index is advisory: an entry counts only when
 * its file exists and dosPattern(engine name) is that file's name (a stale, truncated or corrupt line is dropped); a
 * missing index loses the engine names of wildcard listings only, never a save (exact names still map). It is written
 * in place on DOS (no rename over an existing file on 8.3 FAT) and atomically where the host has it.
 */
class SaveFileManager : public DefaultSaveFileManager {
public:
	SaveFileManager(const Common::Path &dir) : DefaultSaveFileManager(dir), _indexLoaded(false) {}

	Common::OutSaveFile *openForSaving(const Common::String &filename, bool compress = true) override {
		// The lookups and the file's creation (FAT: ~30 ms on a P75) are
		// work of their own: yield on either side of them.
		g_system->delayMillis(0);
		const Common::String dos = dosPattern(filename);
		// A hash collision: this DOS name already belongs to another target's save (per the index, file present). Refuse
		// instead of overwriting it.
		loadIndex();
		if (indexMapsValidly(dos) && !_index.find(dos)->_value.equalsIgnoreCase(filename) && DefaultSaveFileManager::exists(dos)) {
			warning("DOS: save name %s would overwrite %s (same DOS name %s): not saved", filename.c_str(),
			        _index.find(dos)->_value.c_str(), dos.c_str());
			return nullptr;
		}
		// An unindexed file of that DOS name is a plain 8.3 save of a short target ("kq1-u6f4.001"): a long name that hashes
		// onto it must not overwrite it either.
		if (!dos.equalsIgnoreCase(filename) && !indexMapsValidly(dos) && DefaultSaveFileManager::exists(dos)) {
			warning("DOS: save name %s would overwrite the existing save %s: not saved", filename.c_str(), dos.c_str());
			return nullptr;
		}
		Common::OutSaveFile *const sf = DefaultSaveFileManager::openForSaving(dos, compress);
		g_system->delayMillis(0);
		if (!sf)
			return nullptr;
		noteMapping(dos, filename);
		return new Common::OutSaveFile(new YieldingWriteStream(sf));
	}

	Common::InSaveFile *openForLoading(const Common::String &filename) override {
		return DefaultSaveFileManager::openForLoading(dosPattern(filename));
	}

	Common::InSaveFile *openRawFile(const Common::String &filename) override {
		return DefaultSaveFileManager::openRawFile(dosPattern(filename));
	}

	bool removeSavefile(const Common::String &filename) override {
		const Common::String dos = dosPattern(filename);
		const bool ok = DefaultSaveFileManager::removeSavefile(dos);
		if (ok)
			forgetMapping(dos);
		return ok;
	}

	bool exists(const Common::String &filename) override {
		return DefaultSaveFileManager::exists(dosPattern(filename));
	}

	// Two engine names that DOS keeps as one file must never be copied or renamed onto each other: the copy would
	// truncate the file it reads, and the rename would then remove it. The same name (any case) is a no-op that
	// succeeds when the save exists; names that merely collide are refused.
	bool renameSavefile(const Common::String &oldName, const Common::String &newName, bool compress = true) override {
		if (oldName.equalsIgnoreCase(newName))
			return exists(oldName);
		if (dosPattern(oldName).equalsIgnoreCase(dosPattern(newName)))
			return false;
		return DefaultSaveFileManager::renameSavefile(oldName, newName, compress);
	}

	bool copySavefile(const Common::String &oldName, const Common::String &newName, bool compress = true) override {
		if (oldName.equalsIgnoreCase(newName))
			return exists(oldName);
		if (dosPattern(oldName).equalsIgnoreCase(dosPattern(newName)))
			return false;
		return DefaultSaveFileManager::copySavefile(oldName, newName, compress);
	}

	Common::StringArray listSavefiles(const Common::String &pattern) override {
		loadIndex();
		Common::StringArray out;
		// Every save by its engine name where the index knows it, else as the directory lists it, matched against the
		// pattern as the engine wrote it.
		const Common::StringArray all = DefaultSaveFileManager::listSavefiles("*");
		for (uint i = 0; i < all.size(); ++i) {
			if (all[i].equalsIgnoreCase(indexName()))
				continue;
			Common::String engine = all[i];
			IndexMap::const_iterator it = _index.find(all[i]);
			if (it != _index.end() && dosPattern(it->_value).equalsIgnoreCase(all[i]))
				engine = it->_value;
			if (engine.matchString(pattern, true))
				pushUnique(out, engine);
		}
		// A save whose index entry is lost is still found by a pattern with a concrete long base, as before.
		const Common::String dos = dosPattern(pattern);
		if (dos != pattern) {
			const Common::StringArray byName = DefaultSaveFileManager::listSavefiles(dos);
			for (uint i = 0; i < byName.size(); ++i)
				if (!byName[i].equalsIgnoreCase(indexName()) && !indexMapsValidly(byName[i]))	// those are listed above
					pushUnique(out, engineName(pattern, byName[i]));
		}
		return out;
	}

private:
	typedef Common::HashMap<Common::String, Common::String, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> IndexMap;
	static const char *indexName() { return "saveidx.txt"; }

	// Whether the index gives @p dos an engine name whose DOS name it really is.
	bool indexMapsValidly(const Common::String &dos) const {
		IndexMap::const_iterator it = _index.find(dos);
		return it != _index.end() && dosPattern(it->_value).equalsIgnoreCase(dos);
	}

	static void pushUnique(Common::StringArray &a, const Common::String &s) {
		for (uint i = 0; i < a.size(); ++i)
			if (a[i].equalsIgnoreCase(s))
				return;
		a.push_back(s);
	}

	void loadIndex() {
		if (_indexLoaded)
			return;
		_indexLoaded = true;
		_index.clear();
		Common::InSaveFile *in = DefaultSaveFileManager::openRawFile(indexName());
		if (!in)
			return;
		while (!in->eos() && !in->err()) {
			const Common::String line = in->readLine();
			const size_t tab = line.findFirstOf('\t');
			if (tab == Common::String::npos || tab == 0 || tab + 1 >= line.size())
				continue;	// a torn or corrupt line is dropped
			const Common::String dos(line.c_str(), tab), engine(line.c_str() + tab + 1);
			if (dosPattern(engine).equalsIgnoreCase(dos))
				_index[dos] = engine;
		}
		delete in;
	}

	void saveIndex() {
		const Common::Path path = getSavePath().appendComponent(indexName());
		Common::WriteStream *w = Common::FSNode(path).createWriteStream(true);
		if (!w)
			return;
		Common::String text;
		for (IndexMap::const_iterator it = _index.begin(); it != _index.end(); ++it)
			text += it->_key + "\t" + it->_value + "\n";
		w->writeString(text);
		w->finalize();
		delete w;
		updateSavefilesList(_noLocked);	// the folder changed under the default manager's cache
	}

	void noteMapping(const Common::String &dos, const Common::String &engine) {
		if (dos.equalsIgnoreCase(engine))
			return;
		loadIndex();
		IndexMap::const_iterator it = _index.find(dos);
		if (it != _index.end() && it->_value == engine)
			return;
		_index[dos] = engine;
		saveIndex();
	}

	void forgetMapping(const Common::String &dos) {
		loadIndex();
		IndexMap::iterator it = _index.find(dos);
		if (it == _index.end())
			return;
		_index.erase(it);
		saveIndex();
	}

	IndexMap _index;
	bool _indexLoaded;
	Common::StringArray _noLocked;
};

} // End of namespace DOS

#endif
