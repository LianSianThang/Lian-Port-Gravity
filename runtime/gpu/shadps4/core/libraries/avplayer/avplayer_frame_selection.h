// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <utility>

namespace Libraries::AvPlayer {

// Retain the newest decoded frame due at the playback clock. Returning each
// superseded buffer lets the decoder refill its bounded pool during catch-up.
// Free-running clients still receive every frame in order.
template <typename Queue, typename Timestamp, typename Recycle>
auto TakeDueVideoFrame(Queue& queue, std::uint64_t now, bool synchronized,
                       Timestamp timestamp, Recycle recycle) -> decltype(queue.Pop()) {
    if (!queue.Size() || (synchronized && timestamp(queue.Front()) > now)) {
        return {};
    }
    auto frame = queue.Pop();
    while (synchronized && queue.Size() && timestamp(queue.Front()) <= now) {
        recycle(*frame);
        frame = queue.Pop();
    }
    return frame;
}

} // namespace Libraries::AvPlayer
