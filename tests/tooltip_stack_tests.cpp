#include "thunder/presentation/ui/TooltipStack.hpp"
#include <cassert>
#include <iostream>

using namespace thunder;

void test_term_parsing() {
    TooltipStack stack;
    UiRect anchor{0,0,0,0}, screen{0,0,1000,1000};
    stack.push_root("", "Click [term:GDP|Gross Domestic Product] to see details", anchor, screen);
    assert(stack.depth() == 1);
    std::cout << "test_term_parsing [PASS]\n";
}

void test_push_pop_lifecycle() {
    TooltipStack stack;
    UiRect anchor{0,0,10,10}, screen{0,0,1000,1000};
    stack.push_root("Title", "Body", anchor, screen);
    assert(stack.depth() == 1);
    
    stack.set_resolver([](const std::string& /*key*/) {
        return std::make_pair(std::string("Child"), std::string("Child Body"));
    });
    stack.push_child("key", anchor, screen);
    assert(stack.depth() == 2);
    
    stack.pop_to(0);
    assert(stack.depth() == 1);
    
    stack.clear();
    assert(stack.empty());
    std::cout << "test_push_pop_lifecycle [PASS]\n";
}

void test_max_depth_limit() {
    TooltipStack stack;
    UiRect anchor{0,0,10,10}, screen{0,0,1000,1000};
    stack.set_resolver([](const std::string& /*key*/) {
        return std::make_pair(std::string("Child"), std::string("Child Body"));
    });
    stack.push_root("Title", "Body", anchor, screen);
    for (int i = 0; i < 10; ++i) {
        stack.push_child("key", anchor, screen);
    }
    assert(stack.depth() == TooltipStack::kMaxDepth);
    std::cout << "test_max_depth_limit [PASS]\n";
}

void test_lock_unlock() {
    TooltipStack stack;
    UiRect anchor{0,0,10,10}, screen{0,0,1000,1000};
    stack.push_root("Title", "Body", anchor, screen);
    stack.lock_current();
    assert(stack.is_locked());
    
    stack.set_resolver([](const std::string& /*key*/) {
        return std::make_pair(std::string("Child"), std::string("Child Body"));
    });
    stack.push_child("key", anchor, screen);
    
    stack.unlock_all();
    assert(!stack.is_locked());
    std::cout << "test_lock_unlock [PASS]\n";
}

void test_cascade_placement() {
    TooltipStack stack;
    UiRect anchor{100,100,10,10}, screen{0,0,1000,1000};
    stack.set_resolver([](const std::string& /*key*/) {
        return std::make_pair(std::string("Child"), std::string("Child Body"));
    });
    stack.push_root("Title", "Body", anchor, screen);
    stack.push_child("key", anchor, screen);
    assert(stack.depth() == 2);
    std::cout << "test_cascade_placement [PASS]\n";
}

void test_render_produces_output() {
    TooltipStack stack;
    UiRect anchor{0,0,10,10}, screen{0,0,1000,1000};
    stack.push_root("Title", "Body", anchor, screen);
    UiDrawList dl;
    stack.render(dl, screen);
    assert(!dl.vertices().empty());
    assert(!dl.text_runs().empty());
    std::cout << "test_render_produces_output [PASS]\n";
}

void test_mouse_over_detection() {
    TooltipStack stack;
    UiRect anchor{100,100,10,10}, screen{0,0,1000,1000};
    stack.push_root("Title", "Body", anchor, screen);
    assert(stack.is_mouse_over_any(120, 110));
    assert(!stack.is_mouse_over_any(0, 0));
    std::cout << "test_mouse_over_detection [PASS]\n";
}

int main() {
    test_term_parsing();
    test_push_pop_lifecycle();
    test_max_depth_limit();
    test_lock_unlock();
    test_cascade_placement();
    test_render_produces_output();
    test_mouse_over_detection();
    return 0;
}
