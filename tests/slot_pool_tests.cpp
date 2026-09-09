#include "thunder/foundation/memory/SlotPool.hpp"
#include "thunder/foundation/memory/SoaCompactor.hpp"

#include <cassert>
#include <cstdio>
#include <numeric>
#include <vector>

using namespace thunder;

static void test_allocate_release_cycle() {
    SlotPool pool;
    std::vector<SlotHandle> handles;
    handles.reserve(100);
    for (int i = 0; i < 100; ++i) handles.push_back(pool.allocate());

    assert(pool.alive_count() == 100);
    assert(pool.capacity() == 100);
    for (auto& h : handles) assert(pool.is_alive(h));

    // Release first 50
    for (int i = 0; i < 50; ++i) pool.release(handles[i]);
    assert(pool.alive_count() == 50);
    assert(pool.free_count() == 50);

    for (int i = 0; i < 50; ++i) assert(!pool.is_alive(handles[i]));
    for (int i = 50; i < 100; ++i) assert(pool.is_alive(handles[i]));

    std::puts("  test_allocate_release_cycle [PASS]");
}

static void test_generation_increment() {
    SlotPool pool;
    auto h1 = pool.allocate();
    assert(h1.generation == 1);
    auto idx = h1.index;

    pool.release(h1);
    assert(!pool.is_alive(h1));

    // Re-allocate — should get same index but different generation
    auto h2 = pool.allocate();
    assert(h2.index == idx);
    assert(h2.generation == 2);
    assert(pool.is_alive(h2));
    assert(!pool.is_alive(h1));  // old handle is stale

    std::puts("  test_generation_increment [PASS]");
}

static void test_free_list_lifo() {
    SlotPool pool;
    auto a = pool.allocate();  // index 0
    auto b = pool.allocate();  // index 1
    auto c = pool.allocate();  // index 2

    pool.release(c);  // free-list: [2]
    pool.release(b);  // free-list: [2, 1]

    // LIFO: re-allocate should give index 1 first, then 2
    auto r1 = pool.allocate();
    assert(r1.index == 1);
    auto r2 = pool.allocate();
    assert(r2.index == 2);

    std::puts("  test_free_list_lifo [PASS]");
}

static void test_bulk_release() {
    SlotPool pool;
    std::vector<SlotHandle> handles;
    for (int i = 0; i < 1000; ++i) handles.push_back(pool.allocate());
    assert(pool.alive_count() == 1000);

    // Bulk release first 500
    std::vector<SlotHandle> to_release(handles.begin(), handles.begin() + 500);
    pool.release_batch(to_release);
    assert(pool.alive_count() == 500);
    assert(pool.free_count() == 500);

    std::puts("  test_bulk_release [PASS]");
}

static void test_compaction_map() {
    SlotPool pool;
    std::vector<SlotHandle> handles;
    for (int i = 0; i < 10; ++i) handles.push_back(pool.allocate());

    // Release indices 2, 4, 6, 8 (every other starting from 2)
    pool.release(handles[2]);
    pool.release(handles[4]);
    pool.release(handles[6]);
    pool.release(handles[8]);
    assert(pool.alive_count() == 6);

    auto map = build_compaction_map(pool);
    assert(map.compacted_size == 6);

    // Alive slots (0,1,3,5,7,9) should map to contiguous 0..5
    assert(map.old_to_new[0] == 0);
    assert(map.old_to_new[1] == 1);
    assert(map.old_to_new[2] == 0xFFFFFFFFu);  // dead
    assert(map.old_to_new[3] == 2);
    assert(map.old_to_new[4] == 0xFFFFFFFFu);  // dead
    assert(map.old_to_new[5] == 3);
    assert(map.old_to_new[6] == 0xFFFFFFFFu);  // dead
    assert(map.old_to_new[7] == 4);
    assert(map.old_to_new[8] == 0xFFFFFFFFu);  // dead
    assert(map.old_to_new[9] == 5);

    std::puts("  test_compaction_map [PASS]");
}

static void test_soa_column_compaction() {
    SlotPool pool;
    std::vector<SlotHandle> handles;
    for (int i = 0; i < 5; ++i) handles.push_back(pool.allocate());

    // Values: {10, 20, 30, 40, 50}
    std::vector<int> col = {10, 20, 30, 40, 50};

    // Release slots 1 and 3 (values 20 and 40)
    pool.release(handles[1]);
    pool.release(handles[3]);

    auto map = build_compaction_map(pool);
    assert(map.compacted_size == 3);

    compact_column(col, map);
    assert(col.size() == 3);
    assert(col[0] == 10);
    assert(col[1] == 30);
    assert(col[2] == 50);

    std::puts("  test_soa_column_compaction [PASS]");
}

static void test_should_compact_heuristic() {
    // Case 1: large free count — should compact
    SlotPool pool1;
    std::vector<SlotHandle> handles1;
    for (int i = 0; i < 5000; ++i) handles1.push_back(pool1.allocate());
    for (int i = 0; i < 2000; ++i) pool1.release(handles1[i]);
    assert(pool1.should_compact() == true);

    // Case 2: small free count — should NOT compact (below 1024 threshold)
    SlotPool pool2;
    std::vector<SlotHandle> handles2;
    for (int i = 0; i < 100; ++i) handles2.push_back(pool2.allocate());
    for (int i = 0; i < 10; ++i) pool2.release(handles2[i]);
    assert(pool2.should_compact() == false);

    std::puts("  test_should_compact_heuristic [PASS]");
}

static void test_pool_apply_compaction() {
    SlotPool pool;
    std::vector<SlotHandle> handles;
    for (int i = 0; i < 20; ++i) handles.push_back(pool.allocate());

    // Kill half
    for (int i = 0; i < 10; ++i) pool.release(handles[i * 2]);

    auto map = build_compaction_map(pool);
    apply_compaction(pool, map);

    assert(pool.capacity() == map.compacted_size);
    assert(pool.alive_count() == map.compacted_size);
    assert(pool.free_count() == 0);

    // All surviving slots should be alive
    for (std::size_t i = 0; i < pool.capacity(); ++i)
        assert(pool.is_index_alive(static_cast<std::uint32_t>(i)));

    std::puts("  test_pool_apply_compaction [PASS]");
}

int main() {
    std::puts("=== SlotPool & SoaCompactor Tests ===");
    test_allocate_release_cycle();
    test_generation_increment();
    test_free_list_lifo();
    test_bulk_release();
    test_compaction_map();
    test_soa_column_compaction();
    test_should_compact_heuristic();
    test_pool_apply_compaction();
    std::puts("All slot pool tests passed!");
    return 0;
}
