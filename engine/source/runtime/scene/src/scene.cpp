#include "scene/world.hpp"

#include "builtin_components.hpp"
#include "core/slot_map.hpp"
#include "scene_archive_access.hpp"
#include "transform_math.hpp"

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

struct ObjectArchiveState {
    serialization::ArchiveValue::Mapping fields;
    serialization::ArchiveValue::Mapping hierarchy_fields;
    std::vector<serialization::ArchiveDocument> unknown_components;
    std::map<reflect::TypeId, serialization::ArchiveValue::Mapping> component_extensions;
};

struct ObjectEntry {
    std::unique_ptr<GameObject> object;
    entt::entity entity = entt::null;
    bool transform_dirty = true;
    bool spawned = false;
    bool created = false;
    std::uint64_t order = 0;
    reflect::ModuleId owner = reflect::builtin_module;
    std::string type_name;
    std::unique_ptr<ObjectArchiveState> archive;
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

std::expected<ComponentValue, SceneError> makeComponentValue(const reflect::TypeInfo &info, const void *initial) {
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
    return ComponentValue(storage, ComponentDeleter{&info});
}

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

struct SpawnCommand {
    std::unique_ptr<GameObject> object;
    Guid guid;
    std::string name;
    reflect::ModuleId owner;
    std::string type_name;
};

struct ParentCommand {
    ObjectHandle child;
    ObjectHandle parent;
    bool keep_world;
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
    std::vector<ParentCommand> pending_parents;
    std::vector<SpawnCommand> pending_spawns;
    const reflect::TypeId transform_id = componentTypeId<Transform>();
    std::atomic_size_t query_depth{0};
    bool mutating = false;
    bool committing = false;
    bool shutting_down = false;
    std::uint64_t next_order = 0;
    reflect::ModuleId callback_owner = reflect::builtin_module;
    template <class F> void invoke(const ObjectEntry &entry, F &&callback) noexcept {
        const auto previous = std::exchange(callback_owner, entry.owner);
        std::invoke(callback, *entry.object);
        callback_owner = previous;
    }
    serialization::ArchiveValue::Mapping archive_fields;

