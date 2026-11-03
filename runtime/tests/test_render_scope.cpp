// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include "video_core/renderer_vulkan/vk_render_scope.h"

// A rejecting driver oracle: any invalid begin/end reaches an assertion.
struct Driver {
    bool rendering = false;
    unsigned begins = 0, ends = 0;
    void Begin() { assert(!rendering); rendering = true; ++begins; }
    void End() { assert(rendering); rendering = false; ++ends; }
};

int main() {
    Vulkan::RenderScope scope;
    Driver driver;
    auto begin = [&] { return scope.BeginCommand([&] { driver.Begin(); }, [&] { driver.End(); }); };
    auto end = [&] { return scope.EndCommand([&] { driver.End(); }); };
    // Reproduces the invalid end observed in the driver crash. It must never
    // reach the driver, including a duplicate end after a balanced scope.
    assert(!end() && driver.ends == 0);
    assert(!begin() && scope.IsOpen());
    assert(end() && !scope.IsOpen());
    assert(!end() && driver.begins == 1 && driver.ends == 1);
    // Producer state can be replaced before queued work executes. Only the
    // executed operations below determine whether a scope is actually open.
    assert(!begin());
    assert(begin()); // closes the old scope before beginning its replacement
    assert(driver.begins == 3 && driver.ends == 2 && scope.IsOpen());
    assert(end());   // submission closes an outstanding begin
    assert(!scope.IsOpen() && !driver.rendering);
    assert(!end());  // an empty submission emits no end
    assert(!begin() && end()); // fresh command buffer, normal balanced work
    assert(driver.begins == driver.ends && !scope.IsOpen());
    puts("PASS: balanced, duplicate-end, replacement-begin and submission-boundary scopes never issue invalid driver commands");
}
