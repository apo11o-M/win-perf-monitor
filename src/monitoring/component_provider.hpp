#pragma once

#include "../model/system_sample.hpp"

namespace perfmon::monitoring {

class ComponentProvider {
public:
    virtual ~ComponentProvider() = default;
    virtual void Sample(model::SystemSample& sample) = 0;
};

} // namespace perfmon::monitoring
