/* M11-1 PROBE — temporary, remove before merging.
 *
 * Does any script do ARITHMETIC on what kStrLen / kStrAt return?
 *
 * M4 and M6 counted calls to those ops. This follows the VALUE: the
 * accumulator is marked when one of them returns, and any arithmetic opcode
 * that consumes a marked accumulator is a sink. The mark is dropped the
 * moment the accumulator is overwritten by anything else, so a later
 * unrelated add is not blamed on an earlier strlen.
 *
 * Only strings that came from a translation are followed: an untranslated
 * game's arithmetic on its own byte strings is correct today and stays so.
 */

#ifndef SCI_ENGINE_M11PROBE_H
#define SCI_ENGINE_M11PROBE_H

#include "common/scummsys.h"
#include "sci/engine/vm_types.h"

namespace Sci {

struct M11Probe {
	// Accumulator taint: which op produced it, and the exact value, so the
	// sink can verify the acc still holds it before counting.
	const char *accOrigin;
	uint32 accValue;
	bool accTainted;

	// Per-op totals.
	uint32 strLenCalls, strLenTranslated;
	uint32 strAtCalls, strAtTranslated;

	// Sinks: arithmetic opcodes that consumed a tainted acc.
	uint32 sinkAdd, sinkSub, sinkMul, sinkDiv, sinkMod, sinkShl, sinkShr;
	uint32 sinkOther;
	uint32 sinkStaleRejected;   // acc no longer held the tainted value

	// Consumers that are NOT arithmetic but tell us something.
	uint32 consumedByCompare;   // eq/ne/lt/gt - a length compared to a constant
	uint32 consumedByStore;     // stored to a variable; may be used later
	uint32 consumedByKernel;    // passed straight into another kernel call

	// M11-2: buffer fit. The interesting number is not overflow (we expect
	// none under cp949) but the tightest margin - UTF-8 will be ~40% longer.
	uint32 fmtTranslated, fmtOverflow;
	uint32 cpyTranslated, cpyOverflow;
	int    tightestAvail;       // maxSize at the tightest fit seen
	uint32 tightestNeed;
	const char *tightestOp;
	uint32 wouldOverflowAt140;  // fits now, but not at need*1.4
	void noteFit(const char *op, int avail, uint32 need);

	M11Probe();
	void taintAcc(const char *origin, uint32 value);
	void clearAcc();
	// Called from vm.cpp before an arithmetic opcode runs.
	void arithmetic(const char *opName, uint32 accNow);
	void compare(uint32 accNow);
	void store(uint32 accNow);
	void kernelArg(uint32 accNow);
	void report();
};

extern M11Probe &g_m11;

} // End of namespace Sci

#endif
