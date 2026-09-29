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

// MIDI output through an MPU-401 (or compatible) in UART mode on DOS, at
// the port given by BLASTER's P field (0x330 by default).

#define FORBIDDEN_SYMBOL_EXCEPTION_getenv
#define FORBIDDEN_SYMBOL_EXCEPTION_FILE
#define FORBIDDEN_SYMBOL_EXCEPTION_fopen
#define FORBIDDEN_SYMBOL_EXCEPTION_fwrite
#define FORBIDDEN_SYMBOL_EXCEPTION_fflush
#define FORBIDDEN_SYMBOL_EXCEPTION_fclose

#include "common/scummsys.h"

#if defined(DOS_DJGPP)

#include "audio/mpu401.h"
#include "audio/musicplugin.h"
#include "backends/platform/dos/blaster.h"
#include "backends/platform/dos/dos-silence.h"
#include "common/config-manager.h"
#include "common/error.h"
#include "common/events.h"
#include "common/mutex.h"
#include "common/system.h"
#include "common/textconsole.h"

#include <pc.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

/** Status port (base + 1) bits: clear means ready / available. */
const uint8 kStatusDRR = 0x40;	// Data Receive Ready: we may write
const uint8 kStatusDSR = 0x80;	// Data Set Ready: a byte waits to be read

const uint8 kCmdReset = 0xFF;
const uint8 kCmdUart = 0x3F;
const uint8 kAck = 0xFE;

/**
 * Status reads while waiting to send one byte. A read on the ISA bus takes
 * about 1 us, so this is ~10 ms on real hardware. The wait does not use a
 * clock, so it works with interrupts off and in the IRQ0 handler.
 */
const int kSendWaitReads = 10000;
/** Status reads while waiting for a command's ACK in open(): ~100 ms. */
const int kAckWaitReads = 100000;
/**
 * After this many messages in a row lost to a send timeout the device is
 * taken as gone: from then on a byte is sent only if the port is ready at
 * the first look, so a dead port cannot stall the timer interrupt. The
 * first byte that goes out clears it.
 */
const int kStallMessages = 4;

/**
 * The optional byte log (dos_midi_log=<path>): records are
 * [ms:4][ticks:4][len:2][bytes], ticks the number of timer callbacks so far
 * (bit 31 set if the message was sent from outside one, i.e. by the main
 * thread), len bit 15 set if the message was cut short by a send timeout.
 * The ring is allocated at open() and stays below 256 KB so the DOS heap
 * hands out locked memory for it.
 */
const uint32 kLogRingSize = 128 * 1024;	// power of two
const uint32 kLogFlushInterval = 1000;	// ms between flushes from the event loop

class MidiDriver_DosMPU;
MidiDriver_DosMPU *g_logDriver = nullptr;
bool g_atexitInstalled = false;
void flushLogAtExit();

class MidiDriver_DosMPU : public MidiDriver_MPU401, public Common::EventObserver {
public:
	explicit MidiDriver_DosMPU(uint16 base);
	~MidiDriver_DosMPU() override;

	int open() override;
	bool isOpen() const override { return _isOpen; }
	void close() override;
	void send(uint32 b) override;
	void sysEx(const byte *msg, uint16 length) override;

	bool notifyEvent(const Common::Event &event) override { return false; }
	void notifyPoll() override;

	void flushLog();

	void setTimerCallback(void *timerParam, Common::TimerManager::TimerProc timerProc) override;

private:
	static void countingTimerProc(void *driver);
	static void silence(void *driver);
	void drainInput();
	bool writeByte(uint8 value, int waitReads);
	bool writeCommand(uint8 cmd);
	bool sendBytes(const uint8 *bytes, uint32 len);
	void logMessage(const uint8 *bytes, uint32 len, bool cut);
	void openLog();
	void closeLog();

	uint16 _data;
	uint16 _status;
	bool _isOpen;

	// Everything below is touched by send()/sysEx(), which may run in the
	// IRQ0 handler: all of it lives in this (locked) object or the ring.
	Common::Mutex _mutex;
	int _stalledMessages;
	uint32 _lostMessages;
	uint32 _discardedInput;

