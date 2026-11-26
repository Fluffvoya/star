#include "scene/scene.hpp"

#include "core/slot_map.hpp"

#include <entt/entity/registry.hpp>

#include <atomic>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace star {
namespace {

std::uint64_t allocateSceneId() {
    static std::atomic<std::uint64_t> next{1};
    auto id = next.load(std::memory_order_relaxed);
    do {
        if (id == std::numeric_limits<std::uint64_t>::max()) {
            throw std::length_error("Scene IDs exhausted");
        }
    } while (!next.compare_exchange_weak(id, id + 1, std::memory_order_relaxed));
    return id;
}

struct GuidHash {
    std::size_t operator()(const Guid &guid) const noexcept {
        const auto &bytes = guid.getBytes();
        return std::hash<std::string_view>{}({reinterpret_cast<const char *>(bytes.data()), bytes.size()});
    }
};

struct ObjectEntry {
    std::unique_ptr<GameObject> object;
    entt::entity entity = entt::null;
};

using ObjectStorage = SlotMap<ObjectEntry>;

struct GameObjectRef {
    ObjectHandle handle;
};

} // namespace

struct Scene::Impl {
    const std::uint64_t id = allocateSceneId();
    entt::registry registry;
    ObjectStorage objects;
    std::unordered_map<Guid, ObjectStorage::Handle, GuidHash> by_guid;
    std::vector<ObjectStorage::Handle> pending_destroy;
    std::vector<ObjectStorage::Handle> handles;

    [[nodiscard]] ObjectStorage::Handle resolve(std::uint32_t index, std::uint32_t generation) const noexcept;
};

ObjectStorage::Handle Scene::Impl::resolve(std::uint32_t index, std::uint32_t generation) const noexcept {
    if (index >= handles.size() || handles[index].getGeneration() != generation) {
        return {};
    }
    return handles[index];
}

GameObject::~GameObject() = default;

World &GameObject::getWorld() const noexcept { return m_scene->getWorld(); }

Scene::Scene(World &world) : m_impl(std::make_unique<Impl>()), m_world(world) {}

Scene::~Scene() = default;

std::size_t Scene::getObjectCount() const noexcept { return m_impl->objects.getSize(); }

std::expected<ObjectHandle, SceneError> Scene::createObject(std::string_view name) noexcept {
    try {
        return createObject(name, Guid::generate());
    } catch (...) {
        return std::unexpected(SceneError::CreationFailed);
    }
}

std::expected<ObjectHandle, SceneError> Scene::createObject(std::string_view name, Guid guid) noexcept {
    if (!guid.isValid()) {
        return std::unexpected(SceneError::InvalidGuid);
    }

    Impl &impl = *m_impl;
    if (impl.by_guid.contains(guid)) {
        return std::unexpected(SceneError::DuplicateGuid);
    }
    if (impl.objects.getSize() >= entt::entt_traits<entt::entity>::entity_mask) {
        return std::unexpected(SceneError::ResourceExhausted);
    }

    ObjectStorage::Handle slot;
    entt::entity entity = entt::null;
    try {
        auto object = std::unique_ptr<GameObject>(new GameObject());
        object->m_guid = guid;
        object->m_name = name;
        object->m_scene = this;

        auto inserted = impl.objects.emplace(ObjectEntry{std::move(object)});
        if (!inserted) {
            return std::unexpected(SceneError::ResourceExhausted);
        }
        slot = *inserted;
        if (slot.getIndex() >= impl.handles.size()) {
            impl.handles.resize(static_cast<std::size_t>(slot.getIndex()) + 1);
        }

        const ObjectHandle handle(impl.id, slot.getIndex(), slot.getGeneration());
        entity = impl.registry.create();
        impl.registry.emplace<GameObjectRef>(entity, handle);
        impl.by_guid.emplace(guid, slot);

        ObjectEntry &entry = *impl.objects.get(slot);
        entry.entity = entity;
        entry.object->m_handle = handle;
        impl.handles[slot.getIndex()] = slot;
        return handle;
    } catch (...) {
        if (entity != entt::null) {
            impl.registry.destroy(entity);
        }
        impl.objects.erase(slot);
        return std::unexpected(SceneError::ResourceExhausted);
    }
}

GameObject *Scene::findObject(ObjectHandle handle) noexcept {
    return const_cast<GameObject *>(std::as_const(*this).findObject(handle));
}

const GameObject *Scene::findObject(ObjectHandle handle) const noexcept {
    if (handle.m_scene != m_impl->id) {
        return nullptr;
    }
    const ObjectEntry *entry = m_impl->objects.get(m_impl->resolve(handle.m_index, handle.m_generation));
    return entry == nullptr ? nullptr : entry->object.get();
}

GameObject *Scene::findObject(const Guid &guid) noexcept {
    return const_cast<GameObject *>(std::as_const(*this).findObject(guid));
}

const GameObject *Scene::findObject(const Guid &guid) const noexcept {
    const auto entry = m_impl->by_guid.find(guid);
    return entry == m_impl->by_guid.end() ? nullptr : m_impl->objects.get(entry->second)->object.get();
}

std::expected<void, SceneError> Scene::queueDestroy(ObjectHandle handle) noexcept {
    auto *object = findObject(handle);
    if (object == nullptr) {
        return std::unexpected(SceneError::InvalidHandle);
    }
    if (!object->m_pending_destroy) {
        try {
            m_impl->pending_destroy.push_back(m_impl->resolve(handle.m_index, handle.m_generation));
        } catch (...) {
            return std::unexpected(SceneError::ResourceExhausted);
        }
        object->m_pending_destroy = true;
    }
    return {};
}

void Scene::commitCommands() noexcept {
    Impl &impl = *m_impl;
    for (const auto slot : impl.pending_destroy) {
        const ObjectEntry &entry = *impl.objects.get(slot);
        impl.by_guid.erase(entry.object->getGuid());
        impl.registry.destroy(entry.entity);
        impl.handles[slot.getIndex()] = {};
        impl.objects.erase(slot);
    }
    impl.pending_destroy.clear();
}

} // namespace star
