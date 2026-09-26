#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/str-array.h"
#include "common/config-manager.h"
#include "common/keyboard.h"
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

	void test_parse_count() {
		uint32 n = 99;
		TS_ASSERT(GUI::DebugSocketProtocol::parseCount("3", n));
		TS_ASSERT_EQUALS(n, 3u);
		TS_ASSERT(GUI::DebugSocketProtocol::parseCount("0", n));
		TS_ASSERT_EQUALS(n, 0u);
		// Not a count: negative, empty, junk, absurdly large. `wait frames -1`
		// used to become a wait of four billion frames.
		TS_ASSERT(!GUI::DebugSocketProtocol::parseCount("-1", n));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseCount("", n));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseCount("12x", n));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseCount("x", n));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseCount("99999999999", n));
	}

	void test_config_key_from_game_domain_only() {
		// The keys are read from the game's own domain: a debug_socket= put
		// under [scummvm] must not open a socket in every game.
		Common::ConfigManager::Domain app, game;
		app.setVal("debug_socket", "/tmp/everywhere.sock");
		Common::String v;
		TS_ASSERT(!GUI::DebugSocketProtocol::gameDomainKey(&game, "debug_socket", v));
		TS_ASSERT(!GUI::DebugSocketProtocol::gameDomainKey(nullptr, "debug_socket", v));
		game.setVal("debug_socket", "/tmp/g.sock");
		TS_ASSERT(GUI::DebugSocketProtocol::gameDomainKey(&game, "debug_socket", v));
		TS_ASSERT_EQUALS(v, "/tmp/g.sock");
	}

	// ---- numeric arguments of the generic commands ----

	static Common::StringArray args(const char *a, const char *b = nullptr, const char *c = nullptr) {
		Common::StringArray r;
		r.push_back(a);
		if (b)
			r.push_back(b);
		if (c)
			r.push_back(c);
		return r;
	}

	void test_point_valid_and_largest() {
		int x = -1, y = -1;
		Common::String err;
		TS_ASSERT(GUI::DebugSocketProtocol::parsePoint(args("10", "20"), 320, 200, x, y, err));
		TS_ASSERT_EQUALS(x, 10);
		TS_ASSERT_EQUALS(y, 20);
		TS_ASSERT(GUI::DebugSocketProtocol::parsePoint(args("0", "0"), 320, 200, x, y, err));
		// The largest valid coordinate is one less than the screen size.
		TS_ASSERT(GUI::DebugSocketProtocol::parsePoint(args("319", "199"), 320, 200, x, y, err));
		TS_ASSERT_EQUALS(x, 319);
		TS_ASSERT_EQUALS(y, 199);
	}

	void test_point_rejects_bad() {
		int x, y;
		Common::String err;
		// negative
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("-1", "20"), 320, 200, x, y, err));
		TS_ASSERT(!err.empty());
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("10", "-5"), 320, 200, x, y, err));
		// non-numeric
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("ten", "20"), 320, 200, x, y, err));
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("10", "2x"), 320, 200, x, y, err));
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("", "20"), 320, 200, x, y, err));
		// out of the screen
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("320", "10"), 320, 200, x, y, err));
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("10", "200"), 320, 200, x, y, err));
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("99999999999", "1"), 320, 200, x, y, err));
		// missing
		TS_ASSERT(!GUI::DebugSocketProtocol::parsePoint(args("10"), 320, 200, x, y, err));
	}

	void test_key_names_and_keycodes() {
		GUI::DebugSocketProtocol::KeySpec k;
		Common::String err;
		TS_ASSERT(GUI::DebugSocketProtocol::parseKey(args("Return"), k, err));
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_RETURN);
		TS_ASSERT_EQUALS(k.ascii, 13);
		TS_ASSERT(GUI::DebugSocketProtocol::parseKey(args("A"), k, err));
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_a);
		TS_ASSERT_EQUALS(k.flags, Common::KBD_SHIFT);
		// One digit is the digit key, not keycode 5.
		TS_ASSERT(GUI::DebugSocketProtocol::parseKey(args("5"), k, err));
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_5);
		TS_ASSERT(GUI::DebugSocketProtocol::parseKey(args("13", "13", "4"), k, err));
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_RETURN);
		TS_ASSERT_EQUALS(k.ascii, 13);
		TS_ASSERT_EQUALS(k.flags, 4);
		TS_ASSERT(GUI::DebugSocketProtocol::parseKey(args("282"), k, err));	// F1, no ascii
		TS_ASSERT_EQUALS(k.keycode, Common::KEYCODE_F1);
		TS_ASSERT_EQUALS(k.ascii, 0);
	}

	void test_key_rejects_bad() {
		GUI::DebugSocketProtocol::KeySpec k;
		Common::String err;
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("-13"), k, err));		// negative
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("NoSuchKey"), k, err));	// unknown name
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("99999"), k, err));		// keycode out of range
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("00"), k, err));		// KEYCODE_INVALID
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("13", "-1"), k, err));	// negative ascii
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("13", "x"), k, err));	// non-numeric ascii
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("13", "65536"), k, err));	// ascii > 16 bits
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("13", "13", "128"), k, err));	// unknown flag bit
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("13", "13", "-4"), k, err));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseKey(args("Return", "13"), k, err));	// ascii only with a keycode
		TS_ASSERT(!err.empty());
	}

	void test_slot() {
		int slot = -1;
		TS_ASSERT(GUI::DebugSocketProtocol::parseSlot("0", slot));
		TS_ASSERT_EQUALS(slot, 0);
		TS_ASSERT(GUI::DebugSocketProtocol::parseSlot("999", slot));
		TS_ASSERT_EQUALS(slot, 999);
		TS_ASSERT(!GUI::DebugSocketProtocol::parseSlot("-1", slot));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseSlot("one", slot));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseSlot("1000", slot));
		TS_ASSERT(!GUI::DebugSocketProtocol::parseSlot("", slot));
	}
};
