#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/blaster.h"

class DosBlasterTestSuite : public CxxTest::TestSuite {
public:
	void test_all_fields_parsed() {
		DOS::BlasterConfig c = DOS::parseBlaster("A220 I7 D1 H5 P330 T6");
		TS_ASSERT_EQUALS(c.port, 0x220);
		TS_ASSERT_EQUALS(c.irq, 7);
		TS_ASSERT_EQUALS(c.dma, 1);
		TS_ASSERT_EQUALS(c.hdma, 5);
		TS_ASSERT_EQUALS(c.mpu, 0x330);
		TS_ASSERT_EQUALS(c.type, 6);
		TS_ASSERT_EQUALS(c.present, true);
	}

	void test_lowercase_tags_and_hex() {
		DOS::BlasterConfig c = DOS::parseBlaster("a240 p300");
		TS_ASSERT_EQUALS(c.port, 0x240);
		TS_ASSERT_EQUALS(c.mpu, 0x300);
		TS_ASSERT_EQUALS(c.present, true);
		// Fields not mentioned keep their defaults.
		TS_ASSERT_EQUALS(c.irq, 5);
		TS_ASSERT_EQUALS(c.dma, 1);
		TS_ASSERT_EQUALS(c.hdma, -1);
		TS_ASSERT_EQUALS(c.type, 0);
	}

	void test_null_string_gives_defaults_and_not_present() {
		DOS::BlasterConfig c = DOS::parseBlaster(nullptr);
		DOS::BlasterConfig def;
		TS_ASSERT_EQUALS(c.port, def.port);
		TS_ASSERT_EQUALS(c.irq, def.irq);
		TS_ASSERT_EQUALS(c.dma, def.dma);
		TS_ASSERT_EQUALS(c.hdma, def.hdma);
		TS_ASSERT_EQUALS(c.mpu, def.mpu);
		TS_ASSERT_EQUALS(c.type, def.type);
		TS_ASSERT_EQUALS(c.present, false);
	}

	void test_empty_string_gives_defaults_but_is_present() {
		DOS::BlasterConfig c = DOS::parseBlaster("");
		DOS::BlasterConfig def;
		TS_ASSERT_EQUALS(c.port, def.port);
		TS_ASSERT_EQUALS(c.irq, def.irq);
		TS_ASSERT_EQUALS(c.dma, def.dma);
		TS_ASSERT_EQUALS(c.hdma, def.hdma);
		TS_ASSERT_EQUALS(c.mpu, def.mpu);
		TS_ASSERT_EQUALS(c.type, def.type);
		TS_ASSERT_EQUALS(c.present, true);
	}

	void test_bad_hex_digit_keeps_default_port() {
		// "G" is not a hex digit, so the whole A-field is malformed and the
		// port stays at its default instead of parsing a truncated "22".
		DOS::BlasterConfig c = DOS::parseBlaster("A22G");
		TS_ASSERT_EQUALS(c.port, 0x220);
		TS_ASSERT_EQUALS(c.present, true);
	}

	void test_unknown_token_is_ignored() {
		DOS::BlasterConfig c = DOS::parseBlaster("Z99 A210");
		TS_ASSERT_EQUALS(c.port, 0x210);
		TS_ASSERT_EQUALS(c.irq, 5);
	}

	void test_extra_whitespace_between_tokens() {
		DOS::BlasterConfig c = DOS::parseBlaster("  A220   I7  ");
		TS_ASSERT_EQUALS(c.port, 0x220);
		TS_ASSERT_EQUALS(c.irq, 7);
	}
};