	FILE *_logFile;
	uint8 *_ring;
	uint32 _head;	// bytes ever written (writer)
	uint32 _tail;	// bytes ever flushed (main thread)
	uint32 _logDropped;
	uint32 _lastFlush;
	bool _observing;

	// The client's timer proc, called through countingTimerProc so each
	// logged message carries the number of timer callbacks (the driver's
	// own clock, 100 Hz) it was sent in.
	void *_clientParam;
	Common::TimerManager::TimerProc _clientProc;
	volatile uint32 _ticks;
	volatile bool _inTimer;
};

MidiDriver_DosMPU::MidiDriver_DosMPU(uint16 base) : _data(base), _status(base + 1), _isOpen(false),
	_stalledMessages(0), _lostMessages(0), _discardedInput(0),
	_logFile(nullptr), _ring(nullptr), _head(0), _tail(0), _logDropped(0), _lastFlush(0), _observing(false),
	_clientParam(nullptr), _clientProc(nullptr), _ticks(0), _inTimer(false) {
}

void MidiDriver_DosMPU::setTimerCallback(void *timerParam, Common::TimerManager::TimerProc timerProc) {
	if (timerProc && !_clientProc) {
		_clientParam = timerParam;
		_clientProc = timerProc;
		MidiDriver_MPU401::setTimerCallback(this, &countingTimerProc);
	} else if (!timerProc) {
		MidiDriver_MPU401::setTimerCallback(nullptr, nullptr);
		_clientProc = nullptr;
	}
}

void MidiDriver_DosMPU::countingTimerProc(void *driver) {
	MidiDriver_DosMPU *d = (MidiDriver_DosMPU *)driver;
	++d->_ticks;
	d->_inTimer = true;
	if (d->_clientProc)
		d->_clientProc(d->_clientParam);
	d->_inTimer = false;
}

MidiDriver_DosMPU::~MidiDriver_DosMPU() {
	close();
}

void MidiDriver_DosMPU::drainInput() {
	// Incoming MIDI (and stray ACKs) is not used; read it so a card with a
	// full input buffer does not hold off output.
	for (int i = 0; i < 64 && !(inportb(_status) & kStatusDSR); ++i) {
		inportb(_data);
		++_discardedInput;
	}
}

bool MidiDriver_DosMPU::writeByte(uint8 value, int waitReads) {
	for (int i = 0; i < waitReads; ++i) {
		uint8 s = inportb(_status);
		if (!(s & kStatusDRR)) {
			outportb(_data, value);
			return true;
		}
		if (!(s & kStatusDSR)) {
			inportb(_data);
			++_discardedInput;
		}
	}
	return false;
}

bool MidiDriver_DosMPU::writeCommand(uint8 cmd) {
	bool ready = false;
	for (int i = 0; i < kAckWaitReads; ++i) {
		if (!(inportb(_status) & kStatusDRR)) {
			ready = true;
			break;
		}
	}
	if (!ready)
		return false;
	outportb(_status, cmd);
	// Wait for the ACK, skipping any MIDI data queued before it.
	for (int i = 0; i < kAckWaitReads; ++i) {
		if (!(inportb(_status) & kStatusDSR)) {
			if (inportb(_data) == kAck)
				return true;
		}
	}
	return false;
}

/**
 * Whether anything answers at @p base: the status port of an absent card
 * reads 0xFF (both "not ready" bits set, which a live MPU never keeps for
 * long), and a live one lets us write within the ACK wait.
 */
bool portPresent(uint16 base) {
	for (int i = 0; i < kAckWaitReads; ++i) {
		uint8 s = inportb(base + 1);
		if (s == 0xFF)
			return false;
		if (!(s & kStatusDRR))
			return true;
	}
	return false;
}

