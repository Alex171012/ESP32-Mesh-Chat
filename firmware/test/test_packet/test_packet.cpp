/* test_packet — L1: разбор и сериализация пакетов. TEST_PLAN §2.6. */
#include <unity.h>
#include <cstring>
#include "../../lib/mesh_core/packet.h"

using namespace mesh;

void setUp(void) {}
void tearDown(void) {}

/* round-trip: сохраняется поле за полем */
static void test_roundtrip_data(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 4;
    p.hdr.flags = FLAG_ACK_REQ;
    p.hdr.dst = 0x0000;
    p.hdr.src = 0x5678;
    p.hdr.session = 0x002A;
    p.hdr.seq = 7;
    p.hdr.hop_count = 0;
    p.hdr.path_len = 2;
    p.body.data.path[0] = 0x78; p.body.data.path[1] = 0x56;
    p.body.data.path[2] = 0xD8; p.body.data.path[3] = 0x56;
    const char* txt = "привет";
    p.body.data.text_len = (uint8_t)strlen(txt);
    memcpy(p.body.data.text, txt, p.body.data.text_len);

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    Packet q;
    TEST_ASSERT_TRUE(deserialize(buf, n, q));
    TEST_ASSERT_EQUAL_UINT8(q.hdr.type, p.hdr.type);
    TEST_ASSERT_EQUAL_UINT16(q.hdr.src, p.hdr.src);
    TEST_ASSERT_EQUAL_UINT16(q.hdr.dst, p.hdr.dst);
    TEST_ASSERT_EQUAL_UINT32(q.hdr.seq, p.hdr.seq);
    TEST_ASSERT_EQUAL_UINT8(q.body.data.text_len, p.body.data.text_len);
    TEST_ASSERT_EQUAL_MEMORY(p.body.data.text, q.body.data.text, p.body.data.text_len);
}

/* обрезанный пакет → ошибка, не падение */
static void test_truncated_packet(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 4;
    p.body.data.text_len = 5;
    memcpy(p.body.data.text, "hello", 5);

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    Packet q;
    for (size_t cut = 0; cut < n; cut++) {
        TEST_ASSERT_FALSE(deserialize(buf, cut, q));
    }
}

/* text_len не бьётся с реальной длиной → отброшен */
static void test_bad_text_len(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 4;
    p.body.data.text_len = 5;
    memcpy(p.body.data.text, "hello", 5);

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    buf[kHeaderSize] = 99;   /* портим байт text_len */

    Packet q;
    TEST_ASSERT_FALSE(deserialize(buf, n, q));
}

/* пакет длиннее 200 байт → отброшен */
static void test_too_long(void) {
    uint8_t big[kMaxPacket + 10];
    memset(big, 0, sizeof(big));
    big[0] = kVersion;
    big[1] = TYPE_DATA;
    Packet q;
    TEST_ASSERT_FALSE(deserialize(big, sizeof(big), q));
}

/* неверная version → отброшен */
static void test_bad_version(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 4;
    p.body.data.text_len = 2;
    memcpy(p.body.data.text, "hi", 2);

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    buf[0] = 99;   /* version */

    Packet q;
    TEST_ASSERT_FALSE(deserialize(buf, n, q));
}

/* русский текст и эмодзи не режутся посередине символа */
static void test_utf8_roundtrip(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 4;
    const char* txt = "Привет мир \xF0\x9F\x8C\x8D";
    p.body.data.text_len = (uint8_t)strlen(txt);
    memcpy(p.body.data.text, txt, p.body.data.text_len);

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    Packet q;
    TEST_ASSERT_TRUE(deserialize(buf, n, q));
    TEST_ASSERT_EQUAL_UINT8(q.body.data.text_len, p.body.data.text_len);
    TEST_ASSERT_EQUAL_MEMORY(p.body.data.text, q.body.data.text, p.body.data.text_len);
}

/* HELLO round-trip */
static void test_hello_roundtrip(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.type = TYPE_HELLO;
    p.hdr.ttl = 1;
    const char* nm = "Ноутбук";
    p.body.hello.name_len = (uint8_t)strlen(nm);
    memcpy(p.body.hello.name, nm, p.body.hello.name_len);
    p.body.hello.neighbor_count = 2;
    p.body.hello.neighbors[0] = 0x78; p.body.hello.neighbors[1] = 0x56;
    p.body.hello.neighbors[2] = 0xD8; p.body.hello.neighbors[3] = 0x56;

    uint8_t buf[kMaxPacket];
    size_t n = serialize(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);

    Packet q;
    TEST_ASSERT_TRUE(deserialize(buf, n, q));
    TEST_ASSERT_EQUAL_UINT8(q.body.hello.name_len, p.body.hello.name_len);
    TEST_ASSERT_EQUAL_MEMORY(p.body.hello.name, q.body.hello.name, p.body.hello.name_len);
    TEST_ASSERT_EQUAL_UINT8(q.body.hello.neighbor_count, 2);
}

/* неизвестный тип пакета → отброшен */
static void test_unknown_type(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.type = 0xEE;
    p.hdr.ttl = 4;
    uint8_t buf[kMaxPacket];
    TEST_ASSERT_EQUAL_UINT32(0, serialize(p, buf, sizeof(buf)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_data);
    RUN_TEST(test_truncated_packet);
    RUN_TEST(test_bad_text_len);
    RUN_TEST(test_too_long);
    RUN_TEST(test_bad_version);
    RUN_TEST(test_utf8_roundtrip);
    RUN_TEST(test_hello_roundtrip);
    RUN_TEST(test_unknown_type);
    return UNITY_END();
}