#include "rhi/render_graph.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <queue>

namespace star::rhi {

std::expected<RenderTextureHandle, RenderGraphError> RenderGraph::createTexture(std::string_view name,
                                                                                const RenderTextureDesc &description) {
    if (name.empty() || description.width == 0 || description.height == 0) {
        return std::unexpected(name.empty() ? RenderGraphError::EmptyName : RenderGraphError::InvalidResource);
    }
    try {
        m_textures.push_back(Texture{std::string(name), description});
        return RenderTextureHandle{static_cast<std::uint32_t>(m_textures.size() - 1)};
    } catch (...) {
        return std::unexpected(RenderGraphError::AllocationFailed);
    }
}

std::expected<RenderPassHandle, RenderGraphError> RenderGraph::addPass(const RenderPassDesc &description) {
    if (description.name.empty()) {
        return std::unexpected(RenderGraphError::EmptyName);
    }
    for (const RenderResourceUse &use : description.uses) {
        if (!use.resource.isValid() || use.resource.id >= m_textures.size()) {
            return std::unexpected(RenderGraphError::InvalidResource);
        }
    }
    try {
        Pass pass{std::string(description.name), description.type,
                  std::vector<RenderResourceUse>(description.uses.begin(), description.uses.end())};
        m_passes.push_back(std::move(pass));
        return RenderPassHandle{static_cast<std::uint32_t>(m_passes.size() - 1)};
    } catch (...) {
        return std::unexpected(RenderGraphError::AllocationFailed);
    }
}

std::expected<CompiledRenderGraph, RenderGraphError>
RenderGraph::compile(std::span<const RenderTextureHandle> outputs) const try {
    if (m_passes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(RenderGraphError::AllocationFailed);
    }
    const std::size_t passCount = m_passes.size();
    std::vector<std::vector<std::uint32_t>> dependencies(passCount);
    std::vector<std::optional<std::uint32_t>> lastWriters(m_textures.size());
    std::vector<std::vector<std::uint32_t>> readers(m_textures.size());
    const auto addDependency = [&dependencies](std::uint32_t pass, std::uint32_t dependency) {
        if (pass == dependency) {
            return;
        }
        auto &list = dependencies[pass];
        if (std::find(list.begin(), list.end(), dependency) == list.end()) {
            list.push_back(dependency);
        }
    };

    for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        for (const RenderResourceUse &use : m_passes[passIndex].uses) {
            if (!use.resource.isValid() || use.resource.id >= m_textures.size()) {
                return std::unexpected(RenderGraphError::InvalidResource);
            }
            const std::size_t resourceIndex = use.resource.id;
            if (use.write) {
                if (lastWriters[resourceIndex].has_value()) {
                    addDependency(passIndex, *lastWriters[resourceIndex]);
                }
                for (const std::uint32_t reader : readers[resourceIndex]) {
                    addDependency(passIndex, reader);
                }
                readers[resourceIndex].clear();
                lastWriters[resourceIndex] = passIndex;
            } else {
                if (lastWriters[resourceIndex].has_value()) {
                    addDependency(passIndex, *lastWriters[resourceIndex]);
                }
                readers[resourceIndex].push_back(passIndex);
            }
        }
    }

