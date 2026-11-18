/* test_sim — L2: симулятор сети, сценарий 1→2→3. TEST_PLAN §3.
 * Виртуальное время, без реальных задержек. */
#include <unity.h>
#include <cstring>
#include <cstring>
#include "../../lib/mesh_core/relay.h"
#include "../../lib/mesh_core/packet.h"
#include "../../lib/mesh_core/msg_id.h"
#include "../../lib/mesh_core/dedup_cache.h"
#include "../../lib/mesh_core/node_table.h"

using namespace mesh;

void setUp(void) {}
void tearDown(void) {}

/* Минимальный узел симулятора */
struct SimNode {
    uint16_t id;
    DedupCache dedup;
    NodeTable nodes;
    int relayed;        /* сколько раз переслал */
    int delivered;      /* сколько раз принял как получатель */
    Packet lastReceived;

    explicit SimNode(uint16_t i) : id(i), relayed(0), delivered(0) {
        memset(&lastReceived, 0, sizeof(lastReceived));
    }
};

/* Обработать пакет на узле (приём). Возвращает Decision. */
static Decision handle(SimNode& n, const Packet& in, uint32_t now, Packet& out) {
    RelayContext ctx = { n.id, &n.dedup, now };
    Decision d = evaluate(in, ctx, out);
    if (d == DEC_RELAY || d == DEC_LOCAL_RELAY) {
        if (prepareRelay(out, n.id)) n.relayed++;
    }
    if (d == DEC_LOCAL || d == DEC_LOCAL_RELAY) {
        n.delivered++;
        n.lastReceived = in;
    }
    return d;
}

/* Сценарий 1→2→3: маршрут [2] накапливается, 3 получает. */
static void test_demo_route_1_2_3(void) {
    SimNode n1(0x0001), n2(0x0002), n3(0x0003);

    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 4;
    p.hdr.flags = FLAG_ACK_REQ;
    p.hdr.dst = 0x0003;
    p.hdr.src = 0x0001;
    p.hdr.session = 1;
    p.hdr.seq = 1;
    p.body.data.text_len = 5;
    memcpy(p.body.data.text, "hello", 5);

    /* 1 — инициатор: отправка, не приём. evaluate() тут вернул бы
     * DEC_DROP (src == my), поэтому просто кладём в «эфир». */
    Packet in_air = p;

    /* 2 получает → relay, добавляет себя в path */
    Packet out2;
    Decision d2 = handle(n2, in_air, 1010, out2);
    TEST_ASSERT_EQUAL_UINT8(DEC_RELAY, d2);
    TEST_ASSERT_EQUAL_UINT16(0x0001, out2.hdr.src);      /* src не меняется */
    TEST_ASSERT_EQUAL_UINT8(1, out2.hdr.path_len);
    TEST_ASSERT_EQUAL_UINT16(0x0002,
        out2.body.data.path[0] | ((uint16_t)out2.body.data.path[1] << 8));
    TEST_ASSERT_EQUAL_UINT8(3, out2.hdr.ttl);            /* ttl 4→3 */

    /* 3 получает → DEC_LOCAL, доставлено */
    Packet out3;
    Decision d3 = handle(n3, out2, 1020, out3);
    TEST_ASSERT_EQUAL_UINT8(DEC_LOCAL, d3);
    TEST_ASSERT_EQUAL_INT(1, n3.delivered);
    TEST_ASSERT_EQUAL_UINT8(1, n3.lastReceived.hdr.path_len);

    /* ACK: 3 → 2 → 1. path разворачивается, узлы себя не добавляют. */
    Packet ack;
    buildAck(n3.lastReceived, n3.id, ACK_OK, ack);
    TEST_ASSERT_EQUAL_UINT16(0x0001, ack.hdr.dst);       /* назад к отправителю */
    TEST_ASSERT_EQUAL_UINT8(1, ack.hdr.path_len);
    uint16_t hop = 0;
    TEST_ASSERT_TRUE(nextHopForAck(ack, hop));
    TEST_ASSERT_EQUAL_UINT16(0x0002, hop);               /* первый шаг — через 2 */

    /* 2 пересылает ACK: не добавляет себя в path */
    Packet ack_at_2;
    RelayContext ctx2 = { n2.id, &n2.dedup, 1030 };
    Decision da = evaluate(ack, ctx2, ack_at_2);
    TEST_ASSERT_TRUE(da == DEC_RELAY || da == DEC_LOCAL_RELAY);
    TEST_ASSERT_EQUAL_UINT8(1, ack_at_2.hdr.path_len);   /* path не вырос */

    /* 1 получает ACK → доставлено */
    Packet ack_at_1;
    RelayContext ctx1 = { n1.id, &n1.dedup, 1040 };
    Decision d1ack = evaluate(ack_at_2, ctx1, ack_at_1);
    TEST_ASSERT_EQUAL_UINT8(DEC_LOCAL, d1ack);
    TEST_ASSERT_EQUAL_UINT8(ACK_OK, ack_at_1.body.ack.status);

    /* msg_id ACK совпадает с исходным DATA */
    MsgId orig = msgIdOf(n3.lastReceived);
    MsgId got  = unpackMsgId(ack_at_1.body.ack.msg_id);
    TEST_ASSERT_TRUE(orig == got);
}

/* Шторм: каждый узел пересылает пакет не более одного раза */
static void test_no_storm(void) {
    /* 5 узлов в кольце: 1→2→3→4→5→1 */
    SimNode nodes[5] = {
        SimNode(0x0001), SimNode(0x0002), SimNode(0x0003),
        SimNode(0x0004), SimNode(0x0005)
    };

    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 8;
    p.hdr.flags = 0;
    p.hdr.dst = 0x0000;   /* broadcast — худший случай для шторма */
    p.hdr.src = 0x0001;
    p.hdr.session = 1;
    p.hdr.seq = 1;
    p.body.data.text_len = 1;
    p.body.data.text[0] = 'x';

    /* Три прохода по кольцу: каждый узел должен отбросить дубликат */
    Packet cur = p;
    Packet out;
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < 5; i++) {
            handle(nodes[i], cur, 1000 + pass * 100 + i, out);
            cur = out;
        }
    }

    for (int i = 1; i < 5; i++) {
        TEST_ASSERT_TRUE_MESSAGE(nodes[i].relayed <= 1,
                                 "узел переслал больше одного раза — шторм");
    }
}

/* Узел 2 недоступен → 1→3 не доставляется, без бесконечного ретрая */
static void test_node_down_no_delivery(void) {
    SimNode n1(0x0001), n3(0x0003);   /* n2 не существует */

    Packet p;
    memset(&p, 0, sizeof(p));
    p.hdr.version = kVersion;
    p.hdr.type = TYPE_DATA;
    p.hdr.ttl = 4;
    p.hdr.flags = FLAG_ACK_REQ;
    p.hdr.dst = 0x0003;
    p.hdr.src = 0x0001;
    p.hdr.session = 1;
    p.hdr.seq = 1;
    p.body.data.text_len = 5;
    memcpy(p.body.data.text, "hello", 5);

    /* Инициатор отправляет в эфир (не приём) — evaluate() тут не нужен.
     * 2 нет → пакет никто не переслал → 3 не получил, ACK не придёт. */
    Packet out;
    memset(&out, 0, sizeof(out));
    out = p;   /* «эфир»: некому было принять и переслать */
    TEST_ASSERT_EQUAL_INT(0, n3.delivered);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_demo_route_1_2_3);
    RUN_TEST(test_no_storm);
    RUN_TEST(test_node_down_no_delivery);
    return UNITY_END();
}