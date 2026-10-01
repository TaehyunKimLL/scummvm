#include <cxxtest/TestSuite.h>

#include "backends/fs/stdiostream.h"
#include "common/stream.h"

// A stream that Common::unbufferStream() unbuffered reads straight from its
// file handle: DJGPP's fread() fetches an unbuffered stream one byte, one DOS
// call, at a time. The reads, seeks, position, size and end of file it then
// answers must be the buffered stream's.
class StdioStreamUnbufferedTestSuite : public CxxTest::TestSuite {
	static const char *path() { return "stdiostream_unbuffered.tmp"; }

	void writeFile(uint32 size) {
		StdioStream *w = StdioStream::makeFromPath(path(), StdioStream::WriteMode_Write);
		TS_ASSERT(w);
		if (!w)
			return;
		for (uint32 i = 0; i < size; ++i)
			w->writeByte((byte)(i * 7 + 3));
		delete w;
	}

	static byte at(uint32 i) { return (byte)(i * 7 + 3); }

public:
	void test_unbuffered_stream_reads_from_the_handle() {
		writeFile(16);
		StdioStream *s = StdioStream::makeFromPath(path(), StdioStream::WriteMode_Read);
		TS_ASSERT(s);
		if (!s)
			return;
		TS_ASSERT(!s->readsFromHandle());
		Common::unbufferStream(s);
		TS_ASSERT(s->readsFromHandle());
		delete s;
	}

	void test_unbuffered_stream_reads_seeks_and_ends_as_buffered() {
		const uint32 size = 70000;	// more than DJGPP's transfer buffer
		writeFile(size);
		StdioStream *s = StdioStream::makeFromPath(path(), StdioStream::WriteMode_Read);
		TS_ASSERT(s);
		if (!s)
			return;
		// A read before: the stream goes on from there.
		TS_ASSERT_EQUALS(s->readByte(), at(0));
		Common::unbufferStream(s);
		TS_ASSERT_EQUALS(s->pos(), 1);
		TS_ASSERT_EQUALS(s->size(), (int64)size);
		TS_ASSERT_EQUALS(s->pos(), 1);	// size() leaves the position alone

		byte *buf = new byte[size];
		TS_ASSERT_EQUALS(s->read(buf, 4), 4u);
		for (uint32 i = 0; i < 4; ++i)
			TS_ASSERT_EQUALS(buf[i], at(1 + i));
		TS_ASSERT_EQUALS(s->pos(), 5);

		// One big read across the whole file.
		TS_ASSERT(s->seek(100));
		TS_ASSERT_EQUALS(s->read(buf, size - 100), size - 100);
		bool same = true;
		for (uint32 i = 0; i < size - 100 && same; ++i)
			same = buf[i] == at(100 + i);
		TS_ASSERT(same);
		TS_ASSERT(!s->eos());
		TS_ASSERT_EQUALS(s->pos(), (int64)size);

		// Relative and end-relative seeks.
		TS_ASSERT(s->seek(-10, SEEK_CUR));
		TS_ASSERT_EQUALS(s->pos(), (int64)size - 10);
		TS_ASSERT_EQUALS(s->readByte(), at(size - 10));
		TS_ASSERT(s->seek(-3, SEEK_END));
		TS_ASSERT_EQUALS(s->readByte(), at(size - 3));

		// A read past the end: what there is, then the end of the stream.
		TS_ASSERT_EQUALS(s->read(buf, 10), 2u);
		TS_ASSERT_EQUALS(buf[0], at(size - 2));
		TS_ASSERT_EQUALS(buf[1], at(size - 1));
		TS_ASSERT(s->eos());
		TS_ASSERT(!s->err());
		TS_ASSERT_EQUALS(s->read(buf, 1), 0u);
		TS_ASSERT(s->eos());

		// A seek clears it, as fseek() does.
		TS_ASSERT(s->seek(0));
		TS_ASSERT(!s->eos());
		TS_ASSERT_EQUALS(s->readByte(), at(0));
		TS_ASSERT(s->seek(size));
		TS_ASSERT_EQUALS(s->read(buf, 1), 0u);
		TS_ASSERT(s->eos());
		s->clearErr();
		TS_ASSERT(!s->eos());
		TS_ASSERT(!s->seek(-1));	// before the start: refused, the position kept
		TS_ASSERT_EQUALS(s->pos(), (int64)size);

		delete[] buf;
		delete s;
	}
};
