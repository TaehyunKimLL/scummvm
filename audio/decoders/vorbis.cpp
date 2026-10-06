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

#ifdef __DJGPP__
// build-dos.sh defines VORBIS_REFILL_SAMPLES=1024 for every object; vorbis.cpp
// and the Nancy subclass must agree on the VorbisStream layout.
static_assert(Audio::kVorbisRefillSamples == 1024, "the DOS build must compile with -DVORBIS_REFILL_SAMPLES=1024");
#endif
#ifdef USE_VORBIS

#include "common/array.h"
#include "common/mutex.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/util.h"

// This cache needs the tested private Tremor codec_setup layout and an
// explicit build opt-in. Unknown layouts retain the regular vorbisfile path.
#if defined(USE_OGG) && defined(VORBIS_SETUP_INTERNALS) && defined(USE_TREMOR)
#define VORBIS_SETUP_CACHE
#endif

#ifdef VORBIS_SETUP_CACHE
extern "C" {
#include <codec_internal.h>
}
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
#ifdef VORBIS_SETUP_CACHE_TEST_HOOK
bool g_forceDuplicateInsert = false;
uint g_duplicateInsertFrees = 0;
bool forceDuplicateInsert() {
	const bool force = g_forceDuplicateInsert;
	g_forceDuplicateInsert = false;
	return force;
}
#endif

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
	// Constructed once by cache(); g_system is present before concurrent
	// audio activity. The g_system-less unit runner is single-threaded.
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
			if (matches(*e, setup, channels, blocksizes)) {
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
	 * the donor takes that setup and its own is freed. Only a genuinely new
	 * entry counts as a miss; a racing duplicate has already paid expansion.
	 * The loser is not counted as a cache hit, either.
	 */
	Entry *insert(const Common::Array<byte> &setup, int channels, const int blocksizes[2], vorbis_info *donor) {
		void *codecSetup = donor->codec_setup;
		Common::Array<Entry *> dead;
		Entry *e = nullptr;
		bool tookExisting = false;
		{
			Lock lock(*this);
			// The first lookup may have missed in another thread. Deduplicate
			// under the insertion lock; the losing expansion is freed below.
			for (uint i = 0; i < _entries.size(); ++i) {
				Entry *candidate = _entries[i];
				if (matches(*candidate, setup, channels, blocksizes)) {
					e = candidate;
					tookExisting = true;
					e->lastUse = ++_clock;
					++e->refs;
					// Not a cache hit: this caller already expanded its books.
					break;
				}
			}
			if (!e) {
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
		}
		if (tookExisting) {
			// Do not free while vorbisfile still owns this pointer; a later
			// failure/destructor must see only the winner's live setup.
			donor->codec_setup = e->codecSetup;
			freeCodecSetup(codecSetup);
#ifdef VORBIS_SETUP_CACHE_TEST_HOOK
			++g_duplicateInsertFrees;
#endif
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
	static bool matches(const Entry &e, const Common::Array<byte> &setup, int channels, const int blocksizes[2]) {
		return e.channels == channels && e.blocksizes[0] == blocksizes[0] &&
			e.blocksizes[1] == blocksizes[1] && e.setup.size() == setup.size() &&
			!memcmp(e.setup.begin(), setup.begin(), setup.size());
	}
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

	// Never mutate the mutex after construction: doing so can make two
	// concurrent callers lock different mutexes.
	Common::MutexInternal *mutex() const { return _mutex; }

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
	HeaderPackets() : rewound(true), serial(0) {}
	Common::Array<byte> data[3];
	Common::Array<byte> initial;	///< bytes consumed by the speculative scan
	bool rewound;
	uint32 serial;
};

/**
 * Read the three header packets of the first logical stream, then go back
 * to where the stream was. False if they are not Vorbis headers or are not
 * all in the first kMaxHeaderBytes.
 */
bool readHeaderPackets(Common::SeekableReadStream *stream, HeaderPackets &hp) {
	const int64 start = stream->pos();
	// Verify the source can seek before scanning. Even if the final
	// speculative rewind fails, the retained bytes can be replayed.
	if (start < 0 || !stream->seek(0, SEEK_CUR) || stream->pos() != start)
		return false;
	ogg_sync_state oy;
	ogg_stream_state os;
	ogg_sync_init(&oy);
	hp.initial.reserve(8192);
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
			// Retain the exact read buffer for vorbisfile's initial input.
			const uint32 oldSize = hp.initial.size();
			hp.initial.resize(oldSize + n);
			memcpy(hp.initial.begin() + oldSize, buf, n);
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
	// The scan consumes at most hp.initial.size() bytes. On a rewind
	// failure replay exactly those bytes, then continue at the source's
	// current position. If the source is not there, do not fake an open.
	hp.rewound = stream->seek(start) && stream->pos() == start;
	if (!hp.rewound && stream->pos() != start + (int64)hp.initial.size())
		return false;
	return hp.rewound && !bad && got == 3 &&
		hp.data[0].size() >= 7 && !memcmp(hp.data[0].begin(), "\001vorbis", 7) &&
		hp.data[2].size() >= 7 && !memcmp(hp.data[2].begin(), "\005vorbis", 7);
}

} // End of anonymous namespace

/**
 * Give an opened stream a shared, expanded codec_setup for its own: the
 * cache's entry (to hand to release()) or nullptr if it keeps its own.
 */
static void *share(OggVorbis_File &vf, HeaderPackets &hp, bool &failedExpansion) {
	// The cache is only safe while its setup is lazy and has not had
	// decoding state built from it. Once vorbisfile has initialized DSP,
	// leave the stream untouched.
	if (vf.links != 1 || !ov_seekable(&vf) || vf.ready_state >= INITSET || !vf.vi || !vf.vi[0].codec_setup ||
	    !vf.serialnos || (uint32)vf.serialnos[0] != hp.serial)
		return nullptr;
	vorbis_info *vi = &vf.vi[0];
	codec_setup_info *ci = (codec_setup_info *)vi->codec_setup;
	// Headerin must have left the packed books intact. Eager or partial
	// expansion means this library cannot safely accept a shared setup.
	if (ci->books <= 0 || ci->books > 256 || ci->fullbooks)
		return nullptr;
	for (int i = 0; i < ci->books; ++i)
		if (!ci->book_param[i])
			return nullptr;
	const int blocksizes[2] = { vorbis_info_blocksize(vi, 0), vorbis_info_blocksize(vi, 1) };
	Entry *e = nullptr;
#ifdef VORBIS_SETUP_CACHE_TEST_HOOK
	// Deterministically exercise a second insert after the first lookup misses.
	if (!forceDuplicateInsert())
#endif
		e = cache().acquire(hp.data[2], vi->channels, blocksizes);
	if (!e) {
		// Expand the books in this stream's parsed setup, without allocating
		// throwaway DSP/PCM/mapping lookups merely to populate the cache.
		ci->fullbooks = (codebook *)calloc(ci->books, sizeof(*ci->fullbooks));
		if (!ci->fullbooks) {
			failedExpansion = true;
			return nullptr;
		}
		for (int i = 0; i < ci->books; ++i) {
			if (vorbis_book_init_decode(ci->fullbooks + i, ci->book_param[i])) {
				// vorbis_info_clear owns both the expanded and still-packed
				// books on this failure path; do not try a second expansion.
				failedExpansion = true;
				return nullptr;
			}
			vorbis_staticbook_destroy(ci->book_param[i]);
			ci->book_param[i] = nullptr;
		}
		e = cache().insert(hp.data[2], vi->channels, blocksizes, vi);
	} else {
		freeCodecSetup(vi->codec_setup);
	}
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
#ifdef VORBIS_SETUP_CACHE_TEST_HOOK
void forceDuplicateInsertOnce() {
	g_forceDuplicateInsert = true;
}
uint duplicateInsertFrees() {
	return g_duplicateInsertFrees;
}
#endif

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
	const int64 start = inStream->pos();
	VorbisSetupCache::HeaderPackets headers;
	const bool scan = VorbisSetupCache::cache().isEnabled() &&
		VorbisSetupCache::readHeaderPackets(inStream, headers);
	// Feed the exact scanned header bytes to vorbisfile rather than
	// rereading a possibly changing source; position it after that buffer.
	// The setup key then describes the bytes vorbisfile actually parsed.
	// The scan consumed exactly its retained prefix. Let vorbisfile read
	// these same bytes even when the rewind failed; a seekable source
	// remains seekable for the tail scan and later PCM seeks.
	const bool positioned =
		((scan && headers.rewound && inStream->seek(start + headers.initial.size()) &&
		  inStream->pos() == start + (int64)headers.initial.size()) ||
		 (!headers.rewound && !headers.initial.empty() &&
		  inStream->pos() == start + (int64)headers.initial.size()));
	if (scan && headers.rewound && !positioned)
		headers.rewound = inStream->seek(start) && inStream->pos() == start;
	const bool haveHeaders = positioned && scan && headers.rewound;
	if (!positioned && !headers.rewound) {
		// The rewind failed (twice, when the scan itself succeeded) and
		// there is no retained prefix to replay: whatever pos() says, a
		// failed reposition must not feed vorbisfile a misaligned source.
		memset(&_ovFile, 0, sizeof(_ovFile));
		_pos = _bufferEnd;
		return;
	}
	// If positioning failed after a successful rewind, replay is not
	// guaranteed; only the original offset is safe for an ordinary open.
	const char *initial = positioned ?
		(headers.initial.empty() ? nullptr : (const char *)headers.initial.begin()) : nullptr;
	const long ibytes = initial ? headers.initial.size() : 0;
	// A single failed speculative rewind does not mean a stream is
	// non-seekable. The initial buffer leaves it at the correct position,
	// so preserve the seek callback and let vorbisfile check later seeks.
	const ov_callbacks &callbacks = g_stream_wrap;
#else
	const char *initial = nullptr;
	const long ibytes = 0;
	const ov_callbacks &callbacks = g_stream_wrap;
#endif

	int res = ov_open_callbacks(inStream, &_ovFile, initial, ibytes, callbacks);
	if (res < 0) {
		warning("Could not create Vorbis stream (%d)", res);
		_pos = _bufferEnd;
		return;
	}

#ifdef VORBIS_SETUP_CACHE
	bool failedExpansion = false;
	if (haveHeaders)
		_sharedSetup = VorbisSetupCache::share(_ovFile, headers, failedExpansion);
	if (failedExpansion) {
		_pos = _bufferEnd;
		return;
	}
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
