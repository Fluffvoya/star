#include "device_selection.hpp"

namespace star::rhi {

std::optional<std::size_t> selectPreferredDevice(std::span<const VulkanDeviceType> deviceTypes) noexcept {
    return selectPreferredDeviceIndex(deviceTypes);
}

} // namespace star::rhi
