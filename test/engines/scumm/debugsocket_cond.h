#include <cxxtest/TestSuite.h>

#include "engines/scumm/debugsocket_cond.h"
#include "gui/debugsocket-protocol.h"

/**
 * The `wait` conditions of SCUMM's debug socket: what a harness line parses
 * to, and when a parsed condition holds.
 *
 * A harness that waits on the wrong thing captures the wrong frame, and two
 * runs compared at different frames measure timing instead of rendering -
 * the failure these waits exist to prevent. Lines go through the socket's
 * own tokenizer, as they do in the engine.
 */
class ScummDebugSocketCondTestSuite : public CxxTest::TestSuite {
	static bool parseLine(const char *line, Scumm::SocketCond &c, uint32 &timeout, bool &freeze,
	                      Common::String &err) {
		Common::StringArray args;
		GUI::DebugSocketProtocol::tokenize(line, args);
		return Scumm::parseSocketWait(args, c, timeout, freeze, err);
	}

	static Scumm::SocketSnapshot snap(int room, int talking, int haveMsg, int userPut, uint32 loop) {
		Scumm::SocketSnapshot s;
		s.room = room;
		s.talking = talking;
		s.haveMsg = haveMsg;
		s.userPut = userPut;
		s.loop = loop;
		return s;
	}

