#include "rhi/render_graph.hpp"

#include <algorithm>
#include <limits>
#include <optional>

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
RenderGraph::compile(std::span<const RenderTextureHandle> outputs) const {
    if (m_passes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(RenderGraphError::AllocationFailed);
    }
    const std::size_t passCount = m_passes.size();
    std::vector<std::vector<std::uint32_t>> dependencies(passCount);
    std::vector<std::optional<std::uint32_t>> lastWriters(m_textures.size());
    std::vector<std::vector<std::uint32_t>> readers(m_textures.size());
    const auto addDependency = [&dependencies](std::uint32_t pass, std::uint32_t dependency) {
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
                if (std::any_of(m_passes[passIndex].uses.begin(), m_passes[passIndex].uses.end(),
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
    std::vector<std::uint32_t> ready;
    for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex) {
        if (needed[passIndex] && indegree[passIndex] == 0) {
            ready.push_back(passIndex);
        }
    }
    CompiledRenderGraph compiled;
    try {
        while (!ready.empty()) {
            const std::uint32_t passIndex = ready.front();
            ready.erase(ready.begin());
            compiled.passOrder.push_back(RenderPassHandle{passIndex});
            for (const std::uint32_t dependent : dependents[passIndex]) {
                if (--indegree[dependent] == 0) {
                    ready.push_back(dependent);
                }
            }
        }
    } catch (...) {
        return std::unexpected(RenderGraphError::AllocationFailed);
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
            compiled.lifetimes.push_back(
                RenderResourceLifetime{RenderTextureHandle{resourceIndex}, *firstUse[resourceIndex], *lastUse[resourceIndex]});
        }
    }
    for (std::uint32_t resourceIndex = 0; resourceIndex < m_textures.size(); ++resourceIndex) {
        std::optional<RenderPassHandle> previousPass;
        bool previousWrite = false;
        for (const RenderPassHandle pass : compiled.passOrder) {
            const auto use = std::find_if(m_passes[pass.id].uses.begin(), m_passes[pass.id].uses.end(),
                                          [resourceIndex](const RenderResourceUse &value) {
                                              return value.resource.id == resourceIndex;
                                          });
            if (use == m_passes[pass.id].uses.end()) {
                continue;
            }
            if (previousPass.has_value() && (previousWrite || use->write)) {
                compiled.barriers.push_back(RenderBarrier{RenderTextureHandle{resourceIndex}, *previousPass, pass});
            }
            previousPass = pass;
            previousWrite = use->write;
        }
    }
    return compiled;
}

void RenderGraph::clear() noexcept {
    m_passes.clear();
    m_textures.clear();
}

} // namespace star::rhi
