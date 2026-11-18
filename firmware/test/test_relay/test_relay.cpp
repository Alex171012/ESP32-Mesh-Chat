/* test_relay — L1: TTL, дедупликация, петли, маршрут, ACK.
 * TEST_PLAN §2.2–2.5. */
#include <unity.h>
#include <cstring>
#include "../../lib/mesh_core/relay.h"
#include "../../lib/mesh_core/msg_id.h"
#include "../../lib/mesh_core/dedup_cache.h"

using namespace mesh;

void setUp(void) {}
void tearDown(void) {}

static Packet makeData(uint16_t src, uint16_t dst, uint8_t ttl, uint32_t seq) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = ttl;
    p.hdr.flags = FLAG_ACK_REQ;
    p.hdr.dst = dst;
    p.hdr.src = src;
    p.hdr.session = 1;
    p.hdr.seq = seq;
    p.hdr.path_len = 0;
    p.body.data.text_len = 1;
    p.body.data.text[0] = 'x';
    return p;
}

/* TTL=4 уменьшается до 0 ровно за 4 пересылки — через 4 разных узла
 * (один и тот же узел не пересылает дважды: глушит дедупликация) */
static void test_ttl_decrements(void) {
    Packet p = makeData(0x0001, 0x0004, 4, 1);
    const uint16_t hops[4] = { 0x0002, 0x0003, 0x0005, 0x0006 };

    for (int i = 0; i < 4; i++) {
        DedupCache dc;                       /* каждый узел — свой кэш */
        Packet out;
        RelayContext ctx = { hops[i], &dc, 1000 };
        Decision d = evaluate(p, ctx, out);
        TEST_ASSERT_TRUE(d == DEC_RELAY || d == DEC_LOCAL_RELAY);
        TEST_ASSERT_TRUE(prepareRelay(out, hops[i]));
        TEST_ASSERT_EQUAL_UINT8(3 - i, out.hdr.ttl);
        TEST_ASSERT_EQUAL_UINT8(i + 1, out.hdr.path_len);
        p = out;
    }

    /* ttl == 0: посредник не пересылает */
    DedupCache dc;
    Packet out;
    RelayContext ctx = { 0x0009, &dc, 1000 };
    TEST_ASSERT_EQUAL_UINT8(DEC_DROP, evaluate(p, ctx, out));
}

/* ttl=0 и я не получатель → отброшен */
static void test_ttl_zero_not_recipient(void) {
    DedupCache dc;
    Packet p = makeData(0x0001, 0x0003, 0, 1);
    RelayContext ctx = { 0x0002, &dc, 1000 };
    Packet out;
    TEST_ASSERT_EQUAL_UINT8(DEC_DROP, evaluate(p, ctx, out));
}

/* ttl=0 и я получатель → обработать локально, не пересылать */
static void test_ttl_zero_recipient(void) {
    DedupCache dc;
    Packet p = makeData(0x0001, 0x0002, 0, 1);
    RelayContext ctx = { 0x0002, &dc, 1000 };
    Packet out;
    TEST_ASSERT_EQUAL_UINT8(DEC_LOCAL, evaluate(p, ctx, out));
}

/* Повторный приём того же msg_id → отброшен */
static void test_dedup_drops_repeat(void) {
    DedupCache dc;
    Packet p = makeData(0x0001, 0x0003, 4, 1);
    Packet out;
    RelayContext ctx = { 0x0002, &dc, 1000 };
    TEST_ASSERT_EQUAL_UINT8(DEC_RELAY, evaluate(p, ctx, out));
    /* тот же пакет ещё раз */
    TEST_ASSERT_EQUAL_UINT8(DEC_DROP, evaluate(p, ctx, out));
}

/* После TTL кэша (30 с) пакет обрабатывается повторно — кэш не «залипает» */
static void test_dedup_expires(void) {
    DedupCache dc;
    Packet p = makeData(0x0001, 0x0003, 4, 1);
    Packet out;
    RelayContext ctx = { 0x0002, &dc, 1000 };
    TEST_ASSERT_EQUAL_UINT8(DEC_RELAY, evaluate(p, ctx, out));
    ctx.now_ms = 1000 + DedupCache::kTtlMs + 1;
    TEST_ASSERT_EQUAL_UINT8(DEC_RELAY, evaluate(p, ctx, out));
}

/* Кольцевой буфер на 64: 65-я запись вытесняет первую */
static void test_dedup_ring_overwrite(void) {
    DedupCache dc;
    /* заполняем 64 разными id */
    for (uint32_t i = 1; i <= 64; i++) {
        dc.remember(makeMsgId(1, 1, i), 1000);
    }
    TEST_ASSERT_TRUE(dc.seen(makeMsgId(1, 1, 1), 1000));
    /* 65-я вытесняет первую */
    dc.remember(makeMsgId(1, 1, 65), 1000);
    TEST_ASSERT_FALSE(dc.seen(makeMsgId(1, 1, 1), 1000));
    TEST_ASSERT_TRUE(dc.seen(makeMsgId(1, 1, 65), 1000));
}

/* src == my_node → отброшен (защита от петли) */
static void test_drop_own_packet(void) {
    DedupCache dc;
    Packet p = makeData(0x0002, 0x0003, 4, 1);   /* src == my */
    RelayContext ctx = { 0x0002, &dc, 1000 };
    Packet out;
    TEST_ASSERT_EQUAL_UINT8(DEC_DROP, evaluate(p, ctx, out));
}

