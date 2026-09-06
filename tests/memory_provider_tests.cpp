#include "../src/monitoring/memory_provider.hpp"

#include <cassert>
#include <cmath>

int main() {
    perfmon::monitoring::MemoryProvider provider;
    perfmon::model::SystemSample sample{};
    provider.Sample(sample);

    const perfmon::model::MemoryInfo& info = sample.memory.info;
    assert(sample.memory.total_utilization.HasValue());
    assert(info.used_gib.HasValue());
    assert(info.available_gib.HasValue());
    assert(info.total_gib.HasValue());
    assert(info.committed_gib.HasValue());
    assert(info.commit_limit_gib.HasValue());
    assert(info.cached_gib.HasValue());
    assert(info.paged_pool_gib.HasValue());
    assert(info.non_paged_pool_gib.HasValue());

    assert(info.total_gib.value > 0.0F);
    assert(info.available_gib.value >= 0.0F);
    assert(info.used_gib.value >= 0.0F);
    assert(info.committed_gib.value >= 0.0F);
    assert(info.commit_limit_gib.value > 0.0F);
    assert(info.cached_gib.value >= 0.0F);
    assert(info.paged_pool_gib.value >= 0.0F);
    assert(info.non_paged_pool_gib.value >= 0.0F);

    const float reconstructed_total = info.used_gib.value + info.available_gib.value;
    assert(std::fabs(reconstructed_total - info.total_gib.value) < 0.05F);
    assert(sample.memory.total_utilization.value >= 0.0F);
    assert(sample.memory.total_utilization.value <= 100.0F);

    // SMBIOS data is optional on some firmware and virtual machines, but any
    // values that are exposed must be internally consistent.
    if (info.speed_mtps.has_value()) {
        assert(*info.speed_mtps > 0U);
    }
    assert(info.slots_used.has_value() == info.slots_total.has_value());
    if (info.slots_used.has_value()) {
        assert(*info.slots_total > 0U);
        assert(*info.slots_used <= *info.slots_total);
    }
    return 0;
}
