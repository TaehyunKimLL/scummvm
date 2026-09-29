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

#include "scumm/debugsocket.h"

#include "common/file.h"
#include "common/system.h"
#include "graphics/surface.h"

#include "scumm/actor.h"
#include "scumm/scumm.h"

namespace Scumm {

DebugSocket::DebugSocket(ScummEngine *vm, GUI::DebugSocket *socket) :
	_vm(vm), _socket(socket), _loop(0), _frozen(false), _stringSeq(0), _textEpoch(0),
	_waitActive(false), _waitStarted(false), _waitFreeze(false), _waitDeadline(0), _waitTimeout(3600),
	_runPending(false), _runStarted(false), _runUntil(0), _progressMs(0) {
}

DebugSocket::~DebugSocket() {
}

// ---- mouse ---------------------------------------------------------------

// The factor ScummEngine::parseEvent() divides a mouse position by. Hercules
// (720x350, its own mapping) is left alone: coordinates pass unchanged.
int DebugSocket::mouseScale() const {
	if (_vm->_renderMode == Common::kRenderHercA || _vm->_renderMode == Common::kRenderHercG)
		return 1;
	if (_vm->_textSurfaceMultiplier == 2 || _vm->_macScreen || _vm->_renderMode == Common::kRenderCGA_BW || _vm->_enableEGADithering)
		return 2;
	return 1;
}

// `click`/`move` take game pixels: a harness's points are the same on a
// 320x200 screen and on one enlarged to 640x400 for hi-res text.
Common::Point DebugSocket::toBackend(const Common::Point &p) {
	const int m = mouseScale();
	return Common::Point(p.x * m, p.y * m);
}

Common::Point DebugSocket::fromBackend(const Common::Point &p) {
	const int m = mouseScale();
	return Common::Point(p.x / m, p.y / m);
}

// ---- strings -------------------------------------------------------------

void DebugSocket::noteString(int charset, const byte *raw, int len, const Common::U32String &text, const Common::Rect &rect) {
	DrawnString s;
	s.seq = _stringSeq++;
	s.loop = _loop + 1;	// the loop running now, as `OK <loop>` will count it
	s.charset = charset;
	s.utf8 = text.encode(Common::kUtf8);
	for (int i = 0; i < len; i++)
		s.raw.push_back(raw[i]);
	s.rect = rect;
	if (_strings.size() >= kRing)
		_strings.remove_at(0);
	_strings.push_back(s);
}

// ---- state ---------------------------------------------------------------

SocketSnapshot DebugSocket::snapshot() const {
	SocketSnapshot s;
	s.room = _vm->_currentRoom;
	// No one talking is 0xFF in v1-v7 and 0 in HE and v7+ after stopTalk().
	const int t = _vm->getTalkingActor();
	s.talking = (t == 0xFF) ? 0 : t;
	s.haveMsg = _vm->_haveMsg;
	s.userPut = _vm->_userPut;
	s.loop = _loop;
	return s;
}

static Common::String jsonString(const Common::String &s) {
	Common::String out("\"");
	for (uint i = 0; i < s.size(); i++) {
		const byte c = (byte)s[i];
		if (c == '"' || c == '\\') {
			out += '\\';
			out += (char)c;
		} else if (c == '\n') {
			out += "\\n";
		} else if (c < 0x20) {
			out += Common::String::format("\\u%04x", c);
		} else {
			out += (char)c;
		}
	}
	return out + "\"";
}

Common::String DebugSocket::stateJson() const {
	const SocketSnapshot s = snapshot();

	int egoActor = -1, egoX = -1, egoY = -1;
	if (_vm->VAR_EGO != 0xFF && _vm->_scummVars) {
		egoActor = _vm->_scummVars[_vm->VAR_EGO];
		if (_vm->isValidActor(egoActor)) {
			const Common::Point p = _vm->_actors[egoActor]->getRealPos();
			egoX = p.x;
			egoY = p.y;
		}
	}

	const int m = _vm->_textSurfaceMultiplier > 0 ? _vm->_textSurfaceMultiplier : 1;
	Common::String texts;
	for (uint i = 0; i < _strings.size(); i++) {
		const DrawnString &d = _strings[i];
		Common::String hex;
		for (uint k = 0; k < d.raw.size(); k++)
			hex += Common::String::format("%02x", d.raw[k]);
		// The text surface starts at the top of the screen, not the room.
		const int top = d.rect.top - _vm->_screenTop, bottom = d.rect.bottom - _vm->_screenTop;
		texts += Common::String::format("%s{\"charset\":%d,\"loop\":%u,\"text\":%s,\"hex\":\"%s\","
		                                "\"rect\":[%d,%d,%d,%d],\"game\":[%d,%d,%d,%d]}",
		                                i ? "," : "", d.charset, d.loop, jsonString(d.utf8).c_str(), hex.c_str(),
		                                d.rect.left * m, top * m, d.rect.right * m, bottom * m,
		                                d.rect.left, d.rect.top, d.rect.right, d.rect.bottom);
	}

	return Common::String::format(
		"{\"loop\":%u,\"room\":%d,\"ego\":{\"actor\":%d,\"x\":%d,\"y\":%d},\"talking\":%d,\"haveMsg\":%d,"
		"\"userPut\":%d,\"cursor\":{\"state\":%d,\"x\":%d,\"y\":%d},\"frozen\":%s,\"paused\":%s,\"texts\":[%s]}",
		s.loop, s.room, egoActor, egoX, egoY, s.talking, s.haveMsg, s.userPut,
		_vm->_cursor.state, _vm->_mouse.x, _vm->_mouse.y, _frozen ? "true" : "false", _vm->isPaused() ? "true" : "false", texts.c_str());
}

// ---- dump ----------------------------------------------------------------

namespace {

// Rows [0, h) of @p w pixels from @p s, starting at column @p x0.
bool writeSurface(const Common::String &path, const Graphics::Surface &s, int x0, int w, int h, const Common::String &format) {
	Common::DumpFile f;
	if (!f.open(Common::Path(path + ".bin")))
		return false;
	const int bpp = s.format.bytesPerPixel;
	for (int y = 0; y < h; y++)
		f.write(s.getBasePtr(x0, y), w * bpp);
	f.close();
	if (!f.open(Common::Path(path + ".txt")))
		return false;
	f.writeString(Common::String::format("%d %d %d %s\n", w, h, bpp, format.c_str()));
	f.close();
	return true;
}

Common::String formatName(const Graphics::PixelFormat &pf) {
	return pf.bytesPerPixel == 1 ? Common::String("CLUT8") : pf.toString();
}

} // End of anonymous namespace

Common::String DebugSocket::dumpBuffers(const Common::String &prefix) {
	// Main and verb virtual screens: the part on screen, xstart onwards.
	const VirtScreen &main = _vm->_virtscr[kMainVirtScreen];
	if (main.Graphics::Surface::getPixels() && !writeSurface(prefix + "_low", main, main.xstart, main.w, main.h, formatName(main.format)))
		return "FAIL " + prefix + "_low";
	const VirtScreen &verb = _vm->_virtscr[kVerbVirtScreen];
	if (verb.Graphics::Surface::getPixels() && verb.h > 0 && !writeSurface(prefix + "_vrb", verb, verb.xstart, verb.w, verb.h, formatName(verb.format)))
		return "FAIL " + prefix + "_vrb";

	// The text surface: with hi-res text, the overlay's index plane.
	const Graphics::Surface &layer = _vm->_overlay.index();
	if (layer.getPixels() && !writeSurface(prefix + "_layer", layer, 0, layer.w, layer.h, formatName(layer.format)))
		return "FAIL " + prefix + "_layer";
	if (_vm->_hiResText.alphaActive()) {
		const Graphics::Surface *cov = _vm->_overlay.coverage();
		if (cov && !writeSurface(prefix + "_cov", *cov, 0, cov->w, cov->h, formatName(cov->format)))
			return "FAIL " + prefix + "_cov";
	}

	Common::DumpFile f;
	if (!f.open(Common::Path(prefix + "_pal.bin")))
		return "FAIL " + prefix + "_pal";
	f.write(_vm->_currentPalette, sizeof(_vm->_currentPalette));
	f.close();
	if (!f.open(Common::Path(prefix + "_pal.txt")))
		return "FAIL " + prefix + "_pal";
	f.writeString("256 1 3 RGB888\n");	// 256 entries of R, G, B
	f.close();

	// What the backend shows (before its own cursor and scaler).
	Graphics::Surface *out = g_system->lockScreen();
	if (!out)
		return "FAIL " + prefix + "_out (lockScreen)";
	const bool ok = writeSurface(prefix + "_out", *out, 0, out->w, out->h, formatName(out->format));
	g_system->unlockScreen();
	if (!ok)
		return "FAIL " + prefix + "_out";
	return "OK";
}

// ---- wait / freeze / run -------------------------------------------------

void DebugSocket::finishWait(bool ok) {
	_waitActive = false;
	_waitStarted = false;
	if (ok && _waitFreeze)
		_frozen = true;
	_textEpoch = _stringSeq;
	_socket->reply(Common::String::format("%s %u", ok ? "OK" : "TIMEOUT", _loop));
}

void DebugSocket::poll() {
	if (endStalled())
		return;
	startPending();
}

// A wait or `run` counts loops, and a loop that never ends would keep the
// socket from reading anything, `key Return` for the dialog included. go()
// runs no loop while the engine is paused (a modal dialog: a failed load,
// the sound-settings warning, the GMM), so such a wait ends at once; a loop
// that has not ended for kStallMs ends it too.
bool DebugSocket::endStalled() {
	if (!_waitActive && !_runPending)
		return false;
	const char *why = socketStall(_vm->isPaused(), g_system->getMillis(), _progressMs, kStallMs);
	if (!why)
		return false;
	_waitActive = _waitStarted = false;
	_runPending = _runStarted = false;
	_runUntil = 0;
	_textEpoch = _stringSeq;
	_socket->reply(Common::String::format("TIMEOUT %u %s", _loop, why));
	return true;
}

void DebugSocket::startPending() {
	// Input sent while frozen reaches the game in the first loop after it:
	// unfreeze only once the socket's key queue is empty. A click is queued
	// when its command runs, so it is already in the event queue.
	if (_socket->inputPending())
		return;
	if (_waitActive && !_waitStarted) {
		_frozen = false;
		_waitStarted = true;
		if (_cond.kind == SocketCond::kLoops)
			_cond.value += _loop;
		_waitDeadline = _loop + _waitTimeout;
	}
	if (_runPending && !_runStarted) {
		_frozen = false;
		_textEpoch = _stringSeq;
		if (_runUntil) {
			_runStarted = true;
			_runUntil += _loop;
		} else {
			_runPending = false;
			_socket->reply(Common::String::format("OK %u", _loop));
		}
	}
}

void DebugSocket::loopDone() {
	_loop++;
	_progressMs = g_system->getMillis();

	if (_runStarted && _loop >= _runUntil) {
		_runStarted = false;
		_runPending = false;
		_runUntil = 0;
		_frozen = true;
		_textEpoch = _stringSeq;
		_socket->reply(Common::String::format("OK %u", _loop));
	}

	if (_waitActive && _waitStarted) {
		Common::Array<Common::String> texts;
		Common::Array<Common::Array<byte> > raws;
		for (uint i = 0; i < _strings.size(); i++) {
			if (_strings[i].seq >= _textEpoch) {
				texts.push_back(_strings[i].utf8);
				raws.push_back(_strings[i].raw);
			}
		}
		if (condHolds(_cond, snapshot(), texts, raws))
			finishWait(true);
		else if (_loop >= _waitDeadline)
			finishWait(false);
	}
}

bool DebugSocket::handle(const Common::String &cmd, const Common::StringArray &args, Common::String &out) {
	if (cmd == "state") {
		out = stateJson();
		return true;
	}
	if (cmd == "wait") {
		// `wait frames <n>` stays the generic one (screen updates, not loops).
		if (!args.empty() && args[0] == "frames")
			return false;
		SocketCond c;
		uint32 timeout = 3600;
		bool freeze = false;
		Common::String err;
		if (!parseSocketWait(args, c, timeout, freeze, err)) {
			out = "ERR " + err;
			return true;
		}
		_cond = c;
		_waitTimeout = timeout;
		_waitFreeze = freeze;
		_waitActive = true;
		_waitStarted = false;
		_progressMs = g_system->getMillis();
		if (!_frozen)
			startPending();		// starts now, unless keys are still queued
		return true;	// the reply comes from loopDone()
	}
	if (cmd == "freeze") {
		_frozen = true;
		_textEpoch = _stringSeq;
		out = Common::String::format("OK %u", _loop);
		return true;
	}
	if (cmd == "run") {
		int n = 0;
		if (args.size() > 1 || (args.size() == 1 && (!parseSocketNumber(args[0], n) || n == 0))) {
			out = "usage: run [loops > 0]";
			return true;
		}
		_runPending = true;
		_runStarted = false;
		_runUntil = (uint32)n;
		if (!_socket->inputPending() && !n) {
			// Nothing to wait for: answer here, not through reply().
			_runPending = false;
			_frozen = false;
			_textEpoch = _stringSeq;
			out = Common::String::format("OK %u", _loop);
		} else {
			_progressMs = g_system->getMillis();
			startPending();		// starts counting now, or once the keys are out
		}
		return true;
	}
	if (cmd == "dump") {
		if (args.size() != 1) {
			out = "usage: dump <prefix>";
			return true;
		}
		out = dumpBuffers(args[0]);
		return true;
	}
	return false;
}

} // End of namespace Scumm
