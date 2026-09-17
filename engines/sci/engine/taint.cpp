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

#include "sci/engine/taint.h"
#include "sci/engine/seg_manager.h"
#include "sci/engine/segment.h"
#include "common/file.h"
#include "common/textconsole.h"

namespace Sci {

TaintTracker &g_sciTaint = *(new TaintTracker());

static const char *const kVerdictName[5] = { "TAINTED", "SCRIPTLIT", "CLEAN", "IMMEDIATE", "UNKNOWN" };

TaintTracker::TaintTracker() :
	_taintEvents(0), _taintBytes(0), _untaintEvents(0),
	_vmReads(0), _vmWrites(0), _vmReadsTainted(0), _vmWritesTainted(0),
	_sinkTotal(0), _sinceFlush(0), _stalePrunes(0), _staleBytesPruned(0),
	_staleRejects(0), _room(-1), _phase("boot") {
}

void TaintTracker::setPhase(const char *phase) {
	_phase = phase;
}

void TaintTracker::noteRoom(int room) {
	if (room == _room)
		return;
	_room = room;
	_roomsSeen.getOrCreateVal((uint32)room)++;
}

void TaintTracker::noteResourceText(const char *origin, uint32 nbytes) {
	_resBytes[Common::String(origin)] += nbytes;
	_resReads[Common::String(origin)] += 1;
}

void TaintTracker::taint(SegManager *segMan, reg_t ptr, uint32 len, const char *origin, const char *sample) {
	if (ptr.isNull() || ptr.getSegment() == 0 || len == 0)
		return;

	const uint32 seg = ptr.getSegment();
	const uint32 lo = ptr.getOffset();
	const uint32 hi = lo + len;

	char head[24];
	memset(head, 0, sizeof(head));
	if (sample)
		strncpy(head, sample, sizeof(head) - 1);

	RangeList &rl = _ranges.getOrCreateVal(seg);
	// Merge into an overlapping/adjacent range if we can, else append. A merge
	// adopts the NEW head only when the new range starts at or before the old
	// one, so the head always describes the bytes at range.lo.
	bool merged = false;
	for (uint i = 0; i < rl.size(); i++) {
		if (lo <= rl[i].hi && rl[i].lo <= hi) {
			if (lo <= rl[i].lo)
				memcpy(rl[i].head, head, sizeof(head));
			rl[i].lo = MIN(rl[i].lo, lo);
			rl[i].hi = MAX(rl[i].hi, hi);
			merged = true;
			break;
		}
	}
	if (!merged) {
		Range r; r.lo = lo; r.hi = hi;
		memcpy(r.head, head, sizeof(head));
		rl.push_back(r);
	}

	_taintEvents++;
	_taintBytes += len;
	_sourceEvents[Common::String(origin)] += 1;
	_sourceBytes[Common::String(origin)] += len;

	if (_sourceEvents[Common::String(origin)] <= 3) {
		Common::String s = Common::String::format("SOURCE %-14s %04x:%04x len=%u segtype=%d phase=%s sample=\"%.40s\"",
			origin, (unsigned)seg, (unsigned)lo, (unsigned)len,
			segMan ? (int)segMan->getSegmentType(seg) : -1, _phase.c_str(), sample ? sample : "");
		_exemplars.push_back(s);
	}
	_sinceFlush++;
	if (_sinceFlush > 4000) { report(false); _sinceFlush = 0; }
}

void TaintTracker::untaint(reg_t ptr, uint32 len) {
	if (ptr.isNull() || ptr.getSegment() == 0 || len == 0)
		return;
	const uint32 seg = ptr.getSegment();
	if (!_ranges.contains(seg))
		return;
	const uint32 lo = ptr.getOffset(), hi = lo + len;
	RangeList &rl = _ranges[seg];
	RangeList out;
	bool changed = false;
	for (uint i = 0; i < rl.size(); i++) {
		if (hi <= rl[i].lo || rl[i].hi <= lo) { out.push_back(rl[i]); continue; }
		changed = true;
		if (rl[i].lo < lo) { Range r; r.lo = rl[i].lo; r.hi = lo; out.push_back(r); }
		if (hi < rl[i].hi) { Range r; r.lo = hi; r.hi = rl[i].hi; out.push_back(r); }
	}
	if (changed) { rl = out; _untaintEvents++; }
}

TaintVerdict TaintTracker::classify(SegManager *segMan, reg_t ptr, uint32 len) const {
	if (ptr.getSegment() == 0)
		return TV_IMMEDIATE;

	const uint32 seg = ptr.getSegment();
	const uint32 lo = ptr.getOffset();
	const uint32 hi = lo + (len ? len : 1);

	if (_ranges.contains(seg)) {
		const RangeList &rl = _ranges[seg];
		for (uint i = 0; i < rl.size(); i++)
			if (lo < rl[i].hi && rl[i].lo < hi)
				return TV_TAINTED;
	}

	if (segMan) {
		SegmentObj *mobj = segMan->getSegmentObj(seg);
		if (!mobj)
			return TV_UNKNOWN;
		if (mobj->getType() == SEG_TYPE_SCRIPT)
			return TV_SCRIPTLIT;
		return TV_CLEAN;
	}
	return TV_UNKNOWN;
}

TaintVerdict TaintTracker::classifyVerified(SegManager *segMan, reg_t ptr, uint32 len, bool *stale) const {
	if (stale) *stale = false;
	TaintVerdict v = classify(segMan, ptr, len);
	if (v != TV_TAINTED || !segMan)
		return v;

	// Re-read the bytes at the intersecting range's start. If the recorded
	// head no longer matches, the memory was reused and this is NOT the
	// resource text we tainted: report the fallback classification instead.
	const uint32 seg = ptr.getSegment();
	const uint32 lo = ptr.getOffset();
	const uint32 hi = lo + (len ? len : 1);
	const RangeList &rl = _ranges[seg];
	for (uint i = 0; i < rl.size(); i++) {
		if (!(lo < rl[i].hi && rl[i].lo < hi))
			continue;
		if (rl[i].head[0] == '\0')
			return TV_TAINTED;	// nothing recorded; do not reject
		Common::String live = segMan->getString(make_reg((uint16)seg, (uint16)rl[i].lo));
		const uint n = strlen(rl[i].head);
		if (live.size() >= n && memcmp(live.c_str(), rl[i].head, n) == 0)
			return TV_TAINTED;	// content confirmed
	}

	if (stale) *stale = true;
	SegmentObj *mobj = segMan->getSegmentObj(seg);
	if (mobj && mobj->getType() == SEG_TYPE_SCRIPT)
		return TV_SCRIPTLIT;
	return TV_CLEAN;
}

void TaintTracker::addExemplar(const char *sinkName, TaintVerdict v, const Common::String &line) {
	Common::String key = Common::String::format("%s|%d|%s", sinkName, (int)v, _phase.c_str());
	uint32 &n = _exemplarSeen.getOrCreateVal(key);
	if (n < 3) {
		n++;
		_exemplars.push_back(line);
	} else {
		n++;
	}
}

TaintVerdict TaintTracker::sink(SegManager *segMan, const char *sinkName, reg_t ptr, uint32 len, const char *note) {
	// A length of 0 means "the whole NUL-terminated string at ptr". Use the
	// STRING length, not the segment's remaining size: a buffer that runs to
	// the end of a 8KB segment would otherwise intersect every tainted range
	// in that segment and read as TAINTED no matter what it holds.
	uint32 useLen = len;
	if (useLen == 0 && segMan && ptr.getSegment() != 0) {
		useLen = (uint32)segMan->getString(ptr).size() + 1;
		if (useLen == 1)
			useLen = 1;	// empty string: still one byte of operand
	}
	bool stale = false;
	TaintVerdict v = classifyVerified(segMan, ptr, useLen, &stale);
	if (stale)
		_staleRejects++;
	_sinks.getOrCreateVal(Common::String(sinkName)).n[v]++;
	_sinkByPhase.getOrCreateVal(Common::String::format("%s|%s", sinkName, _phase.c_str())).n[v]++;
	_sinkTotal++;

	// For a TAINTED hit, record the live bytes: the doc must be able to say
	// WHICH translatable string the script did byte arithmetic on.
	Common::String content;
	if (v == TV_TAINTED && segMan) {
		reg_t base = ptr;
		content = segMan->getString(base);
		if (content.size() > 46)
			content = Common::String(content.c_str(), 46) + "...";
		for (uint i = 0; i < content.size(); i++)
			if (content[i] == '\n' || content[i] == '\r') content.setChar(' ', i);
	}

	if (v == TV_TAINTED && !content.empty())
		_taintedStrings.getOrCreateVal(Common::String::format("%s :: %s", sinkName, content.c_str()))++;

	addExemplar(sinkName, v, Common::String::format("SINK   %-16s %-9s %04x:%04x len=%u room=%d phase=%s %s%s%s",
		sinkName, kVerdictName[v], (unsigned)ptr.getSegment(), (unsigned)ptr.getOffset(),
		(unsigned)useLen, _room, _phase.c_str(), note ? note : "",
		content.empty() ? "" : " text=", content.c_str()));

	_sinceFlush++;
	if (_sinceFlush > 4000) { report(false); _sinceFlush = 0; }
	return v;
}

void TaintTracker::noteOrigin(const char *sinkName, TaintVerdict v, const Common::String &origin) {
	if (v != TV_TAINTED)
		return;
	_taintedOrigins.getOrCreateVal(Common::String::format("%s :: %s", sinkName, origin.c_str()))++;
}

void TaintTracker::noteCompare(const Common::String &needle, int n, bool haystackTainted,
                               const Common::String &origin) {
	if (!haystackTainted)
		return;
	Common::String vis;
	for (uint i = 0; i < needle.size() && i < 24; i++) {
		const char c = needle[i];
		if (c == ' ')  { vis += "<SP>"; continue; }
		if (c == '\n') { vis += "<LF>"; continue; }
		if (c == '\r') { vis += "<CR>"; continue; }
		if (c == '\t') { vis += "<TAB>"; continue; }
		if ((unsigned char)c < 0x20) { vis += Common::String::format("<%02x>", (unsigned char)c); continue; }
		vis += c;
	}
	_compareNeedles.getOrCreateVal(Common::String::format(
		"needlelen=%u n=%d needle=[%s] from %s",
		(unsigned)needle.size(), n, vis.c_str(), origin.c_str()))++;
}

void TaintTracker::noteStrAtOffset(const Common::String &origin, uint32 offset, bool tainted) {
	if (!tainted)
		return;
	uint32 &mx = _strAtMaxOff.getOrCreateVal(origin);
	if (offset > mx)
		mx = offset;
	uint32 &last = _strAtLastOff.getOrCreateVal(origin);
	if (offset == last + 1)
		_strAtStepOne.getOrCreateVal(origin)++;
	else
		_strAtStepOther.getOrCreateVal(origin)++;
	last = offset;
}

void TaintTracker::sinkVerdict(const char *sinkName, TaintVerdict v, const char *note) {
	_sinks.getOrCreateVal(Common::String(sinkName)).n[v]++;
	_sinkByPhase.getOrCreateVal(Common::String::format("%s|%s", sinkName, _phase.c_str())).n[v]++;
	_sinkTotal++;
	addExemplar(sinkName, v, Common::String::format("SINK   %-16s %-9s (no ptr) phase=%s %s",
		sinkName, kVerdictName[v], _phase.c_str(), note ? note : ""));
}

void TaintTracker::pruneStack(uint16 stackSeg, uint32 liveBytes) {
	if (!_ranges.contains((uint32)stackSeg))
		return;
	RangeList &rl = _ranges[(uint32)stackSeg];
	RangeList out;
	for (uint i = 0; i < rl.size(); i++) {
		if (rl[i].lo >= liveBytes) {
			_stalePrunes++;
			_staleBytesPruned += rl[i].hi - rl[i].lo;
			continue;
		}
		Range r = rl[i];
		if (r.hi > liveBytes) {
			_staleBytesPruned += r.hi - liveBytes;
			r.hi = liveBytes;
		}
		if (r.hi <= r.lo)
			continue;
		out.push_back(r);
	}
	rl = out;
}

void TaintTracker::vmVar(SegManager *segMan, bool isWrite, int varType, int index, uint16 seg, uint32 byteOff) {
	// A raw VM variable read/write of a reg_t. The reg_t occupies 2 bytes at
	// byteOff inside the variable block's segment. If that overlaps tainted
	// text, a script is reading text bytes as a word without a kernel call.
	reg_t p = make_reg(seg, byteOff);
	TaintVerdict v = classifyVerified(segMan, p, 2, nullptr);
	if (isWrite) {
		_vmWrites++;
		if (v == TV_TAINTED) _vmWritesTainted++;
	} else {
		_vmReads++;
		if (v == TV_TAINTED) _vmReadsTainted++;
	}
	if (v == TV_TAINTED) {
		const char *nm = isWrite ? "VM_write_var" : "VM_read_var";
		_sinks.getOrCreateVal(Common::String(nm)).n[v]++;
		_sinkByPhase.getOrCreateVal(Common::String::format("%s|%s", nm, _phase.c_str())).n[v]++;
		addExemplar(nm, v, Common::String::format("SINK   %-16s %-9s %04x:%04x vartype=%d idx=%d room=%d phase=%s",
			nm, kVerdictName[v], (unsigned)seg, (unsigned)byteOff, varType, index, _room, _phase.c_str()));
	}
}

void TaintTracker::report(bool final_) {
	Common::DumpFile out;
	if (!out.open(Common::Path("/tmp/sci_taint_report.txt")))
		return;

	Common::String b;
	b += Common::String::format("# SCI taint report (%s)\n", final_ ? "final" : "interim");
	b += Common::String::format("phase_at_report=%s room=%d\n", _phase.c_str(), _room);
	b += Common::String::format("ROOMS_VISITED %lu :", (unsigned long)_roomsSeen.size());
	for (Common::HashMap<uint32, uint64>::const_iterator it = _roomsSeen.begin(); it != _roomsSeen.end(); ++it)
		b += Common::String::format(" %lu(x%lu)", (unsigned long)it->_key, (unsigned long)it->_value);
	b += "\n\n";

	b += "## Resource text that entered the engine (bytes a translation would replace)\n";
	uint64 totalResBytes = 0, totalResReads = 0;
	for (Common::HashMap<Common::String, uint64>::const_iterator it = _resBytes.begin(); it != _resBytes.end(); ++it) {
		b += Common::String::format("RESLOAD %-16s reads=%lu bytes=%lu\n", it->_key.c_str(),
			(unsigned long)_resReads[it->_key], (unsigned long)it->_value);
		totalResBytes += it->_value;
		totalResReads += _resReads[it->_key];
	}
	b += Common::String::format("RESLOAD TOTAL reads=%lu bytes=%lu\n\n", (unsigned long)totalResReads, (unsigned long)totalResBytes);

	b += "## Taint events (resource text copied into script-visible memory)\n";
	for (Common::HashMap<Common::String, uint64>::const_iterator it = _sourceEvents.begin(); it != _sourceEvents.end(); ++it)
		b += Common::String::format("TAINTSRC %-16s events=%lu bytes=%lu\n", it->_key.c_str(),
			(unsigned long)it->_value, (unsigned long)_sourceBytes[it->_key]);
	b += Common::String::format("TAINTSRC TOTAL events=%lu bytes=%lu untaint_events=%lu\n\n",
		(unsigned long)_taintEvents, (unsigned long)_taintBytes, (unsigned long)_untaintEvents);

	b += "## Sink hits by verdict\n";
	b += "SINKHDR name TAINTED SCRIPTLIT CLEAN IMMEDIATE UNKNOWN\n";
	for (Common::HashMap<Common::String, Counts>::const_iterator it = _sinks.begin(); it != _sinks.end(); ++it) {
		const Counts &c = it->_value;
		b += Common::String::format("SINK %-18s %lu %lu %lu %lu %lu\n", it->_key.c_str(),
			(unsigned long)c.n[0], (unsigned long)c.n[1], (unsigned long)c.n[2],
			(unsigned long)c.n[3], (unsigned long)c.n[4]);
	}
	b += Common::String::format("SINK TOTAL_CALLS %lu\n\n", (unsigned long)_sinkTotal);

	b += "## Sink hits by verdict and phase\n";
	for (Common::HashMap<Common::String, Counts>::const_iterator it = _sinkByPhase.begin(); it != _sinkByPhase.end(); ++it) {
		const Counts &c = it->_value;
		b += Common::String::format("SINKPHASE %-34s %lu %lu %lu %lu %lu\n", it->_key.c_str(),
			(unsigned long)c.n[0], (unsigned long)c.n[1], (unsigned long)c.n[2],
			(unsigned long)c.n[3], (unsigned long)c.n[4]);
	}
	b += "\n";

	b += "## Raw VM variable path (lag/lal/lsg/sag/sal ... read_var/write_var)\n";
	b += Common::String::format("VMVAR reads=%lu reads_tainted=%lu writes=%lu writes_tainted=%lu\n",
		(unsigned long)_vmReads, (unsigned long)_vmReadsTainted,
		(unsigned long)_vmWrites, (unsigned long)_vmWritesTainted);
	b += Common::String::format("STACKPRUNE ranges=%lu bytes=%lu\n",
		(unsigned long)_stalePrunes, (unsigned long)_staleBytesPruned);
	b += Common::String::format("STALEREJECT hits=%lu  (intersected a tainted range but the bytes had been reused)\n\n",
		(unsigned long)_staleRejects);

	b += "## Exemplars (first 3 per sink/verdict/phase)\n";
	for (uint i = 0; i < _exemplars.size(); i++)
		b += _exemplars[i] + "\n";

	b += "\n## What the script COMPARED tainted text against (needle, length)\n";
	b += Common::String::format("DISTINCT_NEEDLES %lu\n", (unsigned long)_compareNeedles.size());
	for (Common::HashMap<Common::String, uint64>::const_iterator it = _compareNeedles.begin(); it != _compareNeedles.end(); ++it)
		b += Common::String::format("NEEDLE %6lu  %s\n", (unsigned long)it->_value, it->_key.c_str());

	b += "\n## kStrAt offset progression on tainted text (per calling origin)\n";
	b += "# step1 = offset advanced by exactly 1 (a left-to-right scan)\n";
	b += "# stepN = any other jump (indexing / restart)\n";
	for (Common::HashMap<Common::String, uint32>::const_iterator it = _strAtMaxOff.begin(); it != _strAtMaxOff.end(); ++it)
		b += Common::String::format("STRATWALK maxoff=%lu step1=%lu stepN=%lu  %s\n",
			(unsigned long)it->_value,
			(unsigned long)(_strAtStepOne.contains(it->_key) ? _strAtStepOne[it->_key] : 0),
			(unsigned long)(_strAtStepOther.contains(it->_key) ? _strAtStepOther[it->_key] : 0),
			it->_key.c_str());

	b += "\n## Which SCRIPT did the byte work on translatable text (TAINTED hits only)\n";
	b += Common::String::format("DISTINCT_TAINTED_ORIGINS %lu\n", (unsigned long)_taintedOrigins.size());
	for (Common::HashMap<Common::String, uint64>::const_iterator it = _taintedOrigins.begin(); it != _taintedOrigins.end(); ++it)
		b += Common::String::format("TORIGIN %6lu  %s\n", (unsigned long)it->_value, it->_key.c_str());

	b += "\n## Distinct TRANSLATABLE strings each sink did byte work on\n";
	b += Common::String::format("DISTINCT_TAINTED_STRINGS %lu\n", (unsigned long)_taintedStrings.size());
	for (Common::HashMap<Common::String, uint64>::const_iterator it = _taintedStrings.begin(); it != _taintedStrings.end(); ++it)
		b += Common::String::format("TSTR %6lu  %s\n", (unsigned long)it->_value, it->_key.c_str());

	b += "\n## Exemplar-class occurrence counts (sink|verdict|phase -> hits)\n";
	for (Common::HashMap<Common::String, uint32>::const_iterator it = _exemplarSeen.begin(); it != _exemplarSeen.end(); ++it)
		b += Common::String::format("CLASS %-46s %lu\n", it->_key.c_str(), (unsigned long)it->_value);

	b += "\n# END\n";
	out.write(b.c_str(), b.size());
	out.close();
	if (final_)
		warning("TAINT: final report written, %lu sink calls, %lu taint events, %lu tainted bytes",
			(unsigned long)_sinkTotal, (unsigned long)_taintEvents, (unsigned long)_taintBytes);
}

} // End of namespace Sci
