#pragma once

#include "rhi/vulkan.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace star::rhi {

// Higher ranks are preferred. Vulkan defines the type order, but not a selection policy.
[[nodiscard]] constexpr int deviceTypeRank(VulkanDeviceType type) noexcept {
    switch (type) {
    case VulkanDeviceType::Discrete:
        return 4;
    case VulkanDeviceType::Integrated:
        return 3;
    case VulkanDeviceType::Virtual:
        return 2;
    case VulkanDeviceType::Other:
        return 1;
    case VulkanDeviceType::Cpu:
        return 0;
    }
    return 1;
}

[[nodiscard]] constexpr std::optional<std::size_t>
selectPreferredDeviceIndex(std::span<const VulkanDeviceType> deviceTypes) noexcept {
    std::optional<std::size_t> selected;
    int bestRank = -1;
    for (std::size_t index = 0; index < deviceTypes.size(); ++index) {
        const int rank = deviceTypeRank(deviceTypes[index]);
        if (rank > bestRank) {
            bestRank = rank;
            selected = index;
        }
    }
    return selected;
}

} // namespace star::rhi
