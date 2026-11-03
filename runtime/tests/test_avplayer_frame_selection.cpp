// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/libraries/avplayer/avplayer_frame_selection.h"
#include <cassert>
#include <deque>
#include <memory>
#include <optional>
#include <vector>
#include <cstdio>

struct Frame {
    std::uint64_t timestamp;
    std::unique_ptr<int> buffer;
};
struct Queue {
    std::deque<Frame> frames;
    std::size_t Size() const { return frames.size(); }
    const Frame& Front() const { return frames.front(); }
    std::optional<Frame> Pop() {
        if (frames.empty()) return {};
        Frame frame=std::move(frames.front()); frames.pop_front();
        return frame;
    }
    void Push(std::uint64_t pts, int id) { frames.push_back({pts,std::make_unique<int>(id)}); }
};

int main() {
    Queue queue;
    std::vector<std::unique_ptr<int>> returned;
    auto select=[&](std::uint64_t now, bool sync=true) {
        return Libraries::AvPlayer::TakeDueVideoFrame(queue,now,sync,
            [](const Frame& frame) { return frame.timestamp; },
            [&](Frame& stale) { assert(stale.buffer); returned.push_back(std::move(stale.buffer)); });
    };
    assert(!select(0));
    queue.Push(33,1); queue.Push(66,2); queue.Push(99,3); queue.Push(133,4);
    assert(!select(32) && queue.Size()==4 && returned.empty());
    auto first=select(33);
    assert(first && first->timestamp==33 && *first->buffer==1 && returned.empty());
    // A delayed presentation catches up in one request, retaining the future
    // frame and returning each superseded buffer exactly once.
    auto caught_up=select(100);
    assert(caught_up && caught_up->timestamp==99 && *caught_up->buffer==3);
    assert(queue.Size()==1 && queue.Front().timestamp==133);
    assert(returned.size()==1 && *returned[0]==2);
    assert(!select(132));
    auto exact=select(133);
    assert(exact && exact->timestamp==133 && queue.Size()==0);
    queue.Push(0,5); queue.Push(0,6); queue.Push(1,7);
    auto duplicate=select(0);
    assert(duplicate && *duplicate->buffer==6 && *returned.back()==5 && queue.Size()==1);
    queue.Push(2,8);
    auto free_run=select(100,false);
    assert(free_run && *free_run->buffer==7 && queue.Size()==1);
    auto future_free=select(0,false);
    assert(future_free && *future_free->buffer==8 && queue.Size()==0);
    assert(*first->buffer==1 && *caught_up->buffer==3 && *exact->buffer==4);
    std::puts("PASS: synchronized video catches up after a stall; future/duplicate timestamps, buffer ownership and free-running delivery");
}
