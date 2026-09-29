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

#ifndef SCUMM_DEBUGSOCKET_H
#define SCUMM_DEBUGSOCKET_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/str-array.h"
#include "common/ustr.h"
#include "gui/debugsocket.h"
#include "scumm/debugsocket_cond.h"

namespace Scumm {

class ScummEngine;

/**
 * SCUMM's commands on the debug socket (GUI::DebugSocket, gui/debugsocket.h).
 *
 * The transport, `key`/`type`/`save`/`load`/`shot`/`record` and every console
 * command (`room`, `actor`, ...) are the engine-neutral ones. Added here:
 *
 *   state                 one line of JSON: loop (scummLoop() calls so far),
 *                         room, ego, talking (actor, 0 for none), haveMsg,
 *                         userPut, cursor, frozen, and the last 8 strings
 *                         drawn (charset, UTF-8 text, the game's bytes in
 *                         hex, the rectangle on the text surface and in game
 *                         pixels, the loop it was drawn in)
 *   wait <cond> [timeout N] [freeze]
 *                         reply `OK <loop>` at the end of the first loop the
 *                         condition holds in, `TIMEOUT <loop>` after N loops
 *                         (default 3600). Conditions: see parseSocketWait().
 *                         `TIMEOUT <loop> paused` at once while the engine is
 *                         paused (a modal dialog: go() runs no loop), and
 *                         `TIMEOUT <loop> stalled` when no loop has ended for
 *                         60 s; the socket reads commands again then, so a
 *                         `key Return` can close the dialog. `run` alike.
 *                         `text`/`seen` look at the strings drawn since the
 *                         game last stopped or started under the socket's
 *                         control (the end of a wait, `freeze`, the start and
 *                         the end of `run`; before any of those, every string
 *                         in the ring), so a string drawn before the input
 *                         that leads to the next one does not answer it. With `freeze` the game
 *                         stays frozen after that loop. Sent while frozen, a
 *                         wait first lets the queued input out, then runs
 *                         the game until the condition holds.
 *   freeze                stop running scummLoop(): go() only waits for the
 *                         timer (events, screen, the socket). `OK <loop>`
 *   run [N]               once the queued keys are out, unfreeze; with N,
 *                         freeze again after N loops. `OK <loop>` then.
 *                         SCUMM keeps one pending key, so send one key per run.
 *   dump <prefix>         the engine's buffers, each with a <file>.txt of
 *                         "w h bytes-per-pixel format":
 *                           <prefix>_low.bin    main virtual screen, visible part
 *                           <prefix>_vrb.bin    verb virtual screen
 *                           <prefix>_layer.bin  text surface (hi-res: the index plane)
 *                           <prefix>_cov.bin    coverage plane (alpha only)
 *                           <prefix>_pal.bin    the game palette, 768 bytes
 *                                               (.txt: "256 1 3 RGB888")
 *                           <prefix>_out.bin    the backend screen (lockScreen())
 *                         `FAIL <file>` when one cannot be written. Keep the
 *                         prefix's file name to 2 characters for 8.3 names.
 *   click/move <x> <y>    in game pixels (320x200 for v5), also when the
 *                         screen is enlarged for hi-res text
 *
 * Why freeze: SCUMM's simulation advances per loop. A frozen game dumped on
 * two machines at the same loop, with the same input in the same loops,
 * gives the same bytes.
 */
class DebugSocket : public GUI::DebugSocketExtension {
public:
	DebugSocket(ScummEngine *vm, GUI::DebugSocket *socket);
	~DebugSocket() override;

	// GUI::DebugSocketExtension
	bool handle(const Common::String &cmd, const Common::StringArray &args, Common::String &reply) override;
	bool replyPending() const override { return _waitActive || _runPending; }
	void poll() override;
	Common::Point toBackend(const Common::Point &p) override;
	Common::Point fromBackend(const Common::Point &p) override;
	Common::String recordState() override { return stateJson(); }

	/** go(): at the end of each scummLoop(). Counts it, checks the wait. */
	void loopDone();

	/** go(): while true, scummLoop() is skipped. */
	bool frozen() const { return _frozen; }

	/**
	 * The engine drew a string: @p raw is the game's bytes of the characters
	 * drawn (a line break as 0x0A), @p text the same decoded, @p rect where
	 * it went, in game pixels.
	 */
	void noteString(int charset, const byte *raw, int len, const Common::U32String &text, const Common::Rect &rect);

	SocketSnapshot snapshot() const;

private:
	struct DrawnString {
		uint32 seq;			///< _stringSeq when noted
		uint32 loop;
		int charset;
		Common::String utf8;
		Common::Array<byte> raw;
		Common::Rect rect;	///< game pixels
	};

	Common::String stateJson() const;
	Common::String dumpBuffers(const Common::String &prefix);
	/** The game-to-backend factor of mouse coordinates (input.cpp's rule). */
	int mouseScale() const;
	void finishWait(bool ok);
	/** End a wait or `run` no loop will finish; true when it replied. */
	bool endStalled();
	/** Start a wait or `run` that was held for queued keys. Never replies for a wait. */
	void startPending();

	ScummEngine *_vm;
	GUI::DebugSocket *_socket;

	uint32 _loop;
	bool _frozen;

	static const uint kRing = 8;
	Common::Array<DrawnString> _strings;	///< oldest first, at most kRing
	uint32 _stringSeq;		///< strings noted so far
	uint32 _textEpoch;		///< `text`/`seen` see strings with seq >= this (see above)

	bool _waitActive;
	bool _waitStarted;		///< the wait's loop count is running (not held by a freeze)
	bool _waitFreeze;
	SocketCond _cond;
	uint32 _waitDeadline;	///< loop count; set when the wait starts running
	uint32 _waitTimeout;

	bool _runPending;		///< `run`: waiting for the input queue to drain
	bool _runStarted;		///< unfrozen; counting to _runUntil
	uint32 _runUntil;		///< 0: no refreeze

	static const uint32 kStallMs = 60000;
	uint32 _progressMs;		///< getMillis() at the last loop end, or when the wait/run came
};

} // End of namespace Scumm

#endif
