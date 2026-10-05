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

// Disable symbol overrides for FILE and fseek as those are used in the
// Vorbis headers.
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fseek

#include "audio/decoders/vorbis.h"
#include "audio/decoders/vorbis_intern.h"

#ifdef USE_VORBIS

#include "common/array.h"
#include "common/mutex.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

// The setup cache reads the header packets itself, with libogg.
#ifdef USE_OGG
#define VORBIS_SETUP_CACHE
#endif

namespace Audio {

// These are wrapper functions to allow using a SeekableReadStream object to
// provide data to the OggVorbis_File object.

static size_t read_stream_wrap(void *ptr, size_t size, size_t nmemb, void *datasource) {
	Common::SeekableReadStream *stream = (Common::SeekableReadStream *)datasource;

	uint32 result = stream->read(ptr, size * nmemb);

	return result / size;
}

static int seek_stream_wrap(void *datasource, ogg_int64_t offset, int whence) {
	Common::SeekableReadStream *stream = (Common::SeekableReadStream *)datasource;
	return stream->seek(offset, whence) ? 0 : -1;
}

static int close_stream_wrap(void *datasource) {
	// Do nothing -- we leave it up to the VorbisStream to free memory as appropriate.
	return 0;
}

static long tell_stream_wrap(void *datasource) {
	Common::SeekableReadStream *stream = (Common::SeekableReadStream *)datasource;
	return stream->pos();
}

static const ov_callbacks g_stream_wrap = {
	read_stream_wrap, seek_stream_wrap, close_stream_wrap, tell_stream_wrap
};



#pragma mark -
#pragma mark --- Setup header cache ---
#pragma mark -

// Expanding the codebooks of a setup header (vorbis_synthesis_init() builds
// the decode tables of every book) is most of what opening a short clip
// costs: on a Pentium at 75 MHz about 85 of the 151 ms that opening a 1.7 s
// speech clip takes. Clips made with one encoder setting have the same setup
// header, so the expanded setup (the codec_setup of a vorbis_info) is kept
// and shared, read only, by every stream whose setup header, channel count
// and block sizes are the same.
//
// vorbisfile parses the headers in ov_open() but expands the codebooks only
// when decoding starts, and only if codec_setup has no expanded ones yet
// (libvorbis and Tremor alike): a stream that swaps its own codec_setup for
// a shared, expanded one before its first read decodes exactly as before.
// vorbis_info_clear() frees the codec_setup it is given, so a stream hands
// its shared one back before ov_clear().

namespace VorbisSetupCache {

#ifdef VORBIS_SETUP_CACHE

namespace {

// Header packets beyond this many bytes from the start are not looked for.
const uint32 kMaxHeaderBytes = 65536;

struct Entry {
	Common::Array<byte> setup;	///< the setup header packet
	int channels;
	int blocksizes[2];
	void *codecSetup;	///< the expanded codec_setup; read only once here
	uint refs;			///< the cache's own and one per stream
	uint32 lastUse;
};

void freeCodecSetup(void *codecSetup) {
	vorbis_info vi;
	memset(&vi, 0, sizeof(vi));
	vi.codec_setup = codecSetup;
	vorbis_info_clear(&vi);
}

class Cache {
public:
	Cache() : _mutex(g_system ? g_system->createMutex() : nullptr), _capacity(kDefaultCapacity),
		_enabled(true), _clock(0) {
		memset(&_stats, 0, sizeof(_stats));
	}

	bool isEnabled() {
		Lock lock(*this);
		return _enabled;
	}

	void setEnabled(bool enabled) {
		Lock lock(*this);
		_enabled = enabled;
	}

	/** The cached setup for this key with a reference for the caller, or nullptr. */
	Entry *acquire(const Common::Array<byte> &setup, int channels, const int blocksizes[2]) {
		Lock lock(*this);
		for (uint i = 0; i < _entries.size(); ++i) {
			Entry *e = _entries[i];
			if (e->channels == channels && e->blocksizes[0] == blocksizes[0] &&
			    e->blocksizes[1] == blocksizes[1] && e->setup.size() == setup.size() &&
			    !memcmp(e->setup.begin(), setup.begin(), setup.size())) {
				e->lastUse = ++_clock;
				++e->refs;
				++_stats.hits;
				return e;
			}
		}
		return nullptr;
	}

