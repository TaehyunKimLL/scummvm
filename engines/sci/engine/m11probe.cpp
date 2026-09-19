/* M11-1 PROBE — temporary, remove before merging. See m11probe.h. */

#include "sci/engine/m11probe.h"
#include "common/textconsole.h"
#include "common/debug.h"
#include "common/str.h"

namespace Sci {

// Leaked on purpose: a file-scope object's destructor runs from exit() after
// the allocator backend is gone and aborts. See dynamic-taint-measurement.
M11Probe &g_m11 = *(new M11Probe());

M11Probe::M11Probe()
	: accOrigin(nullptr), accValue(0), accTainted(false),
	  strLenCalls(0), strLenTranslated(0), strAtCalls(0), strAtTranslated(0),
	  sinkAdd(0), sinkSub(0), sinkMul(0), sinkDiv(0), sinkMod(0), sinkShl(0), sinkShr(0),
	  sinkOther(0), sinkStaleRejected(0),
	  consumedByCompare(0), consumedByStore(0), consumedByKernel(0),
	  fmtTranslated(0), fmtOverflow(0), cpyTranslated(0), cpyOverflow(0),
	  tightestAvail(0), tightestNeed(0), tightestOp(nullptr), wouldOverflowAt140(0) {
}

void M11Probe::noteFit(const char *op, int avail, uint32 need) {
	if (avail <= 0)
		return;
	// UTF-8 makes a cp949 string ~1.4x longer. Would that still fit?
	const uint32 need140 = need + (need * 2) / 5;
	if ((uint32)avail < need140)
		wouldOverflowAt140++;
	const int margin = avail - (int)need;
	const int bestMargin = tightestOp ? tightestAvail - (int)tightestNeed : 0x7FFFFFFF;
	if (margin < bestMargin) {
		tightestAvail = avail;
		tightestNeed = need;
		tightestOp = op;
	}
}

void M11Probe::taintAcc(const char *origin, uint32 value) {
	accOrigin = origin;
	accValue = value;
	accTainted = true;
}

void M11Probe::clearAcc() {
	accTainted = false;
}

void M11Probe::arithmetic(const char *opName, uint32 accNow) {
	if (!accTainted)
		return;
	if (accNow != accValue) {
		// The acc was overwritten by something we did not see. Not a sink.
		sinkStaleRejected++;
		accTainted = false;
		return;
	}
	if (!strcmp(opName, "add")) sinkAdd++;
	else if (!strcmp(opName, "sub")) sinkSub++;
	else if (!strcmp(opName, "mul")) sinkMul++;
	else if (!strcmp(opName, "div")) sinkDiv++;
	else if (!strcmp(opName, "mod")) sinkMod++;
	else if (!strcmp(opName, "shl")) sinkShl++;
	else if (!strcmp(opName, "shr")) sinkShr++;
	else sinkOther++;
	debug("M11SINK\t%s\t%s\tvalue=%u", opName, accOrigin, accNow);
	accTainted = false;
}

void M11Probe::compare(uint32 accNow) {
	if (!accTainted) return;
	if (accNow != accValue) { sinkStaleRejected++; accTainted = false; return; }
	consumedByCompare++;
	accTainted = false;
}

void M11Probe::store(uint32 accNow) {
	if (!accTainted) return;
	if (accNow != accValue) { sinkStaleRejected++; accTainted = false; return; }
	consumedByStore++;
	// Stored: it may be used later, but following it through memory is
	// out of scope for this probe. Recorded so the coverage limit is stated.
	accTainted = false;
}

void M11Probe::kernelArg(uint32 accNow) {
	if (!accTainted) return;
	if (accNow != accValue) { sinkStaleRejected++; accTainted = false; return; }
	consumedByKernel++;
	accTainted = false;
}

void M11Probe::report() {
	debug("M11REPORT kStrLen calls=%u translated=%u", strLenCalls, strLenTranslated);
	debug("M11REPORT kStrAt  calls=%u translated=%u", strAtCalls, strAtTranslated);
	debug("M11REPORT arithmetic sinks: add=%u sub=%u mul=%u div=%u mod=%u shl=%u shr=%u other=%u",
	      sinkAdd, sinkSub, sinkMul, sinkDiv, sinkMod, sinkShl, sinkShr, sinkOther);
	debug("M11REPORT consumed by: compare=%u store=%u kernelArg=%u",
	      consumedByCompare, consumedByStore, consumedByKernel);
	debug("M11REPORT STALEREJECT hits=%u", sinkStaleRejected);
	debug("M11REPORT kFormat translated=%u overflow=%u | kStrCpy translated=%u overflow=%u",
	      fmtTranslated, fmtOverflow, cpyTranslated, cpyOverflow);
	debug("M11REPORT fit: tightest %s avail=%d need=%u | would overflow at 1.4x: %u",
	      tightestOp ? tightestOp : "(none)", tightestAvail, tightestNeed, wouldOverflowAt140);
	if (strLenTranslated == 0 && strAtTranslated == 0)
		debug("M11REPORT COVERAGE: no translated string reached kStrLen/kStrAt - this run proves nothing");
}

} // End of namespace Sci
