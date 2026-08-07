#include "../src/model/history_buffer.hpp"

#include <cassert>
#include <vector>

int main() {
    perfmon::model::HistoryBuffer<int> history(3);
    assert(history.Empty());

    history.Push(1);
    history.Push(2);
    assert((history.Snapshot() == std::vector<int>{1, 2}));

    history.Push(3);
    history.Push(4);
    assert(history.Size() == 3);
    assert((history.Snapshot() == std::vector<int>{2, 3, 4}));
    assert(history.Latest() != nullptr && *history.Latest() == 4);

    history.Clear();
    assert(history.Empty());
    return 0;
}