	/**
	 * Cache an expanded setup and return its entry with a reference for the
	 * caller. Another thread may have cached the same setup meanwhile: then
	 * codecSetup is freed and that entry is returned.
	 */
	Entry *insert(const Common::Array<byte> &setup, int channels, const int blocksizes[2], void *codecSetup) {
		Entry *e = acquire(setup, channels, blocksizes);
		if (e) {
			freeCodecSetup(codecSetup);
			return e;
		}
		Common::Array<Entry *> dead;
		{
			Lock lock(*this);
			e = new Entry;
			e->setup = setup;
			e->channels = channels;
			e->blocksizes[0] = blocksizes[0];
			e->blocksizes[1] = blocksizes[1];
			e->codecSetup = codecSetup;
			e->refs = 2;
			e->lastUse = ++_clock;
			++_stats.misses;
			_entries.push_back(e);
			trim(_capacity, dead);
		}
		destroy(dead);
		return e;
	}

	void release(Entry *e) {
		bool last;
		{
			Lock lock(*this);
			last = !--e->refs;
		}
		if (last) {
			freeCodecSetup(e->codecSetup);
			delete e;
		}
	}

	void setCapacity(uint n) {
		Common::Array<Entry *> dead;
		{
			Lock lock(*this);
			_capacity = MAX(n, 1U);
			trim(_capacity, dead);
		}
		destroy(dead);
	}

	void clear() {
		Common::Array<Entry *> dead;
		{
			Lock lock(*this);
			trim(0, dead);
			memset(&_stats, 0, sizeof(_stats));
		}
		destroy(dead);
	}

	Stats getStats() {
		Lock lock(*this);
		Stats s = _stats;
		s.entries = _entries.size();
		return s;
	}

private:
	class Lock {
	public:
		explicit Lock(Cache &c) : _m(c.mutex()) {
			if (_m)
				_m->lock();
		}
		~Lock() {
			if (_m)
				_m->unlock();
		}
	private:
		Common::MutexInternal *_m;
	};

	// Made with the cache when there is a system already (always, but in
	// unit tests that run without one, and so have no other threads).
	Common::MutexInternal *mutex() {
		if (!_mutex && g_system)
			_mutex = g_system->createMutex();
		return _mutex;
	}

	/** Drop the least recently used entries down to n; dead gets those no stream uses. */
	void trim(uint n, Common::Array<Entry *> &dead) {
		while (_entries.size() > n) {
			uint oldest = 0;
			for (uint i = 1; i < _entries.size(); ++i)
				if (_entries[i]->lastUse < _entries[oldest]->lastUse)
					oldest = i;
			Entry *e = _entries[oldest];
			_entries.remove_at(oldest);
			if (!--e->refs)
				dead.push_back(e);
		}
	}

	static void destroy(const Common::Array<Entry *> &dead) {
		for (uint i = 0; i < dead.size(); ++i) {
			freeCodecSetup(dead[i]->codecSetup);
			delete dead[i];
		}
	}