int MidiDriver_DosMPU::open() {
	if (_isOpen)
		return MERR_ALREADY_OPEN;

	if (!portPresent(_data)) {
		warning("MPU401: No MPU-401 at 0x%X", _data);
		return MERR_DEVICE_NOT_AVAILABLE;
	}

	drainInput();
	// An intelligent MPU ACKs both commands. A card already in UART mode
	// does not ACK the first reset, so it is tried twice; UART-only
	// interfaces (many clones, DOSBox's mpu401=uart) never ACK and are used
	// as they are once the port has shown to be alive.
	bool ack = writeCommand(kCmdReset) || writeCommand(kCmdReset);
	bool uartAck = writeCommand(kCmdUart);
	drainInput();

	openLog();
	_isOpen = true;
	// quit() and fatalError() exit without closing us.
	DOS::addSilencer(&silence, this);
	debug("MPU401: UART mode at 0x%X (%s)", _data,
	      ack && uartAck ? "reset and UART ACKed" : ack ? "reset ACKed, no UART ACK" : "no ACK, UART-only interface");
	return 0;
}

void MidiDriver_DosMPU::close() {
	if (!_isOpen)
		return;
	DOS::removeSilencer(&silence, this);
	// Stops the timer and sends All Notes Off on every channel.
	MidiDriver_MPU401::close();
	_clientProc = nullptr;
	_isOpen = false;
	// A reset takes the MPU out of UART mode for the next program.
	writeCommand(kCmdReset);
	if (_lostMessages)
		warning("MPU401: %u messages cut short by a send timeout", (unsigned)_lostMessages);
	closeLog();
}

/**
 * For an exit that skips close() (quit(), fatalError()), after the timer
 * is out: Sustain Off and All Notes Off on every channel, then the reset
 * that takes the MPU out of UART mode. Port I/O only; each byte gives up
 * after the usual send timeout.
 */
void MidiDriver_DosMPU::silence(void *driver) {
	MidiDriver_DosMPU *d = (MidiDriver_DosMPU *)driver;
	if (!d->_isOpen)
		return;
	for (uint8 ch = 0; ch < 16; ++ch) {
		const uint8 sustainOff[3] = { (uint8)(0xB0 | ch), 0x40, 0 };
		const uint8 notesOff[3] = { (uint8)(0xB0 | ch), 0x7B, 0 };
		d->sendBytes(sustainOff, 3);
		d->sendBytes(notesOff, 3);
	}
	d->writeCommand(kCmdReset);
}

bool MidiDriver_DosMPU::sendBytes(const uint8 *bytes, uint32 len) {
	int waitReads = _stalledMessages >= kStallMessages ? 1 : kSendWaitReads;
	for (uint32 i = 0; i < len; ++i) {
		if (!writeByte(bytes[i], waitReads)) {
			// Drop the rest of the message rather than hang the caller.
			if (_stalledMessages < kStallMessages)
				++_stalledMessages;
			++_lostMessages;
			return false;
		}
		_stalledMessages = 0;
		waitReads = kSendWaitReads;
	}
	return true;
}

void MidiDriver_DosMPU::send(uint32 b) {
	if (!_isOpen)
		return;
	uint8 bytes[3] = { (uint8)b, (uint8)(b >> 8), (uint8)(b >> 16) };
	uint32 len;
	switch (bytes[0] & 0xF0) {
	case 0xC0:
	case 0xD0:
		len = 2;
		break;
	case 0xF0:
		// System common/real-time: F1 and F3 carry one data byte, F2 two.
		len = bytes[0] == 0xF2 ? 3 : (bytes[0] == 0xF1 || bytes[0] == 0xF3) ? 2 : 1;
		break;
	default:
		len = 3;
		break;
	}
	// The mutex holds interrupts off on DOS, so a message from the main
	// thread is not split by one from the timer interrupt.
	Common::StackLock lock(_mutex);
	bool ok = sendBytes(bytes, len);
	logMessage(bytes, len, !ok);
}