/* my_node уже в path → отброшен (защита от петли по маршруту) */
static void test_drop_path_loop(void) {
    DedupCache dc;
    Packet p = makeData(0x0001, 0x0003, 4, 1);
    p.hdr.path_len = 2;
    /* path = [0x0001, 0x0002] — я тут был */
    p.body.data.path[0] = 0x01; p.body.data.path[1] = 0x00;
    p.body.data.path[2] = 0x02; p.body.data.path[3] = 0x00;
    RelayContext ctx = { 0x0002, &dc, 1000 };
    Packet out;
    TEST_ASSERT_EQUAL_UINT8(DEC_DROP, evaluate(p, ctx, out));
}

/* path накапливается в правильном порядке: 1→2→3 */
static void test_path_accumulates(void) {
    Packet p = makeData(0x0001, 0x0003, 4, 1);
    /* узел 2 */
    TEST_ASSERT_TRUE(prepareRelay(p, 0x0002));
    TEST_ASSERT_EQUAL_UINT8(1, p.hdr.path_len);
    TEST_ASSERT_EQUAL_UINT16(0x0002, p.body.data.path[0] | (p.body.data.path[1] << 8));
    /* узел 3 получает: path = [2] — путь к нему через 2 */
    TEST_ASSERT_EQUAL_UINT8(0, p.hdr.path_len == 1 ? 0 : 1);
}

/* ACK разворачивает path корректно */
static void test_ack_reverses_path(void) {
    Packet data = makeData(0x0001, 0x0003, 4, 1);
    data.hdr.path_len = 2;
    data.body.data.path[0] = 0x02; data.body.data.path[1] = 0x00;   /* 1 */
    data.body.data.path[2] = 0x02; data.body.data.path[3] = 0x00;   /* 2 */

    Packet ack;
    buildAck(data, 0x0003, ACK_OK, ack);

    TEST_ASSERT_EQUAL_UINT8(TYPE_ACK, ack.hdr.type);
    TEST_ASSERT_EQUAL_UINT16(0x0001, ack.hdr.dst);   /* назад к отправителю */

    /* обратный путь: первый hop — узел 2 */
    uint16_t hop = 0;
    TEST_ASSERT_TRUE(nextHopForAck(ack, hop));
    TEST_ASSERT_EQUAL_UINT16(0x0002, hop);

    /* msg_id подтверждаемого сообщения сохранён */
    MsgId orig = msgIdOf(data);
    MsgId got = unpackMsgId(ack.body.ack.msg_id);
    TEST_ASSERT_TRUE(orig == got);
}

/* При обратной пересылке ACK узлы НЕ добавляют себя в path */
static void test_ack_does_not_append_path(void) {
    Packet data = makeData(0x0001, 0x0003, 4, 1);
    data.hdr.path_len = 2;
    data.body.data.path[0] = 0x02; data.body.data.path[1] = 0x00;
    data.body.data.path[2] = 0x02; data.body.data.path[3] = 0x00;

    Packet ack;
    buildAck(data, 0x0003, ACK_OK, ack);
    uint8_t before = ack.hdr.path_len;

    /* prepareRelay не должен добавлять узел в ACK */
    prepareRelay(ack, 0x0002);
    TEST_ASSERT_EQUAL_UINT8(before, ack.hdr.path_len);
}

/* msg_id: разные узлы/сессии/seq дают разные id */
static void test_msg_id_uniqueness(void) {
    MsgId a = makeMsgId(1, 1, 1);
    MsgId b = makeMsgId(2, 1, 1);
    MsgId c = makeMsgId(1, 2, 1);
    MsgId d = makeMsgId(1, 1, 2);
    TEST_ASSERT_FALSE(a == b);
    TEST_ASSERT_FALSE(a == c);
    TEST_ASSERT_FALSE(a == d);
    TEST_ASSERT_TRUE(a == makeMsgId(1, 1, 1));
}

/* Границы seq: 0 и 0xFFFFFFFF */
static void test_msg_id_boundaries(void) {
    uint8_t buf[8];
    packMsgId(makeMsgId(1, 1, 0), buf);
    TEST_ASSERT_TRUE(unpackMsgId(buf) == makeMsgId(1, 1, 0));
    packMsgId(makeMsgId(0xFFFF, 0xFFFF, 0xFFFFFFFF), buf);
    TEST_ASSERT_TRUE(unpackMsgId(buf) == makeMsgId(0xFFFF, 0xFFFF, 0xFFFFFFFF));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ttl_decrements);
    RUN_TEST(test_ttl_zero_not_recipient);
    RUN_TEST(test_ttl_zero_recipient);
    RUN_TEST(test_dedup_drops_repeat);
    RUN_TEST(test_dedup_expires);
    RUN_TEST(test_dedup_ring_overwrite);
    RUN_TEST(test_drop_own_packet);
    RUN_TEST(test_drop_path_loop);
    RUN_TEST(test_path_accumulates);
    RUN_TEST(test_ack_reverses_path);
    RUN_TEST(test_ack_does_not_append_path);
    RUN_TEST(test_msg_id_uniqueness);
    RUN_TEST(test_msg_id_boundaries);
    return UNITY_END();
}
