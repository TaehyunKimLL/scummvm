#ifndef TEST_ENGINES_AGI_SUPPORT_READ_SOURCE_H
#define TEST_ENGINES_AGI_SUPPORT_READ_SOURCE_H

#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/str.h"
#include "common/stream.h"
#include "../../../system/null_osystem.h"

// The AGI wiring tests pin engine wiring that needs a running engine to
// exercise by reading the engine's sources back out of the tree the runner
// was built from (AGI_TEST_SRCDIR, test/module.mk). One copy for every suite
// that does so. Needs the null OSystem (installed by the suite's setUp()).
namespace AgiTest {

inline Common::String readSource(const char *rel) {
#if !NULL_OSYSTEM_IS_AVAILABLE
	TS_SKIP("no file access in this test environment");
	return Common::String();
#else
	Common::FSNode node(Common::Path(Common::String(AGI_TEST_SRCDIR) + "/" + rel, '/'));
	Common::SeekableReadStream *in = node.createReadStream();
	if (!in) {
		TS_FAIL(Common::String::format("cannot read %s", rel).c_str());
		return Common::String();
	}
	const uint32 len = (uint32)in->size();
	char *buf = new char[len + 1];
	const uint32 got = in->read(buf, len);
	buf[got] = 0;
	Common::String out(buf, got);
	delete[] buf;
	delete in;
	return out;
#endif
}

} // End of namespace AgiTest

#endif