void MidiDriver_DosMPU::sysEx(const byte *msg, uint16 length) {
	if (!_isOpen)
		return;
	static const uint8 start = 0xF0, end = 0xF7;
	Common::StackLock lock(_mutex);
	bool ok = sendBytes(&start, 1) && sendBytes(msg, length) && sendBytes(&end, 1);
	if (_ring) {
		// Log it as one message: F0 <msg> F7.
		uint32 need = 10 + length + 2;
		if (kLogRingSize - (_head - _tail) < need) {
			++_logDropped;
			return;
		}
		uint32 ms = g_system->getMillis();
		uint32 ticks = _ticks | (_inTimer ? 0 : 0x80000000u);
		uint32 len = (length + 2) | (ok ? 0 : 0x8000);
		const uint8 header[11] = { (uint8)ms, (uint8)(ms >> 8), (uint8)(ms >> 16), (uint8)(ms >> 24),
		                           (uint8)ticks, (uint8)(ticks >> 8), (uint8)(ticks >> 16), (uint8)(ticks >> 24),
		                           (uint8)len, (uint8)(len >> 8), start };
		for (uint32 i = 0; i < 11; ++i)
			_ring[(_head++) & (kLogRingSize - 1)] = header[i];
		for (uint32 i = 0; i < length; ++i)
			_ring[(_head++) & (kLogRingSize - 1)] = msg[i];
		_ring[(_head++) & (kLogRingSize - 1)] = end;
	}
}

// Called with _mutex held.
void MidiDriver_DosMPU::logMessage(const uint8 *bytes, uint32 len, bool cut) {
	if (!_ring)
		return;
	uint32 need = 10 + len;
	if (kLogRingSize - (_head - _tail) < need) {
		++_logDropped;
		return;
	}
	uint32 ms = g_system->getMillis();
	uint32 ticks = _ticks | (_inTimer ? 0 : 0x80000000u);
	uint32 l = len | (cut ? 0x8000 : 0);
	const uint8 header[10] = { (uint8)ms, (uint8)(ms >> 8), (uint8)(ms >> 16), (uint8)(ms >> 24),
	                           (uint8)ticks, (uint8)(ticks >> 8), (uint8)(ticks >> 16), (uint8)(ticks >> 24),
	                           (uint8)l, (uint8)(l >> 8) };
	for (uint32 i = 0; i < 10; ++i)
		_ring[(_head++) & (kLogRingSize - 1)] = header[i];
	for (uint32 i = 0; i < len; ++i)
		_ring[(_head++) & (kLogRingSize - 1)] = bytes[i];
}

void MidiDriver_DosMPU::openLog() {
	if (!ConfMan.hasKey("dos_midi_log"))
		return;
	Common::String path = ConfMan.get("dos_midi_log");
	if (path.empty())
		return;
	_logFile = fopen(path.c_str(), "w");
	if (!_logFile) {
		warning("MPU401: Cannot create the MIDI log %s", path.c_str());
		return;
	}
	_ring = new uint8[kLogRingSize];
	_head = _tail = 0;
	_logDropped = 0;
	_lastFlush = g_system->getMillis();
	g_logDriver = this;
	if (!g_atexitInstalled) {
		atexit(flushLogAtExit);
		g_atexitInstalled = true;
	}
	g_system->getEventManager()->getEventDispatcher()->registerObserver(this, 0, false, true);
	_observing = true;
}

void MidiDriver_DosMPU::closeLog() {
	if (_observing) {
		g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);
		_observing = false;
	}
	if (!_logFile)
		return;
	flushLog();
	fclose(_logFile);
	_logFile = nullptr;
	g_logDriver = nullptr;
	{
		Common::StackLock lock(_mutex);
		delete[] _ring;
		_ring = nullptr;
	}
}

void MidiDriver_DosMPU::notifyPoll() {
	uint32 now = g_system->getMillis();
	if (now - _lastFlush < kLogFlushInterval)
		return;
	_lastFlush = now;
	flushLog();
}

/**
 * Writes the records in the ring as "<ms> <hex bytes>[ !] @<ticks>[m]"
 * lines: "!" marks a message cut short by a send timeout; ticks is the
 * number of timer callbacks so far (the one sending included), "m" that the
 * message came from the main thread rather than a timer callback. Main
 * thread only: from the event loop, close() and atexit.
 */
