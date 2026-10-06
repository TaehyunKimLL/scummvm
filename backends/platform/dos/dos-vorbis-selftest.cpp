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

#include "common/scummsys.h"

#include "backends/platform/dos/dos-vorbis-selftest.h"

#include "common/endian.h"
#include "common/file.h"
#include "common/substream.h"
#include "common/system.h"
#include "common/array.h"
#include "common/archive.h"
#include "common/config-manager.h"
#include "common/fs.h"

#ifdef USE_VORBIS
#include "audio/audiostream.h"
#include "audio/decoders/vorbis.h"
#include "backends/platform/dos/dos-irq.h"
#endif

namespace DOS {

#ifdef USE_VORBIS

namespace {

struct Clip {
	uint32 start, size;
};

bool readIndex(const Common::Path &name, Common::Array<Clip> &clips) {
	Common::File f;
	if (!f.open(name))
		return false;
	const uint32 n = f.readUint32BE();
	const uint32 size = (uint32)f.size();
	if (f.err() || n == 0 || n % 16 || n + 4 > size)
		return false;
	uint32 last = 0;
	for (uint32 i = 0; i < n; i += 16) {
		const uint32 org = f.readUint32BE();
		const uint32 newOff = f.readUint32BE();
		const uint32 tags = f.readUint32BE();
		Clip c;
		c.size = f.readUint32BE();
		const uint64 start = (uint64)newOff + n + 4 + tags;
		if ((i && org <= last) || start + c.size > size)
			return false;
		c.start = (uint32)start;
		last = org;
		clips.push_back(c);
	}
	return !f.err();
}

} // End of anonymous namespace

void vorbisSelftest(const Common::String &path, int requestedClips, int from) {
	if (requestedClips <= 0) {
		g_system->logMessage(LogMessageType::kInfo, "DOS: vorbis selftest: no clips requested\n");
		return;
	}
	uint wanted = requestedClips;
	// The file is found through SearchMan, as the engine finds MONKEY2.SOG.
	const Common::Path full(path, '/');
	const Common::Path name = full.getLastComponent();
	SearchMan.addDirectory("dosVorbisSelftest", Common::FSNode(full.getParent()), 0, 1);
	Common::Array<Clip> clips;
	if (!readIndex(name, clips) || clips.empty()) {
		SearchMan.remove("dosVorbisSelftest");
		g_system->logMessage(LogMessageType::kInfo,
			Common::String::format("DOS: vorbis selftest: cannot read the clip index of %s\n", path.c_str()).c_str());
		return;
	}
	if (!DOS::haveTsc()) {
		SearchMan.remove("dosVorbisSelftest");
		g_system->logMessage(LogMessageType::kInfo, "DOS: vorbis selftest: no TSC\n");
		return;
	}
	// TSC cycles per microsecond, from a 100 ms stretch of the millisecond clock
	const uint32 m0 = g_system->getMillis();
	while (g_system->getMillis() == m0)
		;
	const uint64 c0 = DOS::irqRdtsc();
	const uint32 m1 = g_system->getMillis();
	while (g_system->getMillis() - m1 < 100)
		;
	uint32 tscPerUs = (uint32)((DOS::irqRdtsc() - c0) / 100000);
	if (!tscPerUs)
		tscPerUs = 1;

	// The switch affects this DOS measurement only, never the game.
	const bool noCache = ConfMan.getBool("dos_vorbis_selftest_no_cache");
	Audio::VorbisSetupCache::setEnabled(!noCache);
	Audio::VorbisSetupCache::clear();
	if (from >= (int)clips.size())
		from = clips.size() - 1;
	if (wanted > clips.size() - (from < 0 ? 0 : from))
		wanted = clips.size() - (from < 0 ? 0 : from);
	uint done = 0, failed = 0, stereo = 0, rateMin = 0, rateMax = 0;
	uint64 samples = 0, decode = 0, openSum = 0, primeSum = 0;
	uint32 openMax = 0, primeMax = 0, openPrimeMax = 0;
	uint64 warmOpenSum = 0;
	uint32 coldOpen = 0, coldPrime = 0, coldOpenPrime = 0, warmOpenMax = 0, warmOpenPrimeMax = 0;
	uint warmCount = 0, coldCount = 0;
	static int16 buf[4096 * 2];
	for (uint i = 0; i < wanted; ++i) {
		const Clip &c = clips[from >= 0 ? from + i : (uint32)((uint64)i * clips.size() / wanted)];
		const Audio::VorbisSetupCache::Stats before = Audio::VorbisSetupCache::getStats();
		const uint64 t0 = DOS::irqRdtsc();
		Common::File *file = new Common::File;
		if (!file->open(name)) {
			delete file;
			++failed;
			continue;
		}
		Audio::SeekableAudioStream *s = Audio::makeVorbisStream(
			new Common::SeekableSubReadStream(file, c.start, c.start + c.size, DisposeAfterUse::YES),
			DisposeAfterUse::YES);
		const uint64 t1 = DOS::irqRdtsc();
		const Audio::VorbisSetupCache::Stats after = Audio::VorbisSetupCache::getStats();
		if (!s) {
			++failed;
			continue;
		}
		const int ch = s->isStereo() ? 2 : 1;
		const uint rate = s->getRate();
		int got = s->readBuffer(buf, 4096 * ch);
		const uint64 t2 = DOS::irqRdtsc();
		uint64 n = got;
		while (!s->endOfData() && (got = s->readBuffer(buf, 4096 * ch)) > 0)
			n += got;
		const uint64 t3 = DOS::irqRdtsc();
		delete s;
		++done;
		stereo += ch == 2;
		rateMin = !rateMin || rate < rateMin ? rate : rateMin;
		rateMax = rate > rateMax ? rate : rateMax;
		samples += n;
		decode += t3 - t1;
		openSum += t1 - t0;
		primeSum += t2 - t1;
		const uint32 openUs = (uint32)((t1 - t0) / tscPerUs), primeUs = (uint32)((t2 - t1) / tscPerUs);
		openMax = openUs > openMax ? openUs : openMax;
		primeMax = primeUs > primeMax ? primeUs : primeMax;
		openPrimeMax = openUs + primeUs > openPrimeMax ? openUs + primeUs : openPrimeMax;
		if (noCache || after.misses != before.misses || after.hits == before.hits) {
			++coldCount;
			coldOpen = MAX(coldOpen, openUs);
			coldPrime = MAX(coldPrime, primeUs);
			coldOpenPrime = MAX(coldOpenPrime, openUs + primeUs);
		} else {
			++warmCount;
			warmOpenSum += openUs;
			warmOpenMax = MAX(warmOpenMax, openUs);
			warmOpenPrimeMax = MAX(warmOpenPrimeMax, openUs + primeUs);
		}
	}
	SearchMan.remove("dosVorbisSelftest");
	// decode: kilocycles of the prime read + the rest (not the open); samples: all channels
	g_system->logMessage(LogMessageType::kInfo, Common::String::format(
		"DOS: vorbis selftest clips=%u stereo=%u rate=%u-%u samples=%u decode_kcyc=%u "
		"open_us=%u/%u prime_us=%u/%u openprime_max_us=%u tsc_per_us=%u "
		"cold_open_us=%u cold_prime_us=%u cold_openprime_us=%u "
		"warm_clips=%u warm_open_us=%u/%u warm_openprime_max_us=%u "
		"cold_clips=%u requested=%u failed=%u cache_off=%u\n",
		done, stereo, rateMin, rateMax, (uint)samples, (uint)(decode / 1000),
		done ? (uint)(openSum / tscPerUs / done) : 0, openMax,
		done ? (uint)(primeSum / tscPerUs / done) : 0, primeMax, openPrimeMax, tscPerUs,
		coldOpen, coldPrime, coldOpenPrime, warmCount,
		warmCount ? (uint)(warmOpenSum / warmCount) : 0, warmOpenMax, warmOpenPrimeMax,
		coldCount, wanted, failed, noCache ? 1u : 0u).c_str());
	Audio::VorbisSetupCache::setEnabled(true);
}

#else

void vorbisSelftest(const Common::String &, int, int) {
	g_system->logMessage(LogMessageType::kInfo, "DOS: vorbis selftest: this build has no Vorbis\n");
}

#endif

} // End of namespace DOS
