#include <cxxtest/TestSuite.h>

#include "common/file.h"
#include "common/fs.h"
#include "common/archive.h"
#include "common/memstream.h"

#include "engines/scumm/saved_text.h"
#include "engines/scumm/korean_text.h"

#include "test/system/null_osystem.h"

// The text-encoding trailer of a Korean save (engines/scumm/saved_text.h) and the font index guard (trs_bundle.h).
class ScummSavedTextTestSuite : public CxxTest::TestSuite {
public:
	void test_trailer_round_trip() {
		Common::MemoryWriteStreamDynamic out(DisposeAfterUse::YES);
		out.writeByte(0x42);	// the end of a state
		Scumm::writeSavedTextEncoding(out, true);
		Common::MemoryReadStream in(out.getData(), out.size());
		in.readByte();
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in), Scumm::kSavedTextUtf8);

		Common::MemoryWriteStreamDynamic out0(DisposeAfterUse::YES);
		Scumm::writeSavedTextEncoding(out0, false);
		Common::MemoryReadStream in0(out0.getData(), out0.size());
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in0), Scumm::kSavedTextLegacy);

		// No trailer: an upstream save ends with the state.
		const byte none[] = { 1, 2 };
		Common::MemoryReadStream in1(none, sizeof(none));
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in1), Scumm::kSavedTextUnknown);
		const byte other[] = { 'X', 'E', 'N', 'C', 1 };
		Common::MemoryReadStream in2(other, sizeof(other));
		TS_ASSERT_EQUALS(Scumm::readSavedTextEncoding(in2), Scumm::kSavedTextUnknown);
	}

	void test_refusal_index() {
		// korean.fnt has 2350 glyphs: a code point outside them has no index (the engine draws its '?').
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0x3131), -1);	// ㄱ (compatibility jamo)
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0x2026), -1);	// …
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xD79D), 2349);
	}
};