void MidiDriver_DosMPU::flushLog() {
	if (!_logFile || !_ring)
		return;
	uint32 head, dropped;
	{
		Common::StackLock lock(_mutex);
		head = _head;
		dropped = _logDropped;
		_logDropped = 0;
	}
	// Records in [_tail, head) are complete; the writer leaves them alone
	// until _tail moves past them.
	char line[64];
	uint32 t = _tail;
	while (t != head) {
		uint32 ms = 0, ticks = 0, len = 0;
		for (int i = 0; i < 4; ++i)
			ms |= (uint32)_ring[(t++) & (kLogRingSize - 1)] << (8 * i);
		for (int i = 0; i < 4; ++i)
			ticks |= (uint32)_ring[(t++) & (kLogRingSize - 1)] << (8 * i);
		for (int i = 0; i < 2; ++i)
			len |= (uint32)_ring[(t++) & (kLogRingSize - 1)] << (8 * i);
		bool cut = (len & 0x8000) != 0;
		len &= 0x7FFF;
		int n = snprintf(line, sizeof(line), "%u", (unsigned)ms);
		fwrite(line, 1, n, _logFile);
		for (uint32 i = 0; i < len; ++i) {
			n = snprintf(line, sizeof(line), " %02X", _ring[(t++) & (kLogRingSize - 1)]);
			fwrite(line, 1, n, _logFile);
		}
		if (cut)
			fwrite(" !", 1, 2, _logFile);
		n = snprintf(line, sizeof(line), " @%u%s\n", (unsigned)(ticks & 0x7FFFFFFF),
		             (ticks & 0x80000000u) ? "m" : "");
		fwrite(line, 1, n, _logFile);
	}
	if (dropped) {
		int n = snprintf(line, sizeof(line), "# ring full, %u messages not logged\n", (unsigned)dropped);
		fwrite(line, 1, n, _logFile);
	}
	fflush(_logFile);
	Common::StackLock lock(_mutex);
	_tail = head;
}

void flushLogAtExit() {
	// exit() without close(): e.g. an error() while the music plays.
	if (g_logDriver)
		g_logDriver->flushLog();
}

} // End of anonymous namespace

class MPU401MusicPlugin : public MusicPluginObject {
public:
	const char *getName() const override {
		return "MPU-401 (DOS)";
	}

	const char *getId() const override {
		return "mpu401";
	}

	MusicDevices getDevices() const override;
	bool checkDevice(MidiDriver::DeviceHandle hdl, int checkFlags, bool quiet) const override;
	Common::Error createInstance(MidiDriver **mididriver, MidiDriver::DeviceHandle = 0) const override;
};

MusicDevices MPU401MusicPlugin::getDevices() const {
	MusicDevices devices;
	// Whether an MT-32 or a GM synth hangs off the port is the user's
	// choice (native_mt32), as for the other hardware MIDI plugins.
	devices.push_back(MusicDevice(this, "", MT_GM));
	return devices;
}

bool MPU401MusicPlugin::checkDevice(MidiDriver::DeviceHandle, int checkFlags, bool quiet) const {
	// Never picked by auto-detection: probing the port cannot tell an MT-32
	// from a GM synth, or a synth from nothing plugged into the MIDI out.
	if (checkFlags & MDCK_AUTO)
		return false;
	uint16 base = DOS::parseBlaster(getenv("BLASTER")).mpu;
	if (portPresent(base))
		return true;
	if (!quiet)
		warning("MPU401: No MPU-401 at 0x%X", base);
	return false;
}

Common::Error MPU401MusicPlugin::createInstance(MidiDriver **mididriver, MidiDriver::DeviceHandle) const {
	DOS::BlasterConfig blaster = DOS::parseBlaster(getenv("BLASTER"));
	*mididriver = new MidiDriver_DosMPU(blaster.mpu);
	return Common::kNoError;
}

//#if PLUGIN_ENABLED_DYNAMIC(MPU401)
	//REGISTER_PLUGIN_DYNAMIC(MPU401, PLUGIN_TYPE_MUSIC, MPU401MusicPlugin);
//#else
	REGISTER_PLUGIN_STATIC(MPU401, PLUGIN_TYPE_MUSIC, MPU401MusicPlugin);
//#endif

#endif // DOS_DJGPP
