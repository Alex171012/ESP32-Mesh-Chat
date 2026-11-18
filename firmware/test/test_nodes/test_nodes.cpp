/* test_nodes — L1: таблица узлов, online/offline. TEST_PLAN §2.7. */
#include <unity.h>
#include <cstring>
#include "../../lib/mesh_core/node_table.h"

using namespace mesh;

void setUp(void) {}
void tearDown(void) {}

/* Узел online после HELLO, offline после 30 с молчания */
static void test_hello_then_timeout(void) {
    NodeTable t;
    t.onHello(0x1111, "Ноутбук", true, -55, 1000);
    const NodeEntry* e = t.find(0x1111);
    TEST_ASSERT_NOT_NULL(e);
    TEST_ASSERT_TRUE(e->online);
    TEST_ASSERT_TRUE(e->neighbor);

    /* ещё не прошло 30 с */
    t.sweep(1000 + 29000);
    TEST_ASSERT_TRUE(t.find(0x1111)->online);

    /* прошло больше 30 с */
    t.sweep(1000 + 31000);
    TEST_ASSERT_FALSE(t.find(0x1111)->online);
}

/* Возвращается в online при новом HELLO */
static void test_back_online(void) {
    NodeTable t;
    t.onHello(0x1111, "A", true, -55, 1000);
    t.sweep(1000 + 31000);
    TEST_ASSERT_FALSE(t.find(0x1111)->online);

    t.onHello(0x1111, "A", true, -55, 50000);
    TEST_ASSERT_TRUE(t.find(0x1111)->online);
}

/* Разделение соседей и mesh-узлов */
static void test_neighbor_vs_mesh(void) {
    NodeTable t;
    t.onHello(0x0001, "Прямой", true,  -50, 1000);
    t.onHello(0x0002, "Mesh",   false, -70, 1000);
    TEST_ASSERT_EQUAL_INT(2, t.count());
    TEST_ASSERT_EQUAL_INT(1, t.neighborCount());
    TEST_ASSERT_EQUAL_INT(2, t.onlineCount());
}

/* Имя сохраняется, если передано; пустое не затирает */
static void test_name_preserved(void) {
    NodeTable t;
    t.onHello(0x1111, "Ноутбук", true, -55, 1000);
    t.onHello(0x1111, "", true, -55, 2000);
    TEST_ASSERT_EQUAL_STRING("Ноутбук", t.find(0x1111)->name);
}

/* Зарезервированные id не попадают в таблицу */
static void test_reserved_ids_ignored(void) {
    NodeTable t;
    t.onHello(0x0000, "all", true, -55, 1000);
    t.onHello(0xFFFF, "bc",  true, -55, 1000);
    TEST_ASSERT_EQUAL_INT(0, t.count());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hello_then_timeout);
    RUN_TEST(test_back_online);
    RUN_TEST(test_neighbor_vs_mesh);
    RUN_TEST(test_name_preserved);
    RUN_TEST(test_reserved_ids_ignored);
    return UNITY_END();
}
