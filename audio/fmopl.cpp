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

#include "audio/fmopl.h"

#ifdef USE_RETROWAVE
	#include "audio/rwopl3.h"
#endif

#ifdef USE_NFM
	#include "audio/nfmopl.h"
#endif

#ifdef DOS_DJGPP
	#include "audio/dosopl.h"
#endif

#include "audio/softsynth/opl/dosbox.h"
#include "audio/softsynth/opl/mame.h"
#include "audio/softsynth/opl/nuked.h"

#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/func.h"
#include "common/mutex.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/translation.h"

namespace OPL {

// Factory functions

#ifdef USE_ALSA
namespace ALSA {
OPL *create(Config::OplType type);
} // End of namespace ALSA
#endif // USE_ALSA

#ifdef ENABLE_OPL2LPT
namespace OPL2LPT {
OPL *create(Config::OplType type);
} // End of namespace OPL2LPT
#endif // ENABLE_OPL2LPT

#ifdef USE_RETROWAVE
namespace RetroWaveOPL3 {
OPL *create(Config::OplType type);
} // End of namespace RetroWaveOPL3
#endif // ENABLE_RETROWAVE_OPL3

#ifdef DOS_DJGPP
namespace DosOPL {
OPL *create(Config::OplType type);
} // End of namespace DosOPL
#endif

#ifdef USE_NFM
namespace NfmOPL {
namespace RealChip {
OPL *create(Config::OplType type, enum NfmOPL::OplDevice dt);
} // End of namespace RealChip

namespace EmulatedChip {
OPL *create(Config::OplType type, enum NfmOPL::OplDevice dt);
} // End of namespace EmulatedChip
} // End of namespace NfmOPL
#endif

// Config implementation

enum OplEmulator {
	kNull = 0,
	kAuto = 1,
	kMame = 2,
	kDOSBox = 3,
	kALSA = 4,
	kNuked = 5,
	kOPL2LPT = 6,
	kOPL3LPT = 7,
	kRWOPL3 = 8
#ifdef USE_NFM
	,kNfmNokturnFM2 = 9,
	kNfmNokturnFM3 = 10,
	kNfmRWOpl3Express = 11,
	kNfmOPL2LPT = 12,
	kNfmOPL3LPT = 13,
	kNfmCeOPL2AudioBoard = 14,
	kNfmCeOPL3Duo = 15,
	kNfmStBusIsaVmeSb = 16,
	kNfmNatfeatsNull = 17,
	kNfmNukedOpl3 = 18
#endif
#ifdef DOS_DJGPP
	,kDosOPL = 19
#endif
};

OPL::OPL() {
	if (_hasInstance)
		error("There are multiple OPL output instances running");
	_hasInstance = true;
	_rhythmMode = false;
	_connectionFeedbackValues[0] = 0;
	_connectionFeedbackValues[1] = 0;
	_connectionFeedbackValues[2] = 0;
}

const Config::EmulatorDescription Config::_drivers[] = {
	{ "auto", "<default>", kAuto, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3 },
	{ "null", _s("None"), kNull, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3 },
#ifdef DOS_DJGPP
	// First, so that auto-detection prefers the real chip when there is one.
	{ "dosopl", _s("Hardware OPL (DOS)"), kDosOPL, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3 },
#endif
#ifndef DISABLE_MAME_OPL
	{ "mame", _s("MAME OPL emulator"), kMame, kFlagOpl2 },
#endif
#ifndef DISABLE_DOSBOX_OPL
	{ "db", _s("DOSBox OPL emulator"), kDOSBox, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3 },
#endif
#ifndef DISABLE_NUKED_OPL
	{ "nuked", _s("Nuked OPL emulator"), kNuked, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3 },
#endif
#ifdef USE_ALSA
	{ "alsa", _s("ALSA Direct FM"), kALSA, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3 },
#endif
#ifdef ENABLE_OPL2LPT
	{ "opl2lpt", _s("OPL2LPT"), kOPL2LPT, kFlagOpl2},
	{ "opl3lpt", _s("OPL3LPT"), kOPL3LPT, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3 },
#endif
#ifdef USE_RETROWAVE
	{"rwopl3", _s("RetroWave OPL3"), kRWOPL3, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
#endif
#ifdef USE_NFM
	{"nfm_nokturnfm2", _s("[nFM] NokturnFM2 (OPL2)"), kNfmNokturnFM2, kFlagOpl2 },
	{"nfm_nokturnfm3", _s("[nFM] NokturnFM3 (OPL3)"), kNfmNokturnFM3, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
	{"nfm_rwopl3", _s("[nFM] RetroWave USB OPL3 Express (OPL3)"), kNfmRWOpl3Express, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
	{"nfm_opl2lpt", _s("[nFM] Serdaco OPL2LPT (OPL2)"), kNfmOPL2LPT, kFlagOpl2 },
	{"nfm_opl3lpt", _s("[nFM] Serdaco OPL3LPT (OPL3)"), kNfmOPL3LPT, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
	{"nfm_ce_opl2ab", _s("[nFM] Cheerful Electronics OPL2 Audio Board (OPL2)"), kNfmCeOPL2AudioBoard, kFlagOpl2 },
	{"nfm_ce_opl3duo", _s("[nFM] Cheerful Electronics OPL3 Duo! (2xOPL3)"), kNfmCeOPL3Duo, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
	{"nfm_ce_stbus_isa_vme", _s("[nFM] ST Bus ISA / VME SoundBlaster"), kNfmStBusIsaVmeSb, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
	{"nfm_natfeats_null", _s("[nFM] NatFeats / NULL"), kNfmNatfeatsNull, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
	{"nfm_nuked_opl3", _s("[nFM] Nuked-OPL3 softsynth (OPL3)"), kNfmNukedOpl3, kFlagOpl2 | kFlagDualOpl2 | kFlagOpl3},
#endif
	{ nullptr, nullptr, 0, 0 }
};

Config::DriverId Config::parse(const Common::String &name) {
	for (int i = 0; _drivers[i].name; ++i) {
		if (name.equalsIgnoreCase(_drivers[i].name))
			return _drivers[i].id;
	}

	return -1;
}

const Config::EmulatorDescription *Config::findDriver(DriverId id) {
	for (int i = 0; _drivers[i].name; ++i) {
		if (_drivers[i].id == id)
			return &_drivers[i];
	}

	return nullptr;
}

Config::DriverId Config::detect(OplType type) {
	uint32 flags = 0;
	switch (type) {
	case kOpl2:
		flags = kFlagOpl2;
		break;

	case kDualOpl2:
		flags = kFlagDualOpl2;
		break;

	case kOpl3:
		flags = kFlagOpl3;
		break;

	default:
		break;
	}

	DriverId drv = parse(ConfMan.get("opl_driver"));
	if (drv == kAuto) {
		// Since the "auto" can be explicitly set for a game, and this
		// driver shows up in the GUI as "<default>", check if there is
		// a global setting for it before resorting to auto-detection.
		drv = parse(ConfMan.get("opl_driver", Common::ConfigManager::kApplicationDomain));
	}

	// When a valid driver is selected, check whether it supports
	// the requested OPL chip.
	if (drv != -1 && drv != kAuto) {
		const EmulatorDescription *driverDesc = findDriver(drv);
		// If the chip is supported, just use the driver.
		if (!driverDesc) {
			warning("The selected OPL driver %d could not be found", drv);
		} else if ((flags & driverDesc->flags)) {
			return drv;
		} else {
			// Else we will output a warning and just
			// return that no valid driver is found.
			warning("Your selected OPL driver \"%s\" does not support type %d emulation, which is requested by your game", driverDesc->description, type);
			return -1;
		}
	}

	// Detect the first matching emulator
	drv = -1;

#ifdef DOS_DJGPP
	// A chip that answers is used or nothing is: software emulation costs
	// too much CPU on the machines this port runs on. When the chip cannot
	// serve the type (Dual OPL2 or OPL3 on an OPL2 card), return no driver
	// so that the engine falls back to OPL2 if it can (SCI does). The price
	// is that an engine demanding an OPL3 without a fallback gets no music on
	// an OPL2 card. No emulator is built for DOS (configure).
	if (DosOPL::OPL::detect(kOpl2)) {
		if (DosOPL::OPL::detect(type))
			return kDosOPL;
		static bool warned = false;
		if (!warned) {
			warning("The hardware OPL cannot serve OPL type %d; not using an emulator", type);
			warned = true;
		}
		return -1;
	}
	// No chip, and no emulator is built for DOS: say so once (detect()
	// runs for every device query) and let the engine go without.
	static bool warnedNoChip = false;
	if (!warnedNoChip) {
		warning("No hardware OPL found: no AdLib/OPL music");
		warnedNoChip = true;
	}
	return -1;
#else
	for (int i = 2; _drivers[i].name; ++i) {
		if (_drivers[i].flags & flags) {
			drv = _drivers[i].id;
			break;
		}
	}

	return drv;
#endif
}

OPL *Config::create(OplType type) {
	return create(kAuto, type);
}

static OPL *createUnlogged(Config::DriverId driver, Config::OplType type) {
	// On invalid driver selection, we try to do some fallback detection
	if (driver == -1) {
		warning("Invalid OPL driver selected, trying to detect a fallback emulator");
		driver = kAuto;
	}

	// If autodetection is selected, we search for a matching
	// driver.
	if (driver == kAuto) {
		driver = Config::detect(type);

		// No emulator for the specified OPL chip could
		// be found, thus stop here.
		if (driver == -1) {
#ifndef DOS_DJGPP
			// (DOS: detect() has said why, once.)
			warning("No OPL emulator available for type %d", type);
#endif
			return nullptr;
		}
	}

	switch (driver) {
#ifndef DISABLE_MAME_OPL
	case kMame:
		if (type == Config::kOpl2)
			return new MAME::OPL();
		else
			warning("MAME OPL emulator only supports OPL2 emulation");
		return nullptr;
#endif

#ifndef DISABLE_DOSBOX_OPL
	case kDOSBox:
		return new DOSBox::OPL(type);
#endif

#ifndef DISABLE_NUKED_OPL
	case kNuked:
		return new NUKED::OPL(type);
#endif

#ifdef USE_ALSA
	case kALSA:
		return ALSA::create(type);
#endif

#ifdef ENABLE_OPL2LPT
	case kOPL2LPT:
		if (type == Config::kOpl2) {
			return OPL2LPT::create(type);
		}

		warning("OPL2LPT only supprts OPL2");
		return nullptr;
	case kOPL3LPT:
		return OPL2LPT::create(type);
#endif

#ifdef USE_RETROWAVE
	case kRWOPL3:
		return RetroWaveOPL3::create(type);
#endif

#ifdef DOS_DJGPP
	case kDosOPL:
		// Without a chip for this type, return nothing so that the engine
		// can fall back (e.g. from Dual OPL2 to OPL2).
		if (!DosOPL::OPL::detect(type)) {
			warning("No hardware OPL for type %d", type);
			return nullptr;
		}
		return DosOPL::create(type);
#endif

#ifdef USE_NFM
	case kNfmNokturnFM2: {
		if (type == Config::kOpl2) {
			return NfmOPL::RealChip::create(type, NfmOPL::dtNokturnFM2);
		}
		warning("NokturnFM2 supports only OPL2");
		return nullptr;
	};

	case kNfmNokturnFM3:
		return NfmOPL::RealChip::create(type, NfmOPL::dtNokturnFM3);

	case kNfmRWOpl3Express:
		return NfmOPL::RealChip::create(type, NfmOPL::dtRWOpl3Express);

	case kNfmOPL2LPT: {
		if (type == Config::kOpl2) {
			return NfmOPL::RealChip::create(type, NfmOPL::dtOPL2LPT);
		}
		warning("OPL2LPT supports only OPL2");
		return nullptr;
	};

	case kNfmOPL3LPT:
		return NfmOPL::RealChip::create(type, NfmOPL::dtOPL3LPT);

	case kNfmCeOPL2AudioBoard: {
		if (type == Config::kOpl2) {
			return NfmOPL::RealChip::create(type, NfmOPL::dtOPL2AudioBoard);
		}

		warning("OPL2 Audio Board supports only OPL2");
		return nullptr;
	};

	case kNfmCeOPL3Duo:
		return NfmOPL::RealChip::create(type, NfmOPL::dtOPL3Duo);
	case kNfmStBusIsaVmeSb:
		return NfmOPL::RealChip::create(type, NfmOPL::dtStBusIsaVmeSb);
	case kNfmNatfeatsNull:
		return NfmOPL::RealChip::create(type, NfmOPL::dtNatfeatsOpl);
	case kNfmNukedOpl3:
		return NfmOPL::EmulatedChip::create(type, NfmOPL::dtNukedOpl3);
#endif

	case kNull:
		return new NullOPL();

	default:
		warning("Unsupported OPL emulator %d", driver);
		return nullptr;
	}
}

/**
 * Wraps the OPL that Config::create would return and logs every register
 * write, for comparing the register streams of two builds (e.g. a DOS
 * hardware OPL against a Linux emulator). Enabled by the config key
 * opl_log=<path>, it writes two files:
 *
 *  - <path>.REG: every write, "<reg> <val>" (hex, reg 000-1FF).
 *  - <path>.ON:  every key-on, i.e. a write to B0-B8 (either bank) that
 *                sets bit 5 while it was clear, "<ms> <reg> <val> <n> <t|m>"
 *                with ms = OSystem::getMillis() at the write, n the number
 *                of driver timer callbacks since start() (counting the one
 *                running), and t if the write came from inside a callback,
 *                m if not (the main thread). With ms and n a key-on can be
 *                placed against the driver's own clock: callback n is due
 *                (n - 1) callback periods after the first.
 *
 * Writes may come from a timer callback, which on some ports (DOS) runs in
 * an interrupt handler, so the write path only stores into a ring buffer
 * allocated up front, under a mutex. The main thread empties it into the
 * files from the event loop (an EventObserver poll, at most every
 * kFlushInterval ms) and once more when the OPL is destroyed. Writes that
 * find the ring full are dropped and counted (reported by warning()), so
 * the ring must hold what arrives between two event polls.
 *
 * On DOS the files have 8.3 names, so the last component of <path> must be
 * at most 8 characters without an extension. The periodic flushes reach
 * the file, but an emulator (DOSBox) may hold host writes until the file
 * is closed: the logs are complete only after a clean quit.
 */
class LoggingOPL : public OPL, public Common::EventObserver {
public:
	static OPL *create(Config::DriverId driver, Config::OplType type, const Common::Path &path);
	~LoggingOPL() override;

	bool init() override { return _inner->init(); }
	void reset() override { _inner->reset(); }
	void write(int a, int v) override;
	void writeReg(int r, int v) override;

	void setCallbackFrequency(int timerFrequency) override { _inner->setCallbackFrequency(timerFrequency); }

	bool notifyEvent(const Common::Event &event) override { return false; }
	void notifyPoll() override;

protected:
	void startCallbacks(int timerFrequency) override {
		_callbacks = 0;
		_inner->start(new Common::Functor0Mem<void, LoggingOPL>(this, &LoggingOPL::onInnerTimer), timerFrequency);
	}
	void stopCallbacks() override { _inner->stop(); }

private:
	struct Entry {
		uint32 ms;
		uint32 callbacks;
		uint16 reg;
		uint8 value;
		uint8 flags;	// kKeyOn, kInCallback
	};

	enum {
		kKeyOn = 1,
		kInCallback = 2
	};

	enum {
		// 16384 entries of 12 bytes = 192 KB. On DOS, log() runs in the
		// timer's interrupt handler, and only heap blocks under 256 KB stay
		// locked (backends/platform/dos/dos-heap.cpp); a larger ring would
		// be pageable and fault there. A flush every second leaves ample
		// room: SCI music writes a few hundred registers a second.
		kRingSize = 16384,
		kFlushInterval = 1000
	};

	explicit LoggingOPL(Config::OplType type);

	void onInnerTimer() {
		++_callbacks;
		++_inCallback;
		if (_callback && _callback->isValid())
			(*_callback)();
		--_inCallback;
	}
	void log(int reg, int value);
	void flush();

	OPL *_inner;
	Config::OplType _type;
	int _activeReg;
	uint8 _shadow[0x200];
	volatile uint32 _callbacks;	// driver callbacks since start()
	volatile int _inCallback;

	Common::Mutex _mutex;
	Entry *_ring;
	uint32 _head;	// next slot to fill (writer)
	uint32 _tail;	// next slot to flush (main thread)
	uint32 _dropped;
	uint32 _lastFlush;

	Common::DumpFile _onFile;
	Common::DumpFile _regFile;
	bool _observing;
};

LoggingOPL::LoggingOPL(Config::OplType type) : _inner(nullptr), _type(type), _activeReg(0),
	_callbacks(0), _inCallback(0),
	_ring(nullptr), _head(0), _tail(0), _dropped(0), _lastFlush(0), _observing(false) {
	memset(_shadow, 0, sizeof(_shadow));
}

OPL *LoggingOPL::create(Config::DriverId driver, Config::OplType type, const Common::Path &path) {
	LoggingOPL *opl = new LoggingOPL(type);

	// The wrapper and the wrapped driver are one OPL instance.
	_hasInstance = false;
	opl->_inner = createUnlogged(driver, type);
	if (!opl->_inner) {
		delete opl;
		return nullptr;
	}

	if (!opl->_onFile.open(path.append(".ON")) || !opl->_regFile.open(path.append(".REG"))) {
		// Log nothing; an .ON that did open stays behind, empty (there is
		// no portable way to remove it).
		warning("OPL log: cannot create %s.ON/.REG", path.toString(Common::Path::kNativeSeparator).c_str());
		opl->_onFile.close();
		opl->_regFile.close();
		return opl;
	}
	opl->_ring = new Entry[kRingSize];
	opl->_lastFlush = g_system->getMillis();
	g_system->getEventManager()->getEventDispatcher()->registerObserver(opl, 0, false, true);
	opl->_observing = true;
	return opl;
}

LoggingOPL::~LoggingOPL() {
	if (_observing)
		g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);
	// Deleting the driver stops its callbacks; then nothing writes any more.
	delete _inner;
	if (_ring) {
		flush();
		_onFile.finalize();
		_regFile.finalize();
		delete[] _ring;
	}
}

void LoggingOPL::write(int a, int v) {
	if (a & 1) {
		log(_activeReg, v);
	} else {
		// The register as the chip drivers decode it.
		if (_type == Config::kOpl2)
			_activeReg = v & 0xFF;
		else
			_activeReg = ((a & 2) << 7) | (v & 0xFF);
	}
	_inner->write(a, v);
}

void LoggingOPL::writeReg(int r, int v) {
	log(r, v);
	_inner->writeReg(r, v);
}

void LoggingOPL::log(int reg, int value) {
	if (!_ring)
		return;

	reg &= 0x1FF;
	value &= 0xFF;
	Common::StackLock lock(_mutex);

	int low = reg & 0xFF;
	bool keyOn = low >= 0xB0 && low <= 0xB8 && (value & 0x20) && !(_shadow[reg] & 0x20);
	_shadow[reg] = (uint8)value;

	uint32 next = (_head + 1) % kRingSize;
	if (next == _tail) {
		++_dropped;
		return;
	}
	Entry &e = _ring[_head];
	e.ms = g_system->getMillis();
	e.callbacks = _callbacks;
	e.reg = (uint16)reg;
	e.value = (uint8)value;
	e.flags = (keyOn ? kKeyOn : 0) | (_inCallback ? kInCallback : 0);
	_head = next;
}

void LoggingOPL::notifyPoll() {
	uint32 now = g_system->getMillis();
	if (now - _lastFlush < kFlushInterval)
		return;
	_lastFlush = now;
	flush();
}

void LoggingOPL::flush() {
	uint32 head, dropped;
	{
		Common::StackLock lock(_mutex);
		head = _head;
		dropped = _dropped;
		_dropped = 0;
	}
	if (dropped)
		warning("OPL log: ring full, %u writes not logged", (unsigned)dropped);
	if (head == _tail)
		return;

	// Entries [_tail, head) are complete and the writer does not touch them
	// until _tail moves past them.
	Common::String on, reg;
	for (uint32 i = _tail; i != head; i = (i + 1) % kRingSize) {
		const Entry &e = _ring[i];
		reg += Common::String::format("%03X %02X\n", e.reg, e.value);
		if (e.flags & kKeyOn)
			on += Common::String::format("%u %03X %02X %u %c\n", (unsigned)e.ms, e.reg, e.value,
			                             (unsigned)e.callbacks, (e.flags & kInCallback) ? 't' : 'm');
	}
	_regFile.writeString(reg);
	_onFile.writeString(on);
	_regFile.flush();
	_onFile.flush();

	Common::StackLock lock(_mutex);
	_tail = head;
}

OPL *Config::create(DriverId driver, OplType type) {
	if (ConfMan.hasKey("opl_log")) {
		Common::Path path = ConfMan.getPath("opl_log");
		if (!path.empty())
			return LoggingOPL::create(driver, type, path);
	}
	return createUnlogged(driver, type);
}

void OPL::initDualOpl2OnOpl3(Config::OplType oplType) {
	if (oplType != Config::OplType::kDualOpl2)
		return;

	// Enable OPL3 mode.
	writeReg(0x105, 1);

	// Set panning for channels 0-8 and 9-17 to right and left, respectively.
	for (int i = 0; i <= 0x100; i += 0x100) {
		for (int j = 0xC0; j <= 0xC8; j++) {
			writeReg(i | j, i == 0 ? 0x20 : 0x10);
		}
	}
}

bool OPL::emulateDualOpl2OnOpl3(int r, int v, Config::OplType oplType) {
	if (oplType != Config::OplType::kDualOpl2)
		return true;

	// Prevent writes to the following registers of the second set:
	// - 01 - Test register. Setting any bit here will disable output.
	// - 04 - Connection select. This is used to enable 4 operator instruments,
	//        which are not used for dual OPL2.
	// - 05 - New. Only allow writes which set bit 0 to 1, which enables OPL3
	//        features.
	if (r == 0x101 || r == 0x104 || (r == 0x105 && ((v & 1) == 0)))
		return false;

	// Clear bit 2 of waveform select register writes. This will prevent
	// selection of OPL3-specific waveforms, which are not used for dual OPL2.
	if ((r & 0xFF) >= 0xE0 && (r & 0xFF) <= 0xF5 && ((v & 4) > 0)) {
		writeReg(r, v & ~4);
		return false;
	}

	// Handle rhythm mode register writes.
	if ((r & 0xFF) == 0xBD) {
		// Check if rhythm mode is enabled or disabled.
		bool newRhythmMode = (v & 0x20) > 0;
		if (newRhythmMode != _rhythmMode) {
			_rhythmMode = newRhythmMode;
			// Set panning for channels 6-8 (used by rhythm mode instruments)
			// to center or right if rhythm mode is enabled or disabled,
			// respectively.
			writeReg(0xC6, (_rhythmMode ? 0x30 : 0x20) | _connectionFeedbackValues[0]);
			writeReg(0xC7, (_rhythmMode ? 0x30 : 0x20) | _connectionFeedbackValues[1]);
			writeReg(0xC8, (_rhythmMode ? 0x30 : 0x20) | _connectionFeedbackValues[2]);
		}
		if (r == 0x1BD) {
			// Send writes to the rhythm mode register on the 2nd OPL2 to the
			// single rhythm mode register on the OPL3.
			writeReg(0xBD, v);
			return false;
		}
	}

	// Keep track of the connection and feedback values set for channels 6-8.
	// This is necessary for handling rhythm mode panning (see above).
	if (r >= 0xC6 && r <= 0xC8) {
		_connectionFeedbackValues[r - 0xC6] = v & 0xF;
	}

	// Add panning bits to writes to the connection/feedback registers.
	if ((r & 0xFF) >= 0xC0 && (r & 0xFF) <= 0xC8) {
		// Add right or left panning for the first or second OPL2, respectively.
		int newValue = (r < 0x100 ? 0x20 : 0x10) | (v & 0xF);
		if (_rhythmMode && r >= 0xC6 && r <= 0xC8) {
			// If rhythm mode is enabled, pan channels 6-8 center.
			newValue = 0x30 | (v & 0xF);
		}
		if (v == newValue) {
			// Panning bits are already correct.
			return true;
		} else {
			// Write the new value with the correct panning bits instead.
			writeReg(r, newValue);
			return false;
		}
	}

	// Any other register writes can be processed normally.
	return true;
}

bool OPL::_hasInstance = false;

} // End of namespace OPL
