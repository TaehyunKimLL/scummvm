#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/str-array.h"
#include "gui/debugsocket-protocol.h"

/**
 * Tests for the debug socket's wire protocol: how a command line is split,
 * how a reply is framed, and how `wait frames <n>` counts. The transport
 * (a UNIX socket or a named pipe) is not exercised here; the protocol is
 * what every engine's driver script depends on.
 */
class DebugSocketProtocolTestSuite : public CxxTest::TestSuite {
public:
	void test_tokenize_click_right() {
		Common::StringArray args;
		GUI::DebugSocketProtocol::tokenize("click 10 20 r", args);
		TS_ASSERT_EQUALS(args.size(), 4u);
		TS_ASSERT_EQUALS(args[0], "click");

		// The command's own arguments, as a command handler sees them.
		Common::String cmd;
		Common::StringArray cargs;
		TS_ASSERT(GUI::DebugSocketProtocol::parse("click 10 20 r", cmd, cargs));
		TS_ASSERT_EQUALS(cmd, "click");
		TS_ASSERT_EQUALS(cargs.size(), 3u);
		TS_ASSERT_EQUALS(cargs[0], "10");
		TS_ASSERT_EQUALS(cargs[1], "20");
		TS_ASSERT_EQUALS(cargs[2], "r");
	}

	void test_tokenize_quotes_and_spaces() {
		Common::StringArray args, tails;
		GUI::DebugSocketProtocol::tokenize("wait  text \"Begin Game\" x", args, &tails);
		TS_ASSERT_EQUALS(args.size(), 4u);
		TS_ASSERT_EQUALS(args[2], "Begin Game");
		TS_ASSERT_EQUALS(args[3], "x");
		// The tail keeps the quotes: a game string is matched verbatim.
		TS_ASSERT_EQUALS(tails.size(), 4u);
		TS_ASSERT_EQUALS(tails[2], "\"Begin Game\" x");
	}

	void test_parse_empty_line() {
		Common::String cmd;
		Common::StringArray args;
		TS_ASSERT(!GUI::DebugSocketProtocol::parse("", cmd, args));
		TS_ASSERT(!GUI::DebugSocketProtocol::parse("   ", cmd, args));
	}

	void test_frame_terminator() {
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame("OK"), "OK\n.\n");
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame("OK\n"), "OK\n.\n");
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame(""), ".\n");
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame("a\nb"), "a\nb\n.\n");
	}

	void test_frame_escapes_dot_lines() {
		// A reply line that is itself '.' would end the reply early.
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame("."), "..\n.\n");
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame("a\n.\nb"), "a\n..\nb\n.\n");
		// Any line starting with '.' gains one, so ".." stays distinguishable.
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame(".."), "...\n.\n");
		TS_ASSERT_EQUALS(GUI::DebugSocketProtocol::frame(".x\ny."), "..x\ny.\n.\n");
	}

	void test_wait_frames_counts_on_frame_calls() {
		GUI::DebugSocketProtocol p;
		TS_ASSERT(!p.frameWaitActive());
		p.startFrameWait(3);
		TS_ASSERT(p.frameWaitActive());
		TS_ASSERT(!p.onFrame());	// 1
		TS_ASSERT(!p.onFrame());	// 2
		TS_ASSERT(p.onFrame());		// 3: the wait ends here, once
		TS_ASSERT(!p.frameWaitActive());
		TS_ASSERT(!p.onFrame());
		TS_ASSERT_EQUALS(p.frames(), 4u);
	}

	void test_wait_frames_zero_is_immediate() {
		GUI::DebugSocketProtocol p;
		p.startFrameWait(0);
		TS_ASSERT(!p.frameWaitActive());
	}
};
