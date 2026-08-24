#pragma once

#include <vector>
#include "faircross/domain/fill.hpp"
#include "faircross/engine/batch.hpp"
#include "faircross/engine/allocation.hpp"

namespace faircross {

Result<std::vector<Fill>> generate_canonical_fills(
    const Batch& batch,
    const BatchAllocation& allocation
);

} // namespace faircross
