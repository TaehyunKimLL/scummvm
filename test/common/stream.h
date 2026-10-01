#include <cxxtest/TestSuite.h>

#include "common/memstream.h"

class ReadLineStreamTestSuite : public CxxTest::TestSuite {
	public:
	void test_readline() {
		byte contents[] = { 'a', 'b', '\n', '\n', 'c', '\n' };
		Common::MemoryReadStream ms(contents, sizeof(contents));

		char buffer[100];

		TS_ASSERT_DIFFERS((char *)0, ms.readLine(buffer, sizeof(buffer)));
		TS_ASSERT_EQUALS(0, strcmp(buffer, "ab\n"));

		TS_ASSERT_DIFFERS((char *)0, ms.readLine(buffer, sizeof(buffer)));
		TS_ASSERT_EQUALS(0, strcmp(buffer, "\n"));

		TS_ASSERT_DIFFERS((char *)0, ms.readLine(buffer, sizeof(buffer)));
		TS_ASSERT_EQUALS(0, strcmp(buffer, "c\n"));

		TS_ASSERT(!ms.eos());

		TS_ASSERT_EQUALS((char *)0, ms.readLine(buffer, sizeof(buffer)));

		TS_ASSERT(ms.eos());
	}

	void test_readline2() {
		byte contents[] = { 'a', 'b', '\n', '\n', 'c' };
		Common::MemoryReadStream ms(contents, sizeof(contents));

		char buffer[100];

		TS_ASSERT_DIFFERS((char *)0, ms.readLine(buffer, sizeof(buffer)));
		TS_ASSERT_EQUALS(0, strcmp(buffer, "ab\n"));

		TS_ASSERT_DIFFERS((char *)0, ms.readLine(buffer, sizeof(buffer)));
		TS_ASSERT_EQUALS(0, strcmp(buffer, "\n"));

		TS_ASSERT_DIFFERS((char *)0, ms.readLine(buffer, sizeof(buffer)));
		TS_ASSERT_EQUALS(0, strcmp(buffer, "c"));

		TS_ASSERT(ms.eos());
	}

	// unbufferStream() hands a stream to the backend's unbufferer, if one is
	// registered; null and an unregistered backend do nothing.
	static Common::SeekableReadStream *s_unbuffered;
	static void recordUnbuffer(Common::SeekableReadStream *stream) { s_unbuffered = stream; }

	void test_unbuffer_stream_goes_to_the_registered_unbufferer() {
		byte contents[] = { 1, 2, 3 };
		Common::MemoryReadStream ms(contents, sizeof(contents));
		const Common::StreamUnbufferer before = Common::setStreamUnbufferer(nullptr);
		s_unbuffered = nullptr;
		Common::unbufferStream(&ms);	// none registered: nothing happens
		Common::setStreamUnbufferer(&recordUnbuffer);
		Common::unbufferStream(nullptr);
		TS_ASSERT_EQUALS(s_unbuffered, (Common::SeekableReadStream *)nullptr);
		Common::unbufferStream(&ms);
		TS_ASSERT_EQUALS(s_unbuffered, (Common::SeekableReadStream *)&ms);
		TS_ASSERT_EQUALS(ms.readByte(), 1);
		Common::setStreamUnbufferer(before);
	}
};

Common::SeekableReadStream *ReadLineStreamTestSuite::s_unbuffered = nullptr;
