#include "scene/scene.hpp"

#include "core/slot_map.hpp"

#include <entt/entity/registry.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <limits>
#include <map>
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

struct ComponentDeleter {
    const reflect::TypeInfo *type;

    void operator()(void *value) const noexcept {
        type->ops.destroy(value);
        ::operator delete(value, std::align_val_t{std::max(type->alignment, alignof(std::max_align_t))});
    }
};

using ComponentValue = std::unique_ptr<void, ComponentDeleter>;
using ComponentValues = std::map<entt::entity, ComponentValue>;

struct ComponentPool {
    const reflect::TypeInfo *type;
    ComponentValues values;
};

struct ComponentCommand {
    reflect::TypeId type;
    entt::entity entity;
    // An empty node means removal. Added values are fully constructed before they are queued.
    ComponentValues::node_type addition;
};

class MutationScope {
  public:
    explicit MutationScope(bool &active) noexcept : m_active(active) { m_active = true; }
    ~MutationScope() { m_active = false; }

  private:
    bool &m_active;
};

} // namespace

struct Scene::Impl {
    const std::uint64_t id = allocateSceneId();
    entt::registry registry;
    ObjectStorage objects;
    std::unordered_map<Guid, ObjectStorage::Handle, GuidHash> by_guid;
    std::vector<ObjectStorage::Handle> pending_destroy;
    std::vector<ObjectStorage::Handle> handles;
    std::unordered_map<reflect::TypeId, ComponentPool> components;
    std::vector<ComponentCommand> pending_components;
    std::size_t query_depth = 0;
    bool mutating = false;

    [[nodiscard]] ObjectEntry *entry(ObjectHandle handle) noexcept;

    [[nodiscard]] ObjectStorage::Handle resolve(std::uint32_t index, std::uint32_t generation) const noexcept;
};

ObjectStorage::Handle Scene::Impl::resolve(std::uint32_t index, std::uint32_t generation) const noexcept {
    if (index >= handles.size() || handles[index].getGeneration() != generation) {
        return {};
    }
    return handles[index];
}

ObjectEntry *Scene::Impl::entry(ObjectHandle handle) noexcept {
    return handle.m_scene == id ? objects.get(resolve(handle.m_index, handle.m_generation)) : nullptr;
}

GameObject::~GameObject() = default;

World &GameObject::getWorld() const noexcept { return m_scene->getWorld(); }

Scene::Scene(World &world) : m_impl(std::make_unique<Impl>()), m_world(world) {}

Scene::~Scene() {
    m_impl->mutating = true;
    m_impl->pending_components.clear();
    m_impl->components.clear();
}

bool Scene::isBusy() const noexcept { return m_impl->query_depth != 0 || m_impl->mutating; }

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
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
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
    const ObjectEntry *entry = m_impl->entry(handle);
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
    if (m_impl->mutating) {
        return std::unexpected(SceneError::Busy);
    }
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
    if (isBusy()) {
        return;
    }
    MutationScope scope(impl.mutating);
    for (auto &command : impl.pending_components) {
        auto &values = impl.components.at(command.type).values;
        if (command.addition.empty()) {
            values.erase(command.entity);
        } else {
            values.insert(std::move(command.addition));
        }
    }
    impl.pending_components.clear();

    for (const auto slot : impl.pending_destroy) {
        const ObjectEntry &entry = *impl.objects.get(slot);
        impl.by_guid.erase(entry.object->getGuid());
        for (auto &[type, pool] : impl.components) {
            pool.values.erase(entry.entity);
        }
        impl.registry.destroy(entry.entity);
        impl.handles[slot.getIndex()] = {};
        impl.objects.erase(slot);
    }
    impl.pending_destroy.clear();
}

