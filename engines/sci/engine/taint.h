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

// MEASUREMENT PROBE (card M4) -- temporary, removed in the follow-up commit.
//
// Dynamic taint tracking for SCI: mark every byte range whose content came
// from a resource that a fan translation would replace (TEXT resources,
// MESSAGE records), then report, for every script-visible byte operation,
// whether its operand range intersects a tainted range.
//
// Unconditional: no getenv, no debug-flag gate. Report is written to
// /tmp/sci_taint_report.txt periodically and at engine teardown.

#ifndef SCI_ENGINE_TAINT_H
#define SCI_ENGINE_TAINT_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/str.h"
#include "sci/engine/vm_types.h"

namespace Sci {

class SegManager;

enum TaintVerdict {
	TV_TAINTED   = 0,	///< operand range intersects resource-derived text
	TV_SCRIPTLIT = 1,	///< operand lives in a SCRIPT segment (script-embedded literal), untainted
	TV_CLEAN     = 2,	///< script-built memory, untainted
	TV_IMMEDIATE = 3,	///< operand is not a pointer (segment 0): a number or a text-resource id
	TV_UNKNOWN   = 4	///< pointer could not be resolved
};

class TaintTracker {
public:
	TaintTracker();

	// --- sources ---------------------------------------------------------
	/// A TEXT/MESSAGE resource lookup happened: count the bytes that entered the engine.
	void noteResourceText(const char *origin, uint32 nbytes);
	/// Mark [ptr, ptr+len) as resource-derived text.
	void taint(SegManager *segMan, reg_t ptr, uint32 len, const char *origin, const char *sample);
	/// Clear taint over [ptr, ptr+len) (a clean value overwrote it).
	void untaint(reg_t ptr, uint32 len);

	// --- queries / sinks -------------------------------------------------
	TaintVerdict classify(SegManager *segMan, reg_t ptr, uint32 len) const;
	/// classify() plus a re-read of the bytes: TAINTED only survives if the
	/// intersecting range's recorded head still matches what is in memory.
	TaintVerdict classifyVerified(SegManager *segMan, reg_t ptr, uint32 len, bool *stale) const;
	/// Record one sink hit. `len` 0 means "whole string from ptr".
	TaintVerdict sink(SegManager *segMan, const char *sinkName, reg_t ptr, uint32 len, const char *note);
	/// Sink that takes no pointer operand at all (e.g. an immediate).
	void sinkVerdict(const char *sinkName, TaintVerdict v, const char *note);
	/// Attribute a TAINTED hit to the calling script/object/method.
	void noteOrigin(const char *sinkName, TaintVerdict v, const Common::String &origin);

	/// What is the script COMPARING tainted text against, and how long is the
	/// compare? A one-character needle means a scan (word wrap / tokenising);
	/// a word-length needle means the script is matching dialogue content.
	void noteCompare(const Common::String &needle, int n, bool haystackTainted,
	                 const Common::String &origin);
	/// Offsets a script asked kStrAt for, per origin: a 0,1,2,... walk is a
	/// scan; scattered offsets are indexing.
	void noteStrAtOffset(const Common::String &origin, uint32 offset, bool tainted);

	/// Raw VM variable read/write (lag/lal/lsg/sag/sal): global/local slot.
	void vmVar(SegManager *segMan, bool isWrite, int varType, int index, uint16 seg, uint32 byteOff);

	void setPhase(const char *phase);
	void noteRoom(int room);
	int roomCount() const { return (int)_roomsSeen.size(); }

	/// The VM stack is reused: taint at or above the live stack pointer is
	/// stale and must be dropped, else a dead frame's text produces false
	/// TAINTED verdicts for an unrelated later buffer.
	void pruneStack(uint16 stackSeg, uint32 liveBytes);
	uint64 stalePrunes() const { return _stalePrunes; }
	uint64 staleRejects() const { return _staleRejects; }

	void report(bool final_);

private:
	// Each tainted range remembers the first bytes that were written into it,
	// so a sink hit can be CONFIRMED by re-reading memory: the VM stack is
	// recycled, and without this check a dead frame's dialogue would make an
	// unrelated later buffer read as TAINTED.
	struct Range { uint32 lo, hi; char head[24]; };
	typedef Common::Array<Range> RangeList;
	Common::HashMap<uint32, RangeList> _ranges;	// segment -> ranges

	struct Counts { uint64 n[5]; Counts() { for (int i = 0; i < 5; i++) n[i] = 0; } };
	Common::HashMap<Common::String, Counts> _sinks;			// sink name -> verdict counts
	Common::HashMap<Common::String, Counts> _sinkByPhase;	// "sink|phase" -> verdict counts
	Common::Array<Common::String> _exemplars;
	Common::HashMap<Common::String, uint32> _exemplarSeen;

	Common::HashMap<Common::String, uint64> _sourceEvents;
	Common::HashMap<Common::String, uint64> _sourceBytes;
	Common::HashMap<Common::String, uint64> _resBytes;
	Common::HashMap<Common::String, uint64> _resReads;
	/// sink -> set of distinct tainted strings it touched (value = hit count).
	Common::HashMap<Common::String, uint64> _taintedStrings;
	/// "sink :: script N, object::method" -> hits, for TAINTED hits only.
	Common::HashMap<Common::String, uint64> _taintedOrigins;
	Common::HashMap<uint32, uint64> _roomsSeen;	///< every distinct room the run entered
	/// "len=N needle=<x>" -> hits, for compares whose haystack was tainted.
	Common::HashMap<Common::String, uint64> _compareNeedles;
	/// origin -> max kStrAt offset seen, and whether offsets advanced by 1.
	Common::HashMap<Common::String, uint32> _strAtMaxOff;
	Common::HashMap<Common::String, uint32> _strAtLastOff;
	Common::HashMap<Common::String, uint64> _strAtStepOne;
	Common::HashMap<Common::String, uint64> _strAtStepOther;

	uint64 _taintEvents, _taintBytes, _untaintEvents;
	uint64 _vmReads, _vmWrites, _vmReadsTainted, _vmWritesTainted;
	uint64 _sinkTotal;
	uint64 _sinceFlush;
	uint64 _stalePrunes;
	uint64 _staleBytesPruned;
	uint64 _staleRejects;	///< sink hits that intersected taint but failed the content re-read
	int _room;
	Common::String _phase;

	void addExemplar(const char *sinkName, TaintVerdict v, const Common::String &line);
};

// A reference to a deliberately-leaked heap instance, NOT a file-scope object:
// a static TaintTracker's destructor runs from exit() after ScummVM's memory
// pool and mutex backend are already gone, which aborts with "pure virtual
// method called" long after the game has finished. The abort is in the
// probe, not the engine, but it still makes the run look like a crash.
extern TaintTracker &g_sciTaint;

} // End of namespace Sci

#endif