	Common::MutexInternal *_mutex;
	Common::Array<Entry *> _entries;
	uint _capacity;
	bool _enabled;
	uint32 _clock;
	Stats _stats;
};

// Never destroyed: a stream may outlive static destruction, and the mutex
// belongs to the backend.
Cache &cache() {
	static Cache *c = new Cache();
	return *c;
}

struct HeaderPackets {
	Common::Array<byte> data[3];
	uint32 serial;
};

/**
 * Read the three header packets of the first logical stream, then go back
 * to where the stream was. False if they are not Vorbis headers or are not
 * all in the first kMaxHeaderBytes.
 */
bool readHeaderPackets(Common::SeekableReadStream *stream, HeaderPackets &hp) {
	const int64 start = stream->pos();
	ogg_sync_state oy;
	ogg_stream_state os;
	ogg_sync_init(&oy);
	bool haveStream = false, bad = false;
	int got = 0;
	uint32 total = 0;
	while (got < 3 && !bad) {
		ogg_page og;
		const int r = ogg_sync_pageout(&oy, &og);
		if (r < 0)
			continue;	// skipped bytes that are not a page
		if (r == 0) {
			if (total >= kMaxHeaderBytes)
				break;
			char *buf = ogg_sync_buffer(&oy, 4096);
			const uint32 n = buf ? stream->read(buf, 4096) : 0;
			if (!n)
				break;
			ogg_sync_wrote(&oy, n);
			total += n;
			continue;
		}
		if (!haveStream) {
			if (!ogg_page_bos(&og)) {
				bad = true;
				break;
			}
			hp.serial = (uint32)ogg_page_serialno(&og);
			ogg_stream_init(&os, ogg_page_serialno(&og));
			haveStream = true;
		}
		if ((uint32)ogg_page_serialno(&og) != hp.serial)
			continue;	// a page of another, multiplexed stream
		if (ogg_stream_pagein(&os, &og)) {
			bad = true;
			break;
		}
		ogg_packet op;
		int p;
		while (got < 3 && (p = ogg_stream_packetout(&os, &op)) != 0) {
			if (p < 0) {
				bad = true;
				break;
			}
			hp.data[got].resize(op.bytes);
			memcpy(hp.data[got].begin(), op.packet, op.bytes);
			++got;
		}
	}
	if (haveStream)
		ogg_stream_clear(&os);
	ogg_sync_clear(&oy);
	stream->seek(start);
	return !bad && got == 3 &&
		hp.data[0].size() >= 7 && !memcmp(hp.data[0].begin(), "\001vorbis", 7) &&
		hp.data[2].size() >= 7 && !memcmp(hp.data[2].begin(), "\005vorbis", 7);
}

/** Parse the header packets and expand the codebooks; the codec_setup, or nullptr. */
void *expandSetup(HeaderPackets &hp) {
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_info_init(&vi);
	vorbis_comment_init(&vc);
	bool ok = true;
	for (int i = 0; i < 3 && ok; ++i) {
		ogg_packet op;
		memset(&op, 0, sizeof(op));
		op.packet = hp.data[i].begin();
		op.bytes = hp.data[i].size();
		op.b_o_s = i == 0;
		op.packetno = i;
		ok = vorbis_synthesis_headerin(&vi, &vc, &op) == 0;
	}
	if (ok) {
		vorbis_dsp_state vd;
		// Builds the decode tables of every codebook into vi.codec_setup
		// (and frees the books' packed form), as a stream's first read would.
		ok = vorbis_synthesis_init(&vd, &vi) == 0;
		if (ok)
			vorbis_dsp_clear(&vd);
	}
	void *codecSetup = nullptr;
	if (ok) {
		codecSetup = vi.codec_setup;
		vi.codec_setup = nullptr;
	}
	vorbis_info_clear(&vi);
	vorbis_comment_clear(&vc);
	return codecSetup;
}

} // End of anonymous namespace

/**
 * Give an opened stream a shared, expanded codec_setup for its own: the
 * cache's entry (to hand to release()) or nullptr if it keeps its own.
 */
static void *share(OggVorbis_File &vf, HeaderPackets &hp) {
	if (vf.links != 1 || vf.ready_state >= INITSET || !vf.vi || !vf.vi[0].codec_setup ||
	    !vf.serialnos || (uint32)vf.serialnos[0] != hp.serial)
		return nullptr;
	vorbis_info *vi = &vf.vi[0];
	const int blocksizes[2] = { vorbis_info_blocksize(vi, 0), vorbis_info_blocksize(vi, 1) };
	Entry *e = cache().acquire(hp.data[2], vi->channels, blocksizes);
	if (!e) {
		void *codecSetup = expandSetup(hp);
		if (!codecSetup)
			return nullptr;
		e = cache().insert(hp.data[2], vi->channels, blocksizes, codecSetup);
	}
	freeCodecSetup(vi->codec_setup);
	vi->codec_setup = e->codecSetup;
	return e;
}

/** Undo share() before ov_clear(), which would free the shared codec_setup. */
static void unshare(OggVorbis_File &vf, void *entry) {
	vorbis_block_clear(&vf.vb);
	vorbis_dsp_clear(&vf.vd);	// still needs the codec_setup
	vf.vi[0].codec_setup = nullptr;
	cache().release((Entry *)entry);
}

void setEnabled(bool enabled) {
	cache().setEnabled(enabled);
}

void setCapacity(uint n) {
	cache().setCapacity(n);
}

void clear() {
	cache().clear();
}

Stats getStats() {
	return cache().getStats();
}

#else

void setEnabled(bool) {}
void setCapacity(uint) {}
void clear() {}

Stats getStats() {
	Stats s = { 0, 0, 0 };
	return s;
}

#endif // VORBIS_SETUP_CACHE

} // End of namespace VorbisSetupCache

#pragma mark -
#pragma mark --- Ogg Vorbis stream ---
#pragma mark -


VorbisStream::VorbisStream(Common::SeekableReadStream *inStream, DisposeAfterUse::Flag dispose) :
	_inStream(inStream, dispose),
	_length(0, 1000),
	_bufferEnd(ARRAYEND(_buffer)),
	_sharedSetup(nullptr) {

#ifdef VORBIS_SETUP_CACHE
	VorbisSetupCache::HeaderPackets headers;
	const bool haveHeaders = VorbisSetupCache::cache().isEnabled() &&
		VorbisSetupCache::readHeaderPackets(inStream, headers);
#endif

	int res = ov_open_callbacks(inStream, &_ovFile, nullptr, 0, g_stream_wrap);
	if (res < 0) {
		warning("Could not create Vorbis stream (%d)", res);
		_pos = _bufferEnd;
		return;
	}

#ifdef VORBIS_SETUP_CACHE
	if (haveHeaders)
		_sharedSetup = VorbisSetupCache::share(_ovFile, headers);
#endif

	// Read in initial data
	if (!refill())
		return;

	// Setup some header information
	_isStereo = ov_info(&_ovFile, -1)->channels >= 2;
	_rate = ov_info(&_ovFile, -1)->rate;

#ifdef USE_TREMOR
	_length = Timestamp(ov_time_total(&_ovFile, -1), getRate());
#else
	_length = Timestamp(uint32(ov_time_total(&_ovFile, -1) * 1000.0), getRate());
#endif
}

VorbisStream::~VorbisStream() {
#ifdef VORBIS_SETUP_CACHE
	if (_sharedSetup)
		VorbisSetupCache::unshare(_ovFile, _sharedSetup);
#endif
	ov_clear(&_ovFile);
}

int VorbisStream::readBuffer(int16 *buffer, const int numSamples) {
	int samples = 0;
	while (samples < numSamples && _pos < _bufferEnd) {
		const int len = MIN(numSamples - samples, (int)(_bufferEnd - _pos));
		memcpy(buffer, _pos, len * 2);
		buffer += len;
		_pos += len;
		samples += len;
		if (_pos >= _bufferEnd) {
			if (!refill())
				break;
		}
	}
	return samples;
}

bool VorbisStream::seek(const Timestamp &where) {
	// Vorbisfile uses the sample pair number, thus we always use "false" for the isStereo parameter
	// of the convertTimeToStreamPos helper.
	int res = ov_pcm_seek(&_ovFile, convertTimeToStreamPos(where, getRate(), false).totalNumberOfFrames());
	if (res) {
		warning("Error seeking in Vorbis stream (%d)", res);
		_pos = _bufferEnd;
		return false;
	}

	return refill();
}

bool VorbisStream::refill() {
	// Read the samples
	uint len_left = sizeof(_buffer);
	char *read_pos = (char *)_buffer;

	while (len_left > 0) {
		long result;

#ifdef USE_TREMOR
		// Tremor ov_read() always returns data as signed 16 bit interleaved PCM
		// in host byte order. As such, it does not take arguments to request
		// specific signedness, byte order or bit depth as in Vorbisfile.
		result = ov_read(&_ovFile, read_pos, len_left,
						NULL);
#else
#ifdef SCUMM_BIG_ENDIAN
		result = ov_read(&_ovFile, read_pos, len_left,
						1,
						2,	// 16 bit
						1,	// signed
						NULL);
#else
		result = ov_read(&_ovFile, read_pos, len_left,
						0,
						2,	// 16 bit
						1,	// signed
						nullptr);
#endif
#endif
		if (result == OV_HOLE) {
			// Possibly recoverable, just warn about it
			warning("Corrupted data in Vorbis file");
		} else if (result == 0) {
			//warning("End of file while reading from Vorbis file");
			//_pos = _bufferEnd;
			//return false;
			break;
		} else if (result < 0) {
			warning("Error reading from Vorbis stream (%d)", int(result));
			_pos = _bufferEnd;
			// Don't delete it yet, that causes problems in
			// the CD player emulation code.
			return false;
		} else {
			len_left -= result;
			read_pos += result;
		}
	}

	_pos = _buffer;
	_bufferEnd = (int16 *)read_pos;

	return true;
}


#pragma mark -
#pragma mark --- Ogg Vorbis factory functions ---
#pragma mark -

SeekableAudioStream *makeVorbisStream(
	Common::SeekableReadStream *stream,
	DisposeAfterUse::Flag disposeAfterUse) {
	SeekableAudioStream *s = new VorbisStream(stream, disposeAfterUse);
	if (s && s->endOfData()) {
		delete s;
		return nullptr;
	} else {
		return s;
	}
}

} // End of namespace Audio

#endif // #ifdef USE_VORBIS