std::expected<void, SceneError> Scene::registerComponent(const reflect::TypeInfo &type) noexcept {
    Impl &impl = *m_impl;
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    if (type.kind != reflect::TypeKind::Struct || type.name.empty() || type.id != reflect::makeTypeId(type.name) ||
        type.size == 0 || !std::has_single_bit(type.alignment) || type.size % type.alignment != 0 ||
        type.ops.construct == nullptr || type.ops.copy_construct == nullptr || type.ops.destroy == nullptr) {
        return std::unexpected(SceneError::InvalidComponent);
    }
    try {
        if (!impl.components.try_emplace(type.id, ComponentPool{&type, {}}).second) {
            return std::unexpected(SceneError::DuplicateComponent);
        }
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

std::expected<void, SceneError> Scene::unregisterComponents(reflect::ModuleId owner) noexcept {
    Impl &impl = *m_impl;
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    MutationScope scope(impl.mutating);
    std::erase_if(impl.pending_components,
                  [&](const auto &command) { return impl.components.at(command.type).type->owner == owner; });
    std::erase_if(impl.components, [&](const auto &entry) { return entry.second.type->owner == owner; });
    return {};
}

void *Scene::findComponent(ObjectHandle handle, reflect::TypeId type) noexcept {
    return const_cast<void *>(std::as_const(*this).findComponent(handle, type));
}

const void *Scene::findComponent(ObjectHandle handle, reflect::TypeId type) const noexcept {
    const Impl &impl = *m_impl;
    if (impl.mutating) {
        return nullptr;
    }
    const auto *object = m_impl->entry(handle);
    const auto pool = impl.components.find(type);
    if (object == nullptr || pool == impl.components.end()) {
        return nullptr;
    }
    const auto value = pool->second.values.find(object->entity);
    return value == pool->second.values.end() ? nullptr : value->second.get();
}

std::expected<void, SceneError> Scene::queueAddComponent(ObjectHandle handle, reflect::TypeId type,
                                                         const void *initial) noexcept {
    Impl &impl = *m_impl;
    if (impl.mutating) {
        return std::unexpected(SceneError::Busy);
    }
    const auto *object = impl.entry(handle);
    if (object == nullptr || object->object->isPendingDestroy()) {
        return std::unexpected(SceneError::InvalidHandle);
    }
    const auto pool = impl.components.find(type);
    if (pool == impl.components.end()) {
        return std::unexpected(SceneError::UnknownComponent);
    }

    MutationScope scope(impl.mutating);
    const auto &info = *pool->second.type;
    try {
        const auto alignment = std::align_val_t{std::max(info.alignment, alignof(std::max_align_t))};
        void *storage = ::operator new(info.size, alignment);
        try {
            if (initial == nullptr) {
                info.ops.construct(storage);
            } else {
                info.ops.copy_construct(storage, initial);
            }
        } catch (...) {
            ::operator delete(storage, alignment);
            return std::unexpected(SceneError::CreationFailed);
        }

        ComponentValue value(storage, ComponentDeleter{&info});
        ComponentValues staged;
        const auto node = staged.emplace(object->entity, std::move(value)).first;
        impl.pending_components.push_back({type, object->entity, staged.extract(node)});
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

std::expected<void, SceneError> Scene::queueRemoveComponent(ObjectHandle handle, reflect::TypeId type) noexcept {
    Impl &impl = *m_impl;
    if (impl.mutating) {
        return std::unexpected(SceneError::Busy);
    }
    const auto *object = impl.entry(handle);
    if (object == nullptr || object->object->isPendingDestroy()) {
        return std::unexpected(SceneError::InvalidHandle);
    }
    if (!impl.components.contains(type)) {
        return std::unexpected(SceneError::UnknownComponent);
    }
    try {
        impl.pending_components.push_back({type, object->entity, {}});
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

std::expected<void, SceneError> Scene::visitComponents(std::span<const reflect::TypeId> types, const void *context,
                                                       ComponentVisitor visit) noexcept {
    Impl &impl = *m_impl;
    if (impl.mutating) {
        return std::unexpected(SceneError::Busy);
    }

    std::vector<const ComponentValues *> pools;
    std::vector<void *> values;
    try {
        pools.reserve(types.size());
        values.resize(types.size());
        for (const auto type : types) {
            const auto pool = impl.components.find(type);
            if (pool == impl.components.end()) {
                return std::unexpected(SceneError::UnknownComponent);
            }
            pools.push_back(&pool->second.values);
        }
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }

    const auto smallest = std::ranges::min_element(pools, {}, [](const auto *pool) { return pool->size(); });
    ++impl.query_depth;
    for (const auto &[entity, value] : **smallest) {
        std::size_t index = 0;
        for (; index < pools.size(); ++index) {
            const auto entry = pools[index]->find(entity);
            if (entry == pools[index]->end()) {
                break;
            }
            values[index] = entry->second.get();
        }
        if (index == pools.size()) {
            visit(context, impl.registry.get<GameObjectRef>(entity).handle, values.data());
        }
    }
    --impl.query_depth;
    return {};
}

} // namespace star
