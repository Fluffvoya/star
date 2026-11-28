#include "scene/world.hpp"

#include "platform/dynamic_library.hpp"

#include <algorithm>
#include <utility>

namespace star {

namespace {

struct OpenGameModule {
    DynamicLibrary library;
    const GameModuleDescriptor *descriptor;
};

std::expected<OpenGameModule, GameModuleError> openGameModule(const std::filesystem::path &path) noexcept {
    auto library = DynamicLibrary::open(path);
    if (!library) {
        return std::unexpected(GameModuleError::OpenFailed);
    }
    const auto symbol = library->findFunction(game_module_entry);
    if (!symbol) {
        return std::unexpected(GameModuleError::MissingEntry);
    }
    const auto entry = reinterpret_cast<GetGameModuleDescriptor>(*symbol);
    const auto *descriptor = entry();
    if (descriptor == nullptr) {
        return std::unexpected(GameModuleError::InvalidDescriptor);
    }
    if (descriptor->abi_version != game_module_abi_version) {
        return std::unexpected(GameModuleError::IncompatibleAbi);
    }
    if (descriptor->id == reflect::builtin_module || descriptor->name.empty()) {
        return std::unexpected(GameModuleError::InvalidDescriptor);
    }
    for (auto object = descriptor->objects.begin(); object != descriptor->objects.end(); ++object) {
        if (object->name.empty() || object->id != reflect::makeTypeId(object->name) || object->create == nullptr ||
            std::find_if(descriptor->objects.begin(), object,
                         [&](const auto &previous) { return previous.id == object->id; }) != object) {
            return std::unexpected(GameModuleError::InvalidDescriptor);
        }
    }

    return OpenGameModule{std::move(*library), descriptor};
}

} // namespace

std::expected<reflect::ModuleId, GameModuleError> probeGameModule(const std::filesystem::path &path) noexcept {
    const auto module = openGameModule(path);
    if (!module) {
        return std::unexpected(module.error());
    }
    return module->descriptor->id;
}

struct World::LoadedGameModule {
    DynamicLibrary library;
    const GameModuleDescriptor *descriptor;
    bool started = false;
};

World::World() = default;

World::~World() {
    m_dispatching = true;
    m_scenes.clear();
    m_systems.clear();
    for (auto it = m_game_modules.rbegin(); it != m_game_modules.rend(); ++it) {
        releaseGameModule(**it);
    }
    m_game_modules.clear();
}

std::expected<void, SceneError> GameModuleContext::registerSystem(SystemDescriptor descriptor,
                                                                  std::unique_ptr<System> system) noexcept {
    return m_world.registerSystem(std::move(descriptor), std::move(system), m_owner);
}

bool World::installGameComponents(Scene &scene) const noexcept {
    for (const auto &module : m_game_modules) {
        for (const auto *component : module->descriptor->components) {
            if (!scene.registerComponent(*component)) {
                return false;
            }
        }
    }
    return true;
}

World::RegisteredObject World::findGameObjectType(reflect::TypeId id) const noexcept {
    for (const auto &module : m_game_modules) {
        const auto &descriptor = *module->descriptor;
        const auto type = std::ranges::find(descriptor.objects, id, &GameObjectType::id);
        if (type != descriptor.objects.end()) {
            return {&*type, descriptor.id};
        }
    }
    return {};
}

std::expected<reflect::ModuleId, GameModuleError> World::loadGameModule(const std::filesystem::path &path) noexcept {
    if (isBusy() || m_loading_module) {
        return std::unexpected(GameModuleError::Busy);
    }
    m_loading_module = true;
    struct Reset {
        bool &flag;
        ~Reset() { flag = false; }
    } reset{m_loading_module};

    auto image = openGameModule(path);
    if (!image) {
        return std::unexpected(image.error());
    }
    const auto *descriptor = image->descriptor;
    if (std::ranges::any_of(m_game_modules, [&](const auto &module) {
            return module->descriptor->id == descriptor->id || module->descriptor->name == descriptor->name;
        })) {
        return std::unexpected(GameModuleError::AlreadyLoaded);
    }

    if (std::ranges::any_of(m_scenes, [&](const auto &scene) { return scene->hasComponentsOwnedBy(descriptor->id); }) ||
        std::ranges::any_of(m_systems, [&](const auto &system) { return system.owner == descriptor->id; })) {
        return std::unexpected(GameModuleError::RegistrationFailed);
    }

    if (std::ranges::any_of(descriptor->objects,
                            [this](const auto &object) { return findGameObjectType(object.id).type != nullptr; })) {
        return std::unexpected(GameModuleError::InvalidDescriptor);
    }

    try {
        auto module = std::make_unique<LoadedGameModule>(LoadedGameModule{std::move(image->library), descriptor});
        m_game_modules.reserve(m_game_modules.size() + 1);

        // Validate scene registration even when there are no loaded scenes.
        Scene probe(*this);
        for (const auto *component : descriptor->components) {
            if (component == nullptr || component->owner != descriptor->id || !probe.registerComponent(*component)) {
                return std::unexpected(GameModuleError::RegistrationFailed);
            }
        }
        const auto registered = m_module_reflection.registerModule(descriptor->id, descriptor->components);
        if (!registered) {
            return std::unexpected(GameModuleError::RegistrationFailed);
        }
        for (const auto &scene : m_scenes) {
            for (const auto *component : descriptor->components) {
                if (!scene->registerComponent(*component)) {
                    releaseGameModule(*module);
                    return std::unexpected(GameModuleError::RegistrationFailed);
                }
            }
        }

        m_game_modules.push_back(std::move(module));
        auto &loaded = *m_game_modules.back();
        loaded.started = true;
        GameModuleContext context(*this, descriptor->id);
        if (descriptor->startup != nullptr && !descriptor->startup(context)) {
            releaseGameModule(loaded);
            m_game_modules.pop_back();
            return std::unexpected(GameModuleError::StartupFailed);
        }
        return descriptor->id;
    } catch (...) {
        return std::unexpected(GameModuleError::ResourceExhausted);
    }
}

void World::releaseGameModule(LoadedGameModule &module) noexcept {
    const bool dispatching = std::exchange(m_dispatching, true);
    const auto owner = module.descriptor->id;
    std::erase_if(m_systems, [owner](const auto &entry) { return entry.owner == owner; });
    for (const auto &scene : m_scenes) {
        scene->releaseModuleObjects(owner);
        static_cast<void>(scene->unregisterComponents(owner));
    }
    m_module_reflection.unregisterModule(owner);
    if (std::exchange(module.started, false) && module.descriptor->shutdown != nullptr) {
        module.descriptor->shutdown();
    }
    m_dispatching = dispatching;
}

std::expected<void, GameModuleError> World::unloadGameModule(reflect::ModuleId owner) noexcept {
    if (isBusy() || m_loading_module) {
        return std::unexpected(GameModuleError::Busy);
    }
    const auto found =
        std::ranges::find(m_game_modules, owner, [](const auto &module) { return module->descriptor->id; });
    if (found == m_game_modules.end()) {
        return std::unexpected(GameModuleError::UnknownModule);
    }
    releaseGameModule(**found);
    m_game_modules.erase(found);
    return {};
}

} // namespace star