	static Common::Array<byte> bytes(const char *s) {
		Common::Array<byte> b;
		for (; *s; s++)
			b.push_back((byte)*s);
		return b;
	}

public:
	void test_room() {
		Scumm::SocketCond c;
		uint32 timeout = 3600;
		bool freeze = true;
		Common::String err;
		TS_ASSERT(parseLine("room == 5", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.kind, Scumm::SocketCond::kRoom);
		TS_ASSERT_EQUALS(c.op, "==");
		TS_ASSERT_EQUALS(c.value, 5);
		TS_ASSERT(!freeze);
		TS_ASSERT_EQUALS(timeout, 3600u);	// untouched without `timeout`

		const Common::Array<Common::String> noText;
		const Common::Array<Common::Array<byte> > noRaw;
		TS_ASSERT(Scumm::condHolds(c, snap(5, 0, 0, 0, 0), noText, noRaw));
		TS_ASSERT(!Scumm::condHolds(c, snap(6, 0, 0, 0, 0), noText, noRaw));

		TS_ASSERT(parseLine("room != 3", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.op, "!=");
		TS_ASSERT_EQUALS(c.value, 3);
		TS_ASSERT(!Scumm::condHolds(c, snap(3, 0, 0, 0, 0), noText, noRaw));
		TS_ASSERT(Scumm::condHolds(c, snap(4, 0, 0, 0, 0), noText, noRaw));
	}

	void test_room_orderings() {
		Scumm::SocketCond c;
		uint32 timeout = 3600;
		bool freeze;
		Common::String err;
		const Common::Array<Common::String> noText;
		const Common::Array<Common::Array<byte> > noRaw;
		// op, then whether rooms 4, 5, 6 satisfy "<op> 5"
		static const struct { const char *line; bool r4, r5, r6; } kCases[] = {
			{ "room < 5", true, false, false },
			{ "room <= 5", true, true, false },
			{ "room > 5", false, false, true },
			{ "room >= 5", false, true, true },
		};
		for (uint i = 0; i < ARRAYSIZE(kCases); i++) {
			TS_ASSERT(parseLine(kCases[i].line, c, timeout, freeze, err));
			TS_ASSERT_EQUALS(Scumm::condHolds(c, snap(4, 0, 0, 0, 0), noText, noRaw), kCases[i].r4);
			TS_ASSERT_EQUALS(Scumm::condHolds(c, snap(5, 0, 0, 0, 0), noText, noRaw), kCases[i].r5);
			TS_ASSERT_EQUALS(Scumm::condHolds(c, snap(6, 0, 0, 0, 0), noText, noRaw), kCases[i].r6);
		}
	}

	/**
	 * A wait counts loops, and a paused engine (a modal dialog - a failed
	 * load) runs none: the wait must end at once, or the socket never reads
	 * the `key Return` that would close the dialog.
	 */
	void test_stall() {
		TS_ASSERT(Scumm::socketStall(false, 1000, 900, 60000) == nullptr);
		TS_ASSERT_EQUALS(Common::String(Scumm::socketStall(true, 1000, 900, 60000)), "paused");
		TS_ASSERT(Scumm::socketStall(false, 60899, 900, 60000) == nullptr);
		TS_ASSERT_EQUALS(Common::String(Scumm::socketStall(false, 60900, 900, 60000)), "stalled");
		// getMillis() wrapping past 2^32 is still "just now".
		TS_ASSERT(Scumm::socketStall(false, 5, 0xfffffff0u, 60000) == nullptr);
	}

	void test_text_is_a_substring_of_one_string() {
		Scumm::SocketCond c;
		uint32 timeout = 3600;
		bool freeze;
		Common::String err;
		TS_ASSERT(parseLine("text \"Guybrush\"", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.kind, Scumm::SocketCond::kText);
		TS_ASSERT_EQUALS(c.text, "Guybrush");

		Common::Array<Common::String> texts;
		const Common::Array<Common::Array<byte> > noRaw;
		TS_ASSERT(!Scumm::condHolds(c, snap(1, 0, 0, 0, 0), texts, noRaw));
		texts.push_back("I'm Guybrush Threepwood.");
		TS_ASSERT(Scumm::condHolds(c, snap(1, 0, 0, 0, 0), texts, noRaw));

		// Spaces inside the quotes are part of it; UTF-8 Hangul too.
		TS_ASSERT(parseLine("text \"Well, well, well.\" freeze", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.text, "Well, well, well.");
		TS_ASSERT(freeze);
		TS_ASSERT(parseLine("text \"\xEC\x95\x88\xEB\x85\x95\"", c, timeout, freeze, err));	// 안녕
		TS_ASSERT_EQUALS(c.text, "\xEC\x95\x88\xEB\x85\x95");
		texts.clear();
		texts.push_back("\xEC\x95\x88\xEB\x85\x95\xED\x95\x98\xEC\x84\xB8\xEC\x9A\x94");	// 안녕하세요
		TS_ASSERT(Scumm::condHolds(c, snap(1, 0, 0, 0, 0), texts, noRaw));

		// Not across two strings.
		TS_ASSERT(parseLine("text \"ab cd\"", c, timeout, freeze, err));
		texts.clear();
		texts.push_back("xx ab");
		texts.push_back("cd yy");
		TS_ASSERT(!Scumm::condHolds(c, snap(1, 0, 0, 0, 0), texts, noRaw));

		// Quotes kept by a caller that did not tokenize are dropped.
		Common::StringArray raw;
		raw.push_back("text");
		raw.push_back("\"Guybrush\"");
		TS_ASSERT(Scumm::parseSocketWait(raw, c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.text, "Guybrush");
	}

	void test_seen_matches_the_games_bytes() {
		Scumm::SocketCond c;
		uint32 timeout = 3600;
		bool freeze;
		Common::String err;
		TS_ASSERT(parseLine("seen c7d1b1db", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.kind, Scumm::SocketCond::kSeen);
		TS_ASSERT_EQUALS(c.bytes.size(), 4u);
		TS_ASSERT_EQUALS(c.bytes[0], 0xc7);
		TS_ASSERT_EQUALS(c.bytes[3], 0xdb);

		const Common::Array<Common::String> noText;
		Common::Array<Common::Array<byte> > raw;
		raw.push_back(bytes("abc"));
		TS_ASSERT(!Scumm::condHolds(c, snap(1, 0, 0, 0, 0), noText, raw));
		raw.push_back(bytes("\x20\xc7\xd1\xb1\xdb\x2e"));	// " 한글." in CP949
		TS_ASSERT(Scumm::condHolds(c, snap(1, 0, 0, 0, 0), noText, raw));

		TS_ASSERT(parseLine("seen C7D1", c, timeout, freeze, err));	// either case
		TS_ASSERT(Scumm::condHolds(c, snap(1, 0, 0, 0, 0), noText, raw));
	}

	void test_talkdone_userput_loops() {
		Scumm::SocketCond c;
		uint32 timeout = 3600;
		bool freeze;
		Common::String err;
		const Common::Array<Common::String> noText;
		const Common::Array<Common::Array<byte> > noRaw;

		TS_ASSERT(parseLine("talkdone", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.kind, Scumm::SocketCond::kTalkDone);
		TS_ASSERT(Scumm::condHolds(c, snap(1, 0, 0, 0, 0), noText, noRaw));
		TS_ASSERT(!Scumm::condHolds(c, snap(1, 3, 0, 0, 0), noText, noRaw));	// actor 3 talking
		TS_ASSERT(!Scumm::condHolds(c, snap(1, 0, 1, 0, 0), noText, noRaw));	// message still up

		TS_ASSERT(parseLine("userput", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.kind, Scumm::SocketCond::kUserPut);
		TS_ASSERT(!Scumm::condHolds(c, snap(1, 0, 0, 0, 0), noText, noRaw));
		TS_ASSERT(Scumm::condHolds(c, snap(1, 0, 0, 1, 0), noText, noRaw));

		TS_ASSERT(parseLine("loops 30", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(c.kind, Scumm::SocketCond::kLoops);
		TS_ASSERT_EQUALS(c.value, 30);
		// The caller adds the loop the wait started on.
		c.value += 100;
		TS_ASSERT(!Scumm::condHolds(c, snap(1, 0, 0, 0, 129), noText, noRaw));
		TS_ASSERT(Scumm::condHolds(c, snap(1, 0, 0, 0, 130), noText, noRaw));
	}

	void test_timeout_and_freeze_in_any_order() {
		Scumm::SocketCond c;
		uint32 timeout = 3600;
		bool freeze;
		Common::String err;
		TS_ASSERT(parseLine("room == 7 timeout 600", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(timeout, 600u);
		TS_ASSERT(!freeze);
		timeout = 3600;
		TS_ASSERT(parseLine("userput freeze timeout 90", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(timeout, 90u);
		TS_ASSERT(freeze);
		timeout = 3600;
		TS_ASSERT(parseLine("loops 2 timeout 600 freeze", c, timeout, freeze, err));
		TS_ASSERT_EQUALS(timeout, 600u);
		TS_ASSERT(freeze);
	}

	void test_errors() {
		Scumm::SocketCond c;
		uint32 timeout = 3600;
		bool freeze;
		Common::String err;
		static const char *const kBad[] = {
			"room ==", "room = 5", "room == x", "room == -1",
			"seen abc", "seen zz", "seen",
			"bogus", "", "text", "loops", "loops 1x",
			"talkdone freeze extra", "room == 5 timeout", "room == 5 timeout 0"
		};
		for (uint i = 0; i < ARRAYSIZE(kBad); i++) {
			err.clear();
			TS_ASSERT(!parseLine(kBad[i], c, timeout, freeze, err));
			TS_ASSERT(!err.empty());
		}
		TS_ASSERT_EQUALS(timeout, 3600u);
	}
};
