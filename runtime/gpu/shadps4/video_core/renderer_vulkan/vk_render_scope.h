// SPDX-License-Identifier: GPL-2.0-or-later
// Execution-state guard adapted from LucaPazienza/gravityrush_pc, revision
// 9108d33eeeb25910ce484a64df535028fe8824e0. See GRAVITY_RUNTIME_UPSTREAM.md.
#pragma once

namespace Vulkan {
// Owned by the command-recording executor, not by the producer queuing work.
// Caller-side render-state caching cannot establish what an executed buffer holds.
class RenderScope {
public:
    template <typename Begin, typename End>
    bool BeginCommand(Begin&& begin, End&& end) {
        const bool closed_previous = open;
        if (open) end();
        begin();
        open = true;
        return closed_previous;
    }

    template <typename End>
    bool EndCommand(End&& end) {
        if (!open) return false;
        end();
        open = false;
        return true;
    }

    bool IsOpen() const { return open; }

private:
    bool open = false;
};
} // namespace Vulkan
