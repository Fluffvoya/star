#pragma once

#include "core/export.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace star {

class STAR_CORE_API DelegateHandle final {
  public:
    DelegateHandle() noexcept = default;
    [[nodiscard]] bool isValid() const noexcept { return m_id != 0; }
    friend bool operator==(const DelegateHandle &, const DelegateHandle &) = default;

  private:
    template <class> friend class MulticastDelegate;

    explicit DelegateHandle(std::uint64_t id) noexcept : m_id(id) {}
    std::uint64_t m_id = 0;
};

template <class Signature> class Delegate;

template <class Return, class... Args> class Delegate<Return(Args...)> final {
    using Function = std::function<Return(Args...)>;

    struct State final {
        mutable std::mutex mutex;
        Function function;
    };

  public:
    Delegate() : m_state(std::make_shared<State>()) {}

    template <class Callable>
        requires std::is_invocable_r_v<Return, std::decay_t<Callable> &, Args...>
    explicit Delegate(Callable &&callable) : Delegate() {
        bind(std::forward<Callable>(callable));
    }

    template <class Callable>
        requires std::is_invocable_r_v<Return, std::decay_t<Callable> &, Args...>
    void bind(Callable &&callable) {
        Function function(std::forward<Callable>(callable));
        const std::lock_guard lock(m_state->mutex);
        m_state->function = std::move(function);
    }

    void unbind() noexcept {
        const std::lock_guard lock(m_state->mutex);
        m_state->function = nullptr;
    }

    [[nodiscard]] bool isBound() const noexcept {
        const std::lock_guard lock(m_state->mutex);
        return static_cast<bool>(m_state->function);
    }

    Return operator()(Args... args) const {
        Function function;
        {
            const std::lock_guard lock(m_state->mutex);
            function = m_state->function;
        }
        if constexpr (std::is_void_v<Return>) {
            if (function) {
                function(std::forward<Args>(args)...);
            }
        } else {
            if (function) {
                return function(std::forward<Args>(args)...);
            }
            return Return{};
        }
    }

    template <class Object, class Member>
        requires std::is_invocable_r_v<Return, Member, Object &, Args...>
    void bindMember(const std::shared_ptr<Object> &object, Member member) {
        std::weak_ptr<Object> weak_object = object;
        bind([weak_object, member](Args... args) -> Return {
            const auto locked = weak_object.lock();
            if (!locked) {
                if constexpr (!std::is_void_v<Return>) {
                    return Return{};
                } else {
                    return;
                }
            }
            if constexpr (std::is_void_v<Return>) {
                std::invoke(member, *locked, std::forward<Args>(args)...);
            } else {
                return std::invoke(member, *locked, std::forward<Args>(args)...);
            }
        });
    }

  private:
    std::shared_ptr<State> m_state;
};

template <class Signature> class MulticastDelegate;

template <class... Args> class MulticastDelegate<void(Args...)> final {
    using Function = std::function<void(Args...)>;

    struct State final {
        mutable std::mutex mutex;
        std::unordered_map<std::uint64_t, Function> functions;
        std::atomic<std::uint64_t> next_id{1};
    };

  public:
    MulticastDelegate() : m_state(std::make_shared<State>()) {}

    template <class Callable>
        requires std::is_invocable_r_v<void, std::decay_t<Callable> &, Args...>
    [[nodiscard]] DelegateHandle add(Callable &&callable) {
        Function function(std::forward<Callable>(callable));
        const std::uint64_t id = m_state->next_id.fetch_add(1, std::memory_order_relaxed);
        if (id == 0) {
            return {};
        }
        const std::lock_guard lock(m_state->mutex);
        try {
            m_state->functions.emplace(id, std::move(function));
        } catch (...) {
            return {};
        }
        return DelegateHandle(id);
    }

    bool remove(DelegateHandle handle) noexcept {
        if (!handle.isValid()) {
            return false;
        }
        const std::lock_guard lock(m_state->mutex);
        return m_state->functions.erase(handle.m_id) != 0;
    }

    void clear() noexcept {
        const std::lock_guard lock(m_state->mutex);
        m_state->functions.clear();
    }

    [[nodiscard]] std::size_t getSize() const noexcept {
        const std::lock_guard lock(m_state->mutex);
        return m_state->functions.size();
    }

    void broadcast(Args... args) const {
        std::vector<Function> functions;
        {
            const std::lock_guard lock(m_state->mutex);
            functions.reserve(m_state->functions.size());
            for (const auto &[id, function] : m_state->functions) {
                static_cast<void>(id);
                functions.push_back(function);
            }
        }
        for (const Function &function : functions) {
            function(args...);
        }
    }

  private:
    std::shared_ptr<State> m_state;
};

} // namespace star
