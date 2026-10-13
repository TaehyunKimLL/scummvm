#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/fs.h"
#include "common/stream.h"
#include "../../system/null_osystem.h"

/**
 * A console command run between two VM instructions must not leave the VM
 * on the wrong frame.
 *
 * run_vm() calls Console::onFrame() before every instruction, and the debug
 * socket polled there runs console commands. `send` runs a nested
 * run_vm(); its final ret leaves s->variables[] on the nested frame and
 * only sets _executionStackPosChanged. The block that reloads the frame
 * sits at the top of the loop, before onFrame(), so without a second look
 * the instruction after a `send` read the nested frame's temps: PQ2 room 5,
 * "[5/999 sounds::eachElementDo @ 0214] Uninitialized read for temp 0"
 * right after `sat 0` wrote it, kNextNode signature errors in
 * regions::eachElementDo, and "xs is stale ((nil) ...)" (C13 play.py
 * check 8 sends `send ?ego posn` in room 5).
 *
 * Running the VM needs a game, so this pins the order in the source: the
 * frame is reloaded at the top of the loop, onFrame() runs after that, and
 * a changed stack after onFrame() goes back to the reload before the
 * opcode is fetched.
 */
class SciVmConsoleSendTestSuite : public CxxTest::TestSuite {
	Common::String readSource(const char *rel) {
		Common::String path = Common::String(SCI_TEST_SRCDIR) + "/" + rel;
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path).c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		return out;
	}

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_a_console_send_reloads_the_frame_before_the_next_opcode() {
		const Common::String vm = readSource("engines/sci/engine/vm.cpp");
		const int runVm = vm.find("void run_vm(EngineState *s) {");
		TS_ASSERT_LESS_THAN(0, runVm);

		const int reload = vm.find("if (s->_executionStackPosChanged) {", runVm);
		const int setTemps = vm.find("s->variables[VAR_TEMP] = s->xs->fp;", runVm);
		const int onFrame = vm.find("con->onFrame();", runVm);
		const int recheck = vm.find("if (s->_executionStackPosChanged)\n\t\t\tcontinue;", runVm);
		const int fetch = vm.find("readPMachineInstruction(", runVm);

		TS_ASSERT_LESS_THAN(runVm, reload);
		TS_ASSERT_LESS_THAN(reload, setTemps);
		TS_ASSERT_LESS_THAN(setTemps, onFrame);
		// Right after onFrame(), before the opcode is read and executed.
		TS_ASSERT_LESS_THAN(onFrame, recheck);
		TS_ASSERT_LESS_THAN(recheck, fetch);

		// The loop is the one that reloads at its top: `continue` goes back
		// to the reload, not past it.
		const int loop = vm.find("while (1) {", runVm);
		TS_ASSERT_LESS_THAN(runVm, loop);
		TS_ASSERT_LESS_THAN(loop, reload);
	}

	void test_a_console_send_runs_a_nested_vm() {
		// What the recheck is for: `send` runs run_vm() from inside the
		// console, and a hard return from it sets the flag.
		const Common::String con = readSource("engines/sci/console.cpp");
		const int send = con.find("bool Console::cmdSend(");
		TS_ASSERT_LESS_THAN(0, send);
		TS_ASSERT_LESS_THAN(send, con.find("run_vm(_engine->_gamestate);", send));

		const Common::String vm = readSource("engines/sci/engine/vm.cpp");
		const int base = vm.find("s->executionStackBase = old_executionStackBase;");
		TS_ASSERT_LESS_THAN(0, base);
		TS_ASSERT_LESS_THAN(base, vm.find("s->_executionStackPosChanged = true;", base));
	}
};
