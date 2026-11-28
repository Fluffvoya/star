#pragma once

#include "scene/system.hpp"

#include <filesystem>

namespace star {

inline constexpr std::uint32_t game_module_abi_version = 1;
inline constexpr std::string_view game_module_entry = "StarGetModuleDescriptor";

enum class GameModuleError : std::uint8_t {
    Busy,
    OpenFailed,
    MissingEntry,
    IncompatibleAbi,
    InvalidDescriptor,
    AlreadyLoaded,
    RegistrationFailed,
    StartupFailed,
    UnknownModule,
    ResourceExhausted,
};

struct GameObjectType {
    reflect::TypeId id;
    std::string_view name;
    std::unique_ptr<GameObject> (*create)() noexcept;
};

template <std::derived_from<GameObject> T>
    requires std::default_initializable<T>
GameObjectType makeGameObjectType(std::string_view name) noexcept {
    return {reflect::makeTypeId(name), name, []() noexcept -> std::unique_ptr<GameObject> {
                try {
                    return std::make_unique<T>();
                } catch (...) {
                    return nullptr;
                }
            }};
}

class STAR_SCENE_API GameModuleContext final {
  public:
    [[nodiscard]] reflect::ModuleId getOwner() const noexcept { return m_owner; }
    [[nodiscard]] std::expected<void, SceneError> registerSystem(SystemDescriptor descriptor,
                                                                 std::unique_ptr<System> system) noexcept;

  private:
    friend class World;
    GameModuleContext(World &world, reflect::ModuleId owner) noexcept : m_world(world), m_owner(owner) {}
    World &m_world;
    reflect::ModuleId m_owner;
};

struct GameModuleDescriptor {
    std::uint32_t abi_version;
    reflect::ModuleId id;
    std::string_view name;
    std::span<const reflect::TypeInfo *const> components;
    std::span<const GameObjectType> objects;
    bool (*startup)(GameModuleContext &) noexcept = nullptr;
    void (*shutdown)() noexcept = nullptr;
};

using GetGameModuleDescriptor = const GameModuleDescriptor *(*)() noexcept;

// Resolves loader dependencies, ABI and the descriptor without registering or starting a module.
[[nodiscard]] STAR_SCENE_API std::expected<reflect::ModuleId, GameModuleError>
probeGameModule(const std::filesystem::path &path) noexcept;

} // namespace star