    [[nodiscard]] ObjectEntry *entry(ObjectHandle handle) noexcept;
    Hierarchy &hierarchy(ObjectHandle handle) noexcept;
    Transform &transform(entt::entity entity) noexcept;
    ObjectHandle nextInSubtree(ObjectHandle node, ObjectHandle root) noexcept;
    ObjectHandle pendingParent(ObjectHandle child) noexcept;
    math::Mat4 computeWorld(ObjectHandle node) noexcept;
    std::expected<void, SceneError> applyParent(const ParentCommand &command) noexcept;
    void unlink(Hierarchy &links) noexcept;
    void destroySubtree(ObjectHandle root, EndPlayReason reason) noexcept;
    void refreshActivity(bool playing, bool notify = true) noexcept;
    void propagateTransforms() noexcept;

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

Hierarchy &Scene::Impl::hierarchy(ObjectHandle handle) noexcept {
    return registry.get<Hierarchy>(entry(handle)->entity);
}

Transform &Scene::Impl::transform(entt::entity entity) noexcept {
    return *static_cast<Transform *>(components.at(transform_id).values.at(entity).get());
}

ObjectHandle Scene::Impl::nextInSubtree(ObjectHandle node, ObjectHandle root) noexcept {
    if (const auto first = hierarchy(node).first_child; first != ObjectHandle{}) {
        return first;
    }
    while (node != root) {
        const auto &links = hierarchy(node);
        if (links.next_sibling != ObjectHandle{}) {
            return links.next_sibling;
        }
        node = links.parent;
    }
    return {};
}

ObjectHandle Scene::Impl::pendingParent(ObjectHandle child) noexcept {
    const auto command = std::find_if(pending_parents.rbegin(), pending_parents.rend(),
                                      [child](const auto &item) { return item.child == child; });
    return command == pending_parents.rend() ? hierarchy(child).parent : command->parent;
}

math::Mat4 Scene::Impl::computeWorld(ObjectHandle node) noexcept {
    auto result = math::Mat4::identity();
    while (node != ObjectHandle{}) {
        result = scene_detail::multiply(scene_detail::localMatrix(transform(entry(node)->entity)), result);
        node = hierarchy(node).parent;
    }
    return result;
}

void Scene::Impl::unlink(Hierarchy &links) noexcept {
    if (links.parent == ObjectHandle{}) {
        return;
    }
    auto &parent = hierarchy(links.parent);
    if (links.previous_sibling == ObjectHandle{}) {
        parent.first_child = links.next_sibling;
    } else {
        hierarchy(links.previous_sibling).next_sibling = links.next_sibling;
    }
    if (links.next_sibling == ObjectHandle{}) {
        parent.last_child = links.previous_sibling;
    } else {
        hierarchy(links.next_sibling).previous_sibling = links.previous_sibling;
    }
    --parent.child_count;
    links.parent = {};
    links.previous_sibling = {};
    links.next_sibling = {};
}

std::expected<void, SceneError> Scene::Impl::applyParent(const ParentCommand &command) noexcept {
    auto &child = *entry(command.child);
    if (child.object->isPendingDestroy() ||
        (command.parent != ObjectHandle{} && entry(command.parent)->object->isPendingDestroy())) {
        return {};
    }
    auto &links = hierarchy(command.child);
    if (links.parent == command.parent) {
        return {};
    }
    // Earlier commands can fail to preserve their world transform, so recheck the committed chain.
    for (auto ancestor = command.parent; ancestor != ObjectHandle{}; ancestor = hierarchy(ancestor).parent) {
        if (ancestor == command.child) {
            return std::unexpected(SceneError::HierarchyCycle);
        }
    }
    if (command.keep_world) {
        const auto local = scene_detail::relativeTransform(computeWorld(command.parent), computeWorld(command.child));
        if (!local) {
            return std::unexpected(SceneError::IncompatibleTransform);
        }
        transform(child.entity) = *local;
    }
    unlink(links);
    if (command.parent != ObjectHandle{}) {
        auto &parent = hierarchy(command.parent);
        links.parent = command.parent;
        links.previous_sibling = parent.last_child;
        if (parent.last_child != ObjectHandle{}) {
            hierarchy(parent.last_child).next_sibling = command.child;
        } else {
            parent.first_child = command.child;
        }
        parent.last_child = command.child;
        ++parent.child_count;
    }
    child.transform_dirty = true;
    return {};
}

void Scene::Impl::destroySubtree(ObjectHandle root, EndPlayReason reason) noexcept {
    auto node = root;
    while (true) {
        auto &links = hierarchy(node);
        if (links.first_child != ObjectHandle{}) {
            node = links.first_child;
            continue;
        }
        const auto parent = links.parent;
        const auto slot = resolve(node.m_index, node.m_generation);
        const auto &object = *objects.get(slot);
        object.object->m_pending_destroy = true;
        invoke(object, [&](GameObject &instance) {
            if (std::exchange(instance.m_begun_play, false)) {
                instance.onEndPlay(reason);
            }
            if (std::exchange(instance.m_active_in_hierarchy, false)) {
                instance.onDisable();
            }
            if (object.created) {
                instance.onDestroy();
            }
        });
        unlink(links);
        MutationScope scope(mutating);
        by_guid.erase(object.object->getGuid());
        std::erase_if(pending_components, [&object](const auto &command) { return command.entity == object.entity; });
        std::erase_if(pending_parents,
                      [node](const auto &command) { return command.child == node || command.parent == node; });
        for (auto &[type, pool] : components) {
            pool.values.erase(object.entity);
        }
        registry.destroy(object.entity);
        handles[slot.getIndex()] = {};
        objects.erase(slot);
        if (node == root) {
            return;
        }
        node = parent;
    }
}

void Scene::Impl::refreshActivity(bool playing, bool notify) noexcept {
    for (std::size_t index = 0; index < objects.getSize(); ++index) {
        const auto root = objects.get(objects.getHandleAt(index))->object->getHandle();
        if (hierarchy(root).parent != ObjectHandle{}) {
            continue;
        }
        for (auto node = root; node != ObjectHandle{}; node = nextInSubtree(node, root)) {
            auto &item = *entry(node);
            auto &object = *item.object;
            if (object.m_pending_destroy) {
                continue;
            }
            const auto parent = hierarchy(node).parent;
            const bool active =
                object.m_active && (parent == ObjectHandle{} || entry(parent)->object->m_active_in_hierarchy);
            if (std::exchange(object.m_active_in_hierarchy, active) != active && notify) {
                if (active) {
                    invoke(item, [](GameObject &value) { value.onEnable(); });
                } else {
                    invoke(item, [](GameObject &value) { value.onDisable(); });
                }
            }
            if (notify && playing && active && !object.m_pending_destroy && !std::exchange(object.m_begun_play, true)) {
                invoke(item, [](GameObject &value) { value.onBeginPlay(); });
            }
        }
    }
}

void Scene::Impl::propagateTransforms() noexcept {
    for (std::size_t index = 0; index < objects.getSize(); ++index) {
        const auto root = objects.get(objects.getHandleAt(index))->object->getHandle();
        if (hierarchy(root).parent != ObjectHandle{}) {
            continue;
        }
        for (auto node = root; node != ObjectHandle{}; node = nextInSubtree(node, root)) {
            auto &links = hierarchy(node);
            auto &object = *entry(node);
            auto &world = registry.get<WorldTransform>(object.entity).matrix;
            if (links.parent == ObjectHandle{}) {
                links.depth = 0;
                links.sibling_index = 0;
                if (object.transform_dirty) {
                    world = scene_detail::localMatrix(transform(object.entity));
                }
            } else {
                links.depth = hierarchy(links.parent).depth + 1;
                links.sibling_index =
                    links.previous_sibling == ObjectHandle{} ? 0 : hierarchy(links.previous_sibling).sibling_index + 1;
                const auto &parent = *entry(links.parent);
                if (object.transform_dirty) {
                    world = scene_detail::multiply(registry.get<WorldTransform>(parent.entity).matrix,
                                                   scene_detail::localMatrix(transform(object.entity)));
                }
            }
            if (object.transform_dirty) {
                for (auto child = links.first_child; child != ObjectHandle{}; child = hierarchy(child).next_sibling) {
                    entry(child)->transform_dirty = true;
                }
                object.transform_dirty = false;
            }
        }
    }
}

GameObject::~GameObject() = default;

void GameObject::setName(std::string name) noexcept {
    try {
        m_name = std::move(name);
    } catch (...) {
    }
}

World &GameObject::getWorld() const noexcept { return m_scene->getWorld(); }

const Transform &GameObject::getTransform() const noexcept {
    return *std::as_const(*m_scene).findComponent<Transform>(m_handle);
}

void GameObject::setTransform(const Transform &transform) noexcept {
    if (auto *local = m_scene->findComponent<Transform>(m_handle)) {
        *local = transform;
    }
}

const WorldTransform &GameObject::getWorldTransform() const noexcept { return *m_scene->findWorldTransform(m_handle); }

GameObject *GameObject::getParent() const noexcept {
    return m_scene->findObject(m_scene->findHierarchy(m_handle)->parent);
}

std::expected<void, SceneError> GameObject::setParent(GameObject *parent, bool keep_world_transform) noexcept {
    return m_scene->queueSetParent(m_handle, parent == nullptr ? ObjectHandle{} : parent->getHandle(),
                                   keep_world_transform);
}

std::expected<void, SceneError> GameObject::destroy() noexcept { return m_scene->queueDestroy(m_handle); }

Scene::Scene(World &world) : m_impl(std::make_unique<Impl>()), m_world(world) {
    for (const auto &type : scene_detail::builtinComponents()) {
        if (!registerComponent(type)) {
            throw std::bad_alloc{};
        }
    }
}

Scene::~Scene() {
    auto &impl = *m_impl;
    impl.committing = true;
    impl.shutting_down = true;
    while (impl.objects.getSize() != 0) {
        const auto handle = impl.objects.get(impl.objects.getHandleAt(impl.objects.getSize() - 1))->object->getHandle();
        impl.destroySubtree(handle, EndPlayReason::SceneUnloaded);
    }
    impl.mutating = true;
}

bool Scene::isBusy() const noexcept { return m_impl->query_depth != 0 || m_impl->mutating || m_impl->committing; }

std::size_t Scene::getObjectCount() const noexcept { return m_impl->objects.getSize(); }

std::vector<ObjectHandle> Scene::getObjects() const {
    std::vector<ObjectHandle> result;
    const auto &impl = *m_impl;
    try {
        result.reserve(impl.objects.getSize());
        for (std::size_t index = 0; index < impl.objects.getSize(); ++index) {
            result.push_back(impl.objects.get(impl.objects.getHandleAt(index))->object->getHandle());
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

std::vector<reflect::TypeId> Scene::getComponentTypes(ObjectHandle handle) const {
    std::vector<reflect::TypeId> result;
    const ObjectEntry *object = m_impl->entry(handle);
    if (object == nullptr) {
        return result;
    }
    try {
        for (const auto &[type, pool] : m_impl->components) {
            if (pool.values.find(object->entity) != pool.values.end()) {
                result.push_back(type);
            }
        }
    } catch (...) {
        result.clear();
    }
    return result;
}

std::expected<ObjectHandle, SceneError> Scene::createObject(std::string_view name) noexcept {
    try {
        return createObject(name, Guid::generate());
    } catch (...) {
        return std::unexpected(SceneError::CreationFailed);
    }
}

std::expected<ObjectHandle, SceneError> Scene::createObject(std::string_view name, Guid guid) noexcept {
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    try {
        MutationScope scope(m_impl->committing);
        auto attached = attachObject(std::unique_ptr<GameObject>(new GameObject()), guid, name);
        if (attached) {
            auto &entry = *m_impl->entry(*attached);
            entry.created = true;
            auto &object = *entry.object;
            object.onCreate();
            if (object.m_active && !object.m_pending_destroy) {
                object.m_active_in_hierarchy = true;
                object.onEnable();
            }
        }
        return attached;
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
}

std::expected<ObjectHandle, SceneError> Scene::attachObject(std::unique_ptr<GameObject> object, Guid guid,
                                                            std::string_view name) noexcept {
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
        object->m_guid = guid;
        object->m_name = name;
        object->m_scene = this;

        auto inserted = impl.objects.emplace();
        if (!inserted) {
            return std::unexpected(SceneError::ResourceExhausted);
        }
        slot = *inserted;
        impl.objects.get(slot)->object = std::move(object);
        if (slot.getIndex() >= impl.handles.size()) {
            impl.handles.resize(static_cast<std::size_t>(slot.getIndex()) + 1);
        }

        const ObjectHandle handle(impl.id, slot.getIndex(), slot.getGeneration());
        entity = impl.registry.create();
        impl.registry.emplace<GameObjectRef>(entity, handle);
        impl.registry.emplace<Hierarchy>(entity);
        impl.registry.emplace<WorldTransform>(entity);
        auto &transforms = impl.components.at(impl.transform_id);
        auto transform = makeComponentValue(*transforms.type, nullptr);
        if (!transform) {
            impl.registry.destroy(entity);
            impl.objects.erase(slot);
            return std::unexpected(transform.error());
        }
        transforms.values.emplace(entity, std::move(*transform));
        impl.by_guid.emplace(guid, slot);

        ObjectEntry &entry = *impl.objects.get(slot);
        entry.entity = entity;
        entry.order = impl.next_order++;
        entry.object->m_handle = handle;
        impl.handles[slot.getIndex()] = slot;
        return handle;
    } catch (...) {
        if (entity != entt::null) {
            impl.components.at(impl.transform_id).values.erase(entity);
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

std::expected<SpawnToken, SceneError> Scene::queueSpawn(std::string_view name,
                                                        std::unique_ptr<GameObject> object) noexcept {
    return enqueueSpawn(name, std::move(object), m_impl->callback_owner, {});
}

std::expected<SpawnToken, SceneError> Scene::queueSpawn(std::string_view name, reflect::TypeId type) noexcept {
    if (m_impl->mutating || m_impl->shutting_down) {
        return std::unexpected(SceneError::Busy);
    }
    const auto registered = m_world.findGameObjectType(type);
    if (registered.type == nullptr) {
        return std::unexpected(SceneError::CreationFailed);
    }
    return enqueueSpawn(name, registered.type->create(), registered.owner, registered.type->name);
}

std::expected<SpawnToken, SceneError> Scene::enqueueSpawn(std::string_view name, std::unique_ptr<GameObject> object,
                                                          reflect::ModuleId owner, std::string_view type) noexcept {
    if (m_impl->mutating || m_impl->shutting_down) {
        return std::unexpected(SceneError::Busy);
    }
    if (!object || object->m_scene != nullptr) {
        return std::unexpected(SceneError::CreationFailed);
    }
    try {
        const Guid guid = Guid::generate();
        m_impl->pending_spawns.push_back({std::move(object), guid, std::string(name), owner, std::string(type)});
        return SpawnToken(m_impl->id, guid);
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
}

std::expected<SpawnToken, SceneError> Scene::queueSpawn(std::string_view name) noexcept {
    try {
        return queueSpawn(name, std::unique_ptr<GameObject>(new GameObject()));
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
}

ObjectHandle Scene::resolve(const SpawnToken &token) const noexcept {
    const auto *object = token.m_scene == m_impl->id ? findObject(token.m_guid) : nullptr;
    return object != nullptr && m_impl->entry(object->getHandle())->spawned ? object->getHandle() : ObjectHandle{};
}

std::expected<void, SceneError> Scene::queueDestroy(ObjectHandle handle) noexcept {
    if (m_impl->mutating || m_impl->shutting_down) {
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
        for (auto node = handle; node != ObjectHandle{}; node = m_impl->nextInSubtree(node, handle)) {
            m_impl->entry(node)->object->m_pending_destroy = true;
        }
    }
    return {};
}

std::expected<void, SceneError> Scene::commitCommands() noexcept {
    Impl &impl = *m_impl;
    if (isBusy()) {
        return std::unexpected(SceneError::Busy);
    }
    MutationScope committing(impl.committing);
    auto components = std::exchange(impl.pending_components, {});
    auto parents = std::exchange(impl.pending_parents, {});
    auto destroys = std::exchange(impl.pending_destroy, {});
    auto spawns = std::exchange(impl.pending_spawns, {});
    {
        MutationScope mutating(impl.mutating);
        for (auto &command : components) {
            auto &values = impl.components.at(command.type).values;
            if (command.addition.empty()) {
                values.erase(command.entity);
            } else {
                values.insert(std::move(command.addition));
            }
        }
        components.clear();
    }

    std::expected<void, SceneError> result;
    for (const auto &command : parents) {
        const auto applied = impl.applyParent(command);
        if (!applied && result) {
            result = std::unexpected(applied.error());
        }
    }

    for (const auto slot : destroys) {
        const auto *entry = impl.objects.get(slot);
        if (entry != nullptr) {
            impl.destroySubtree(entry->object->getHandle(), EndPlayReason::Destroyed);
        }
    }
    for (auto &spawn : spawns) {
        const auto attached = attachObject(std::move(spawn.object), spawn.guid, spawn.name);
        if (attached) {
            auto &entry = *impl.entry(*attached);
            entry.spawned = true;
            entry.created = true;
            entry.owner = spawn.owner;
            entry.type_name = std::move(spawn.type_name);
            impl.invoke(entry, [](GameObject &object) { object.onCreate(); });
        } else if (result) {
            result = std::unexpected(attached.error());
        }
    }

    impl.refreshActivity(m_world.isPlaying());
    impl.propagateTransforms();
    return result;
}

void Scene::endPlay(EndPlayReason reason) noexcept {
    auto &impl = *m_impl;
    MutationScope committing(impl.committing);
    for (std::size_t index = 0; index < impl.objects.getSize(); ++index) {
        auto &entry = *impl.objects.get(impl.objects.getHandleAt(index));
        if (std::exchange(entry.object->m_begun_play, false)) {
            impl.invoke(entry, [reason](GameObject &object) { object.onEndPlay(reason); });
        }
    }
}

bool Scene::hasComponentsOwnedBy(reflect::ModuleId owner) const noexcept {
    return std::ranges::any_of(m_impl->components,
                               [owner](const auto &pool) { return pool.second.type->owner == owner; });
}

void Scene::releaseModuleObjects(reflect::ModuleId owner) noexcept {
    auto &impl = *m_impl;
    MutationScope committing(impl.committing);
    MutationScope shutdown(impl.shutting_down);
    std::erase_if(impl.pending_spawns, [owner](const auto &spawn) { return spawn.owner == owner; });
    for (std::size_t index = 0; index < impl.objects.getSize(); ++index) {
        const auto &entry = *impl.objects.get(impl.objects.getHandleAt(index));
        if (entry.owner != owner) {
            continue;
        }
        for (auto child = impl.hierarchy(entry.object->getHandle()).first_child; child != ObjectHandle{};) {
            const auto next = impl.hierarchy(child).next_sibling;
            if (impl.entry(child)->owner != owner && !impl.entry(child)->object->isPendingDestroy()) {
                auto &links = impl.hierarchy(child);
                const auto local = scene_detail::relativeTransform(math::Mat4::identity(), impl.computeWorld(child));
                auto &target = *impl.entry(child);
                if (local) {
                    impl.transform(target.entity) = *local;
                }
                impl.unlink(links);
                target.transform_dirty = true;
            }
            child = next;
        }
    }
    std::size_t index = 0;
    while (index < impl.objects.getSize()) {
        const auto &entry = *impl.objects.get(impl.objects.getHandleAt(index));
        if (entry.owner == owner) {
            impl.destroySubtree(entry.object->getHandle(), EndPlayReason::Destroyed);
        } else {
            ++index;
        }
    }
    impl.propagateTransforms();
}

std::expected<void, SceneError> GameObject::addTickPrerequisite(const GameObject &other) noexcept {
    if (m_scene == nullptr || &other == this || other.m_scene != m_scene || other.m_pending_destroy) {
        return std::unexpected(SceneError::InvalidHandle);
    }
    if (std::ranges::find(m_tick_prerequisites, other.m_handle) != m_tick_prerequisites.end()) {
        return {};
    }
    try {
        m_tick_prerequisites.push_back(other.m_handle);
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

std::expected<void, SceneError> Scene::tickObjects(TickGroup group, float dt, std::uint64_t frame) noexcept {
    auto &impl = *m_impl;
    MutationScope ticking(impl.committing);
    auto eligible = [frame](const GameObject &object) {
        return object.m_tick_enabled && object.m_begun_play && object.m_active_in_hierarchy &&
               !object.m_pending_destroy && object.m_last_tick != frame;
    };
    try {
        struct TickNode {
            GameObject *object;
            std::size_t prerequisites = 0;
            std::vector<std::size_t> dependents;
        };
        std::vector<TickNode> nodes;
        std::unordered_map<const GameObject *, std::size_t> index_by_object;
        for (std::size_t i = 0; i < impl.objects.getSize(); ++i) {
            auto &object = *impl.objects.get(impl.objects.getHandleAt(i))->object;
            if (eligible(object) && object.m_tick_group == group) {
                index_by_object.emplace(&object, nodes.size());
                nodes.push_back({&object, 0, {}});
            }
        }

        std::vector<GameObject *> order;
        order.reserve(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            auto &node = nodes[i];
            for (const auto handle : node.object->m_tick_prerequisites) {
                const auto *other = findObject(handle);
                if (other == nullptr || !eligible(*other)) {
                    continue;
                }
                if (other->m_tick_group > group) {
                    return std::unexpected(SceneError::ScheduleCycle);
                }
                if (const auto index = index_by_object.find(other); index != index_by_object.end()) {
                    ++node.prerequisites;
                    nodes[index->second].dependents.push_back(i);
                }
            }
            if (node.prerequisites == 0) {
                order.push_back(node.object);
            }
        }
        for (std::size_t i = 0; i < order.size(); ++i) {
            const auto &node = nodes[index_by_object.at(order[i])];
            for (const auto dependent : node.dependents) {
                auto &next = nodes[dependent];
                if (--next.prerequisites == 0) {
                    order.push_back(next.object);
                }
            }
        }
        if (order.size() != nodes.size()) {
            return std::unexpected(SceneError::ScheduleCycle);
        }
        for (auto *object : order) {
            if (eligible(*object) && object->m_tick_group == group) {
                object->m_last_tick = frame;
                impl.invoke(*impl.entry(object->getHandle()), [dt](GameObject &value) { value.onTick(dt); });
            }
        }
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

const Hierarchy *Scene::findHierarchy(ObjectHandle handle) const noexcept {
    const auto *object = m_impl->entry(handle);
    return object == nullptr ? nullptr : m_impl->registry.try_get<Hierarchy>(object->entity);
}

const WorldTransform *Scene::findWorldTransform(ObjectHandle handle) const noexcept {
    const auto *object = m_impl->entry(handle);
    return object == nullptr ? nullptr : m_impl->registry.try_get<WorldTransform>(object->entity);
}

std::expected<Transform, SceneError> Scene::localTransformForWorld(ObjectHandle handle,
                                                                   const math::Mat4 &world) const noexcept {
    const auto *hierarchy = findHierarchy(handle);
    if (!hierarchy) {
        return std::unexpected(SceneError::InvalidHandle);
    }
    if (auto local = scene_detail::relativeTransform(m_impl->computeWorld(hierarchy->parent), world)) {
        return *local;
    }
    return std::unexpected(SceneError::IncompatibleTransform);
}

std::expected<void, SceneError> Scene::queueSetParent(ObjectHandle child, ObjectHandle parent,
                                                      bool keep_world_transform) noexcept {
    Impl &impl = *m_impl;
    if (impl.mutating || impl.shutting_down) {
        return std::unexpected(SceneError::Busy);
    }
    const auto *child_object = findObject(child);
    const auto *parent_object = findObject(parent);
    if (child_object == nullptr || child_object->isPendingDestroy() ||
        (parent != ObjectHandle{} && (parent_object == nullptr || parent_object->isPendingDestroy()))) {
        return std::unexpected(SceneError::InvalidHandle);
    }
    for (auto ancestor = parent; ancestor != ObjectHandle{}; ancestor = impl.pendingParent(ancestor)) {
        if (ancestor == child) {
            return std::unexpected(SceneError::HierarchyCycle);
        }
    }
    try {
        impl.pending_parents.push_back({child, parent, keep_world_transform});
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

const reflect::TypeInfo *Scene::findComponentType(reflect::TypeId type) const noexcept {
    const auto pool = m_impl->components.find(type);
    return pool == m_impl->components.end() ? nullptr : pool->second.type;
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
    if (owner == reflect::builtin_module) {
        return std::unexpected(SceneError::RequiredComponent);
    }
    MutationScope scope(impl.mutating);
    std::erase_if(impl.pending_components,
                  [&](const auto &command) { return impl.components.at(command.type).type->owner == owner; });
    std::erase_if(impl.components, [&](const auto &entry) { return entry.second.type->owner == owner; });
    return {};
}

void *Scene::findComponent(ObjectHandle handle, reflect::TypeId type) noexcept {
    if (m_impl->mutating) {
        return nullptr;
    }
    auto *value = const_cast<void *>(std::as_const(*this).findComponent(handle, type));
    if (value != nullptr && type == m_impl->transform_id) {
        m_impl->entry(handle)->transform_dirty = true;
    }
    return value;
}

const void *Scene::findComponent(ObjectHandle handle, reflect::TypeId type) const noexcept {
    const Impl &impl = *m_impl;
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
    if (impl.mutating || impl.shutting_down) {
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

    if (type == impl.transform_id) {
        return std::unexpected(SceneError::RequiredComponent);
    }
    MutationScope scope(impl.mutating);
    try {
        auto value = makeComponentValue(*pool->second.type, initial);
        if (!value) {
            return std::unexpected(value.error());
        }
        ComponentValues staged;
        const auto node = staged.emplace(object->entity, std::move(*value)).first;
        impl.pending_components.push_back({type, object->entity, staged.extract(node)});
    } catch (...) {
        return std::unexpected(SceneError::ResourceExhausted);
    }
    return {};
}

std::expected<void, SceneError> Scene::queueRemoveComponent(ObjectHandle handle, reflect::TypeId type) noexcept {
    Impl &impl = *m_impl;
    if (impl.mutating || impl.shutting_down) {
        return std::unexpected(SceneError::Busy);
    }
    const auto *object = impl.entry(handle);
    if (object == nullptr || object->object->isPendingDestroy()) {
        return std::unexpected(SceneError::InvalidHandle);
    }
    if (type == impl.transform_id) {
        return std::unexpected(SceneError::RequiredComponent);
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

std::expected<void, SceneError> Scene::visitComponents(std::span<const reflect::TypeId> types,
                                                       std::span<const bool> writable, const void *context,
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
    bool writes_transform = false;
    for (std::size_t index = 0; index < types.size(); ++index) {
        writes_transform |= writable[index] && types[index] == impl.transform_id;
    }
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
            const auto handle = std::as_const(impl.registry).get<GameObjectRef>(entity).handle;
            if (writes_transform) {
                impl.entry(handle)->transform_dirty = true;
            }
            visit(context, handle, values.data());
        }
    }
    --impl.query_depth;
    return {};
}

bool SceneArchiveAccess::isBusy(const Scene &scene) noexcept { return scene.isBusy(); }

serialization::ArchiveValue::Mapping &SceneArchiveAccess::sceneFields(Scene &scene) noexcept {
    return scene.m_impl->archive_fields;
}

const serialization::ArchiveValue::Mapping &SceneArchiveAccess::sceneFields(const Scene &scene) noexcept {
    return scene.m_impl->archive_fields;
}

std::expected<std::vector<ArchivedObject>, serialization::ArchiveError>
SceneArchiveAccess::capture(const Scene &scene, ObjectHandle root, serialization::CapturePolicy policy) {
    using namespace serialization;
    auto &impl = *scene.m_impl;
    std::vector<ArchivedObject> records;
    if (scene.isBusy() || (root != ObjectHandle{} && impl.entry(root) == nullptr)) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/GameObjects"});
    }
    auto capture_object = [&](const ObjectEntry &entry) -> std::expected<void, ArchiveError> {
        const auto &object = *entry.object;
        const auto &links = impl.hierarchy(object.getHandle());
        const auto parent = links.parent != ObjectHandle{} && object.getHandle() != root
                                ? impl.entry(links.parent)->object->getGuid()
                                : Guid{};
        ArchivedObject record{object.getGuid(),
                              parent,
                              object.getName(),
                              entry.order,
                              object.getHandle() == root ? 0 : links.sibling_index,
                              object.m_active,
                              {},
                              {},
                              {}};
        if (entry.archive) {
            record.fields = entry.archive->fields;
            record.hierarchy_fields = entry.archive->hierarchy_fields;
            record.components = entry.archive->unknown_components;
        }
        if (!entry.type_name.empty()) {
            record.fields.insert_or_assign("Type", ArchiveValue{entry.type_name});
        }
        for (const auto &[id, pool] : impl.components) {
            if (entry.archive && std::ranges::any_of(entry.archive->unknown_components, [&](const auto &unknown) {
                    return unknown.type_name == pool.type->name || unknown.type_name == pool.type->former_name;
                })) {
                continue;
            }
            const auto value = pool.values.find(entry.entity);
            if (value == pool.values.end()) {
                continue;
            }
            const ArchiveValue::Mapping empty;
            const auto *extensions = &empty;
            if (entry.archive) {
                const auto known = entry.archive->component_extensions.find(id);
                if (known != entry.archive->component_extensions.end()) {
                    extensions = &known->second;
                }
            }
            auto document = captureObject(*pool.type, value->second.get(), *extensions, policy);
            if (!document) {
                document.error().path.insert(0, "/GameObjects/" + object.getGuid().toString() + "/Components/" +
                                                    std::string(pool.type->name));
                return std::unexpected(std::move(document.error()));
            }
            if (id != impl.transform_id || !document->fields.empty()) {
                record.components.push_back(std::move(*document));
            }
        }
        std::ranges::sort(record.components, {}, &ArchiveDocument::type_name);
        records.push_back(std::move(record));
        return {};
    };
    if (root == ObjectHandle{}) {
        for (std::size_t i = 0; i < impl.objects.getSize(); ++i) {
            if (auto result = capture_object(*impl.objects.get(impl.objects.getHandleAt(i))); !result) {
                return std::unexpected(std::move(result.error()));
            }
        }
    } else {
        for (auto node = root; node != ObjectHandle{}; node = impl.nextInSubtree(node, root)) {
            if (auto result = capture_object(*impl.entry(node)); !result) {
                return std::unexpected(std::move(result.error()));
            }
        }
    }
    return records;
}

std::expected<void, serialization::ArchiveError>
SceneArchiveAccess::restore(Scene &scene, std::span<ArchivedObject> objects, serialization::LoadPolicy policy,
                            std::vector<serialization::ArchiveError> &diagnostics, SceneSnapshotRestore mode) {
    using namespace serialization;
    auto &impl = *scene.m_impl;
    if (scene.isBusy()) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, "/GameObjects"});
    }
    MutationScope committing(impl.committing);
    const auto original_order = impl.next_order;
    std::vector<ObjectHandle> attached;
    struct Rollback {
        Scene::Impl &impl;
        std::vector<ObjectHandle> &objects;
        std::uint64_t original_order;
        bool completed = false;
        ~Rollback() {
            if (!completed) {
                for (const auto handle : objects) {
                    if (impl.entry(handle) != nullptr) {
                        impl.destroySubtree(handle, EndPlayReason::Destroyed);
                    }
                }
                impl.next_order = original_order;
            }
        }
    } rollback{impl, attached, original_order};

    std::vector<std::pair<ComponentValue *, ComponentValue>> authored_values;
    attached.reserve(objects.size());
    for (auto &record : objects) {
        const auto location = "/GameObjects/" + record.guid.toString();
        World::RegisteredObject registered;
        if (const auto type = record.fields.find("Type"); type != record.fields.end()) {
            if (const auto *name = std::get_if<std::string>(&type->second.value); name && *name != "star::GameObject") {
                registered = scene.m_world.findGameObjectType(reflect::makeTypeId(*name));
                if (registered.type != nullptr && registered.type->name != *name) {
                    registered = {};
                }
            }
        }
        auto object =
            registered.type == nullptr ? std::unique_ptr<GameObject>(new GameObject()) : registered.type->create();
        if (!object) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, location + "/Type"});
        }
        auto handle = scene.attachObject(std::move(object), record.guid, record.name);
        if (!handle) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, location});
        }
        attached.push_back(*handle);
        auto &entry = *impl.entry(*handle);
        entry.object->m_active = record.active;
        if (registered.type != nullptr) {
            entry.owner = registered.owner;
            entry.type_name = registered.type->name;
        }
        auto state = std::make_unique<ObjectArchiveState>();
        state->fields = std::move(record.fields);
        state->hierarchy_fields = std::move(record.hierarchy_fields);
        if (!state->hierarchy_fields.empty()) {
            ArchiveError diagnostic{ArchiveErrorCode::UnknownField, location + "/Hierarchy"};
            if (policy == LoadPolicy::Strict) {
                return std::unexpected(std::move(diagnostic));
            }
            diagnostics.push_back(std::move(diagnostic));
        }
        if (const auto type = state->fields.find("Type"); type != state->fields.end()) {
            const auto *name = std::get_if<std::string>(&type->second.value);
            if (name == nullptr || (*name != "star::GameObject" && registered.type == nullptr)) {
                ArchiveError diagnostic{ArchiveErrorCode::TypeMismatch, location + "/Type"};
                if (policy == LoadPolicy::Strict) {
                    return std::unexpected(std::move(diagnostic));
                }
                diagnostics.push_back(std::move(diagnostic));
            }
        }

        for (const auto &[name, value] : state->fields) {
            const auto *properties = std::get_if<ArchiveValue::Mapping>(&value.value);
            if (name == "Type" || name == "Version" || name == "PrefabInstance" ||
                (name == "Properties" && properties != nullptr && properties->empty())) {
                continue;
            }
            ArchiveError diagnostic{ArchiveErrorCode::UnknownField, location + "/" + name};
            if (policy == LoadPolicy::Strict) {
                return std::unexpected(std::move(diagnostic));
            }
            diagnostics.push_back(std::move(diagnostic));
        }

        for (auto &document : record.components) {
            const auto component_path = location + "/Components/" + document.type_name;
            auto pool = impl.components.find(reflect::makeTypeId(document.type_name));
            if (pool == impl.components.end()) {
                pool = std::ranges::find_if(impl.components, [&](const auto &candidate) {
                    return candidate.second.type->former_name == document.type_name;
                });
            }
            if (pool == impl.components.end()) {
                ArchiveError diagnostic{ArchiveErrorCode::TypeMismatch, component_path};
                if (policy == LoadPolicy::Strict) {
                    return std::unexpected(std::move(diagnostic));
                }
                diagnostics.push_back(std::move(diagnostic));
                state->unknown_components.push_back(std::move(document));
                continue;
            }

            if (document.version > pool->second.type->version && policy == LoadPolicy::Preserve) {
                diagnostics.push_back({ArchiveErrorCode::UnsupportedVersion, component_path + "/Version"});
                state->unknown_components.push_back(std::move(document));
                continue;
            }
            auto &values = pool->second.values;
            auto value = values.find(entry.entity);
            if (value == values.end()) {
                auto initial = makeComponentValue(*pool->second.type, nullptr);
                if (!initial) {
                    return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, component_path});
                }
                value = values.emplace(entry.entity, std::move(*initial)).first;
            }
            if (state->component_extensions.contains(pool->first)) {
                return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, component_path});
            }
            auto applied = applyObject(document, *pool->second.type, value->second.get(), policy);
            if (!applied) {
                applied.error().path.insert(0, component_path);
                return std::unexpected(std::move(applied.error()));
            }
            for (auto &diagnostic : applied->diagnostics) {
                diagnostic.path.insert(0, component_path);
                diagnostics.push_back(std::move(diagnostic));
            }
            if (mode == SceneSnapshotRestore::Reload) {
                auto saved = makeComponentValue(*pool->second.type, value->second.get());
                if (!saved) {
                    return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, component_path});
                }
                authored_values.emplace_back(&value->second, std::move(*saved));
            }
            state->component_extensions.emplace(pool->first, std::move(applied->extensions));
        }
        entry.archive = std::move(state);
    }