    std::vector<bool> needed(passCount, outputs.empty());
    if (!outputs.empty()) {
        for (const RenderTextureHandle output : outputs) {
            if (!output.isValid() || output.id >= m_textures.size()) {
                return std::unexpected(RenderGraphError::InvalidResource);
            }
            for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
                if (std::any_of(
                        m_passes[passIndex].uses.begin(), m_passes[passIndex].uses.end(),
                        [output](const RenderResourceUse &use) { return use.resource == output && use.write; })) {
                    needed[passIndex] = true;
                }
            }
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
                if (!needed[passIndex]) {
                    continue;
                }
                for (const std::uint32_t dependency : dependencies[passIndex]) {
                    if (!needed[dependency]) {
                        needed[dependency] = true;
                        changed = true;
                    }
                }
            }
        }
    }

    std::vector<std::uint32_t> indegree(passCount);
    std::vector<std::vector<std::uint32_t>> dependents(passCount);
    for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        if (!needed[passIndex]) {
            continue;
        }
        for (const std::uint32_t dependency : dependencies[passIndex]) {
            if (needed[dependency]) {
                ++indegree[passIndex];
                dependents[dependency].push_back(passIndex);
            }
        }
    }
    std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, std::greater<>> ready;
    for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        if (needed[passIndex] && indegree[passIndex] == 0) {
            ready.push(passIndex);
        }
    }
    CompiledRenderGraph compiled;
    while (!ready.empty()) {
        const std::uint32_t passIndex = ready.top();
        ready.pop();
        compiled.passOrder.push_back(RenderPassHandle{passIndex});
        for (const std::uint32_t dependent : dependents[passIndex]) {
            if (--indegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }
    std::size_t neededCount = 0;
    for (const bool value : needed) {
        neededCount += value ? 1U : 0U;
    }
    if (compiled.passOrder.size() != neededCount) {
        return std::unexpected(RenderGraphError::CycleDetected);
    }

    std::vector<std::optional<std::uint32_t>> firstUse(m_textures.size());
    std::vector<std::optional<std::uint32_t>> lastUse(m_textures.size());
    for (std::uint32_t position = 0; position < compiled.passOrder.size(); ++position) {
        const std::uint32_t passIndex = compiled.passOrder[position].id;
        for (const RenderResourceUse &use : m_passes[passIndex].uses) {
            if (!firstUse[use.resource.id].has_value()) {
                firstUse[use.resource.id] = position;
            }
            lastUse[use.resource.id] = position;
        }
    }
    for (std::uint32_t resourceIndex = 0; resourceIndex < m_textures.size(); ++resourceIndex) {
        if (firstUse[resourceIndex].has_value()) {
            compiled.lifetimes.push_back(RenderResourceLifetime{RenderTextureHandle{resourceIndex},
                                                                *firstUse[resourceIndex], *lastUse[resourceIndex]});
        }
    }
    for (std::uint32_t resourceIndex = 0; resourceIndex < m_textures.size(); ++resourceIndex) {
        std::optional<RenderPassHandle> previousPass;
        bool previousWrite = false;
        TextureState previous_state = TextureState::Undefined;
        for (const RenderPassHandle pass : compiled.passOrder) {
            bool used = false;
            bool writes = false;
            TextureState state = TextureState::Undefined;
            for (const RenderResourceUse &use : m_passes[pass.id].uses) {
                if (use.resource.id == resourceIndex) {
                    used = true;
                    writes |= use.write;
                    if (use.state != TextureState::Undefined) {
                        if (state != TextureState::Undefined && state != use.state) {
                            return std::unexpected(RenderGraphError::InvalidPass);
                        }
                        state = use.state;
                    }
                }
            }
            if (!used) {
                continue;
            }
            if (previousPass.has_value() && (previousWrite || writes || previous_state != state)) {
                compiled.barriers.push_back(
                    RenderBarrier{RenderTextureHandle{resourceIndex}, *previousPass, pass, previous_state, state});
            }
            previousPass = pass;
            previousWrite = writes;
            previous_state = state;
        }
    }
    std::vector<RenderResourceLifetime *> transients;
    for (RenderResourceLifetime &lifetime : compiled.lifetimes) {
        const RenderTextureDesc &description = m_textures[lifetime.resource.id].description;
        if (!description.imported && description.usage != TextureUsage::None) {
            transients.push_back(&lifetime);
        }
    }
    std::stable_sort(transients.begin(), transients.end(),
                     [](const auto *left, const auto *right) { return left->firstPass < right->firstPass; });
    std::vector<const RenderResourceLifetime *> slot_owners;
    for (RenderResourceLifetime *lifetime : transients) {
        const Texture &texture = m_textures[lifetime->resource.id];
        const RenderPassHandle first_pass = compiled.passOrder[lifetime->firstPass];
        const auto &uses = m_passes[first_pass.id].uses;
        const bool writes_first = std::any_of(uses.begin(), uses.end(), [lifetime](const RenderResourceUse &use) {
            return use.resource == lifetime->resource && use.write;
        });
        const auto reusable = std::find_if(slot_owners.begin(), slot_owners.end(), [&](const auto *owner) {
            const auto &old = m_textures[owner->resource.id].description;
            const auto &next = texture.description;
            return writes_first && owner->lastPass < lifetime->firstPass && old.width == next.width &&
                   old.height == next.height && old.format == next.format && old.usage == next.usage;
        });
        lifetime->transientSlot = static_cast<std::uint32_t>(reusable - slot_owners.begin());
        if (reusable == slot_owners.end()) {
            compiled.transientTextures.push_back({texture.description, texture.name});
            slot_owners.push_back(lifetime);
        } else {
            compiled.textureReuses.push_back({(*reusable)->resource, lifetime->resource, first_pass});
            *reusable = lifetime;
        }
    }
    return compiled;
} catch (const std::bad_alloc &) {
    return std::unexpected(RenderGraphError::AllocationFailed);
}

std::string_view RenderGraph::getPassName(RenderPassHandle pass) const noexcept {
    return pass.id < m_passes.size() ? std::string_view(m_passes[pass.id].name) : std::string_view{};
}

void RenderGraph::clear() noexcept {
    m_passes.clear();
    m_textures.clear();
}

} // namespace star::rhi