    std::vector<std::size_t> sibling_order;
    sibling_order.reserve(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].parent.isValid()) {
            sibling_order.push_back(i);
        }
    }
    std::ranges::stable_sort(sibling_order, {}, [&](auto i) { return objects[i].sibling; });
    for (const auto i : sibling_order) {
        const auto *parent = scene.findObject(objects[i].parent);
        if (parent == nullptr || !impl.applyParent({attached[i], parent->getHandle(), false})) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput,
                                                "/GameObjects/" + objects[i].guid.toString() + "/Hierarchy"});
        }
    }

    impl.propagateTransforms();
    for (const auto handle : attached) {
        auto &entry = *impl.entry(handle);
        entry.created = true;
        impl.invoke(entry, [](GameObject &object) { object.onCreate(); });
    }
    impl.refreshActivity(false);
    if (mode == SceneSnapshotRestore::Reload) {
        // Lifecycle hooks initialize the new instances. The captured scene still owns its authored state.
        for (auto &[destination, saved] : authored_values) {
            saved.get_deleter().type->ops.swap(destination->get(), saved.get());
        }
        impl.pending_components.clear();
        impl.pending_parents.clear();
        impl.pending_destroy.clear();
        impl.pending_spawns.clear();
        for (std::size_t i = 0; i < attached.size(); ++i) {
            auto &object = *impl.entry(attached[i])->object;
            object.m_name = std::move(objects[i].name);
            object.m_active = objects[i].active;
            object.m_pending_destroy = false;
        }
        impl.refreshActivity(false, false);
    }
    impl.propagateTransforms();
    rollback.completed = true;
    return {};
}

} // namespace star
