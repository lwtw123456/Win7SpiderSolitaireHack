#pragma once

#include <safetyhook.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace safetyhook_manager {

enum class HookKind {
    Inline,
    Mid,
};

enum class ErrorCode {
    InvalidName,
    NullTarget,
    NullDestination,
    DuplicateName,
    TargetAlreadyHooked,
    NotFound,
    CreateFailed,
    StateChangeFailed,
};

struct Error {
    ErrorCode code{};
    HookKind kind{};
    std::string name{};
    std::string message{};
};

using Result = std::expected<void, Error>;

class HookManager final {
public:
    HookManager() = default;
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;
    HookManager(HookManager&&) = delete;
    HookManager& operator=(HookManager&&) = delete;

    ~HookManager() {
        uninstall_all();
    }

    template <typename Target, typename Destination>
    [[nodiscard]] Result install_inline(
        std::string name,
        Target target,
        Destination destination,
        bool start_enabled = true) {
        return install_inline_address(
            std::move(name),
            address_cast(target),
            address_cast(destination),
            start_enabled);
    }

    template <typename Target, typename Callback>
    [[nodiscard]] Result install_mid(
        std::string name,
        Target target,
        Callback&& callback,
        bool start_enabled = true) {
        static_assert(
            std::is_convertible_v<std::decay_t<Callback>, safetyhook::MidHookFn>,
            "Mid-hook callback must be a captureless function/lambda with signature "
            "void(SafetyHookContext&).");

        safetyhook::MidHookFn destination = std::forward<Callback>(callback);
        return install_mid_address(
            std::move(name),
            address_cast(target),
            destination,
            start_enabled);
    }

    [[nodiscard]] bool uninstall(std::string_view name) {
        if (auto it = inline_hooks_.find(name); it != inline_hooks_.end()) {
            it->second->hook.reset();
            inline_hooks_.erase(it);
            erase_order(name);
            return true;
        }

        if (auto it = mid_hooks_.find(name); it != mid_hooks_.end()) {
            it->second->hook.reset();
            mid_hooks_.erase(it);
            erase_order(name);
            return true;
        }

        return false;
    }

    void uninstall_all() {
        while (!installation_order_.empty()) {
            std::string name = std::move(installation_order_.back());
            installation_order_.pop_back();

            if (auto it = inline_hooks_.find(name); it != inline_hooks_.end()) {
                it->second->hook.reset();
                inline_hooks_.erase(it);
                continue;
            }

            if (auto it = mid_hooks_.find(name); it != mid_hooks_.end()) {
                it->second->hook.reset();
                mid_hooks_.erase(it);
            }
        }

        for (auto& [_, entry] : inline_hooks_) {
            entry->hook.reset();
        }
        inline_hooks_.clear();

        for (auto& [_, entry] : mid_hooks_) {
            entry->hook.reset();
        }
        mid_hooks_.clear();
    }

    [[nodiscard]] bool remove(std::string_view name) {
        return uninstall(name);
    }

    void remove_all() {
        uninstall_all();
    }

    [[nodiscard]] Result enable(std::string_view name) {
        return set_enabled(name, true);
    }

    [[nodiscard]] Result disable(std::string_view name) {
        return set_enabled(name, false);
    }

    [[nodiscard]] Result set_enabled(std::string_view name, bool enabled) {
        if (auto it = inline_hooks_.find(name); it != inline_hooks_.end()) {
            if (it->second->hook.enabled() == enabled) {
                return {};
            }

            auto result = enabled ? it->second->hook.enable() : it->second->hook.disable();
            if (!result) {
                return std::unexpected(Error{
                    ErrorCode::StateChangeFailed,
                    HookKind::Inline,
                    std::string{name},
                    std::string{enabled ? "enable failed: " : "disable failed: "} +
                        inline_error_name(result.error()),
                });
            }
            return {};
        }

        if (auto it = mid_hooks_.find(name); it != mid_hooks_.end()) {
            if (it->second->hook.enabled() == enabled) {
                return {};
            }

            auto result = enabled ? it->second->hook.enable() : it->second->hook.disable();
            if (!result) {
                return std::unexpected(Error{
                    ErrorCode::StateChangeFailed,
                    HookKind::Mid,
                    std::string{name},
                    std::string{enabled ? "enable failed: " : "disable failed: "} +
                        mid_error_name(result.error()),
                });
            }
            return {};
        }

        return std::unexpected(Error{
            ErrorCode::NotFound,
            HookKind::Inline,
            std::string{name},
            "hook was not found",
        });
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        return inline_hooks_.contains(name) || mid_hooks_.contains(name);
    }

    [[nodiscard]] std::optional<HookKind> kind(std::string_view name) const {
        if (inline_hooks_.contains(name)) {
            return HookKind::Inline;
        }
        if (mid_hooks_.contains(name)) {
            return HookKind::Mid;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<bool> enabled(std::string_view name) const {
        if (auto it = inline_hooks_.find(name); it != inline_hooks_.end()) {
            return it->second->hook.enabled();
        }
        if (auto it = mid_hooks_.find(name); it != mid_hooks_.end()) {
            return it->second->hook.enabled();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return inline_hooks_.size() + mid_hooks_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return inline_hooks_.empty() && mid_hooks_.empty();
    }

    [[nodiscard]] SafetyHookInline* inline_hook(std::string_view name) {
        auto it = inline_hooks_.find(name);
        return it == inline_hooks_.end() ? nullptr : &it->second->hook;
    }

    [[nodiscard]] const SafetyHookInline* inline_hook(std::string_view name) const {
        auto it = inline_hooks_.find(name);
        return it == inline_hooks_.end() ? nullptr : &it->second->hook;
    }

    [[nodiscard]] SafetyHookMid* mid_hook(std::string_view name) {
        auto it = mid_hooks_.find(name);
        return it == mid_hooks_.end() ? nullptr : &it->second->hook;
    }

    [[nodiscard]] const SafetyHookMid* mid_hook(std::string_view name) const {
        auto it = mid_hooks_.find(name);
        return it == mid_hooks_.end() ? nullptr : &it->second->hook;
    }

    template <typename Fn>
    [[nodiscard]] Fn original(std::string_view name) const {
        static_assert(std::is_pointer_v<Fn>, "Fn must be a function-pointer type.");

        auto it = inline_hooks_.find(name);
        if (it == inline_hooks_.end()) {
            return Fn{};
        }
        return it->second->hook.template original<Fn>();
    }

    template <typename Fn, typename... Args>
    decltype(auto) invoke_original(std::string_view name, Args&&... args) const {
        auto function = original<Fn>(name);
        if (!function) {
            throw std::runtime_error(
                "SafetyHookManager: inline hook not found or trampoline is invalid: " +
                std::string{name});
        }
        return std::invoke(function, std::forward<Args>(args)...);
    }

private:
    struct TransparentHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }

        [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept {
            return operator()(std::string_view{value});
        }
    };

    struct TransparentEqual {
        using is_transparent = void;

        [[nodiscard]] bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
            return lhs == rhs;
        }
    };

    struct InlineEntry {
        std::uintptr_t target{};
        SafetyHookInline hook{};

        InlineEntry(std::uintptr_t target_address, SafetyHookInline&& value)
            : target(target_address), hook(std::move(value)) {}
    };

    struct MidEntry {
        std::uintptr_t target{};
        SafetyHookMid hook{};

        MidEntry(std::uintptr_t target_address, SafetyHookMid&& value)
            : target(target_address), hook(std::move(value)) {}
    };

    using InlineMap = std::unordered_map<
        std::string,
        std::unique_ptr<InlineEntry>,
        TransparentHash,
        TransparentEqual>;

    using MidMap = std::unordered_map<
        std::string,
        std::unique_ptr<MidEntry>,
        TransparentHash,
        TransparentEqual>;

    template <typename>
    static constexpr bool always_false = false;

    template <typename T>
    [[nodiscard]] static void* address_cast(T value) noexcept {
        using U = std::remove_cvref_t<T>;

        if constexpr (std::is_same_v<U, std::nullptr_t>) {
            return nullptr;
        } else if constexpr (std::is_integral_v<U> || std::is_enum_v<U>) {
            return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
        } else if constexpr (std::is_pointer_v<U>) {
            if constexpr (std::is_const_v<std::remove_pointer_t<U>>) {
                return const_cast<void*>(reinterpret_cast<const void*>(value));
            } else {
                return reinterpret_cast<void*>(value);
            }
        } else {
            static_assert(always_false<U>, "Address must be a pointer or an integer address.");
        }
    }

    [[nodiscard]] Result install_inline_address(
        std::string name,
        void* target,
        void* destination,
        bool start_enabled) {
        if (auto validation = validate_install(name, target, destination, HookKind::Inline); !validation) {
            return validation;
        }

        const auto flags = start_enabled
            ? SafetyHookInline::Default
            : SafetyHookInline::StartDisabled;

        auto created = SafetyHookInline::create(target, destination, flags);
        if (!created) {
            return std::unexpected(Error{
                ErrorCode::CreateFailed,
                HookKind::Inline,
                std::move(name),
                "inline hook creation failed: " + inline_error_name(created.error()),
            });
        }

        const auto target_address = reinterpret_cast<std::uintptr_t>(target);
        auto entry = std::make_unique<InlineEntry>(target_address, std::move(*created));
        installation_order_.push_back(name);
        inline_hooks_.emplace(std::move(name), std::move(entry));
        return {};
    }

    [[nodiscard]] Result install_mid_address(
        std::string name,
        void* target,
        safetyhook::MidHookFn destination,
        bool start_enabled) {
        if (auto validation = validate_install(
                name,
                target,
                reinterpret_cast<void*>(destination),
                HookKind::Mid);
            !validation) {
            return validation;
        }

        const auto flags = start_enabled
            ? SafetyHookMid::Default
            : SafetyHookMid::StartDisabled;

        auto created = SafetyHookMid::create(target, destination, flags);
        if (!created) {
            return std::unexpected(Error{
                ErrorCode::CreateFailed,
                HookKind::Mid,
                std::move(name),
                "mid hook creation failed: " + mid_error_name(created.error()),
            });
        }

        const auto target_address = reinterpret_cast<std::uintptr_t>(target);
        auto entry = std::make_unique<MidEntry>(target_address, std::move(*created));
        installation_order_.push_back(name);
        mid_hooks_.emplace(std::move(name), std::move(entry));
        return {};
    }

    [[nodiscard]] Result validate_install(
        std::string_view name,
        const void* target,
        const void* destination,
        HookKind kind_value) const {
        if (name.empty()) {
            return std::unexpected(Error{
                ErrorCode::InvalidName,
                kind_value,
                {},
                "hook name must not be empty",
            });
        }

        if (target == nullptr) {
            return std::unexpected(Error{
                ErrorCode::NullTarget,
                kind_value,
                std::string{name},
                "target address is null",
            });
        }

        if (destination == nullptr) {
            return std::unexpected(Error{
                ErrorCode::NullDestination,
                kind_value,
                std::string{name},
                "destination address is null",
            });
        }

        if (contains(name)) {
            return std::unexpected(Error{
                ErrorCode::DuplicateName,
                kind_value,
                std::string{name},
                "another hook already uses this name",
            });
        }

        const auto target_address = reinterpret_cast<std::uintptr_t>(target);
        if (target_in_use(target_address)) {
            return std::unexpected(Error{
                ErrorCode::TargetAlreadyHooked,
                kind_value,
                std::string{name},
                "another managed hook already uses this target address",
            });
        }

        return {};
    }

    [[nodiscard]] bool target_in_use(std::uintptr_t address) const noexcept {
        for (const auto& [_, entry] : inline_hooks_) {
            if (entry->target == address) {
                return true;
            }
        }

        for (const auto& [_, entry] : mid_hooks_) {
            if (entry->target == address) {
                return true;
            }
        }

        return false;
    }

    void erase_order(std::string_view name) {
        const auto it = std::find(installation_order_.begin(), installation_order_.end(), name);
        if (it != installation_order_.end()) {
            installation_order_.erase(it);
        }
    }

    [[nodiscard]] static std::string inline_error_name(const SafetyHookInline::Error& error) {
        switch (error.type) {
        case SafetyHookInline::Error::BAD_ALLOCATION:
            return "BAD_ALLOCATION";
        case SafetyHookInline::Error::FAILED_TO_DECODE_INSTRUCTION:
            return "FAILED_TO_DECODE_INSTRUCTION";
        case SafetyHookInline::Error::SHORT_JUMP_IN_TRAMPOLINE:
            return "SHORT_JUMP_IN_TRAMPOLINE";
        case SafetyHookInline::Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE:
            return "IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE";
        case SafetyHookInline::Error::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE:
            return "UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE";
        case SafetyHookInline::Error::FAILED_TO_UNPROTECT:
            return "FAILED_TO_UNPROTECT";
        case SafetyHookInline::Error::NOT_ENOUGH_SPACE:
            return "NOT_ENOUGH_SPACE";
        default:
            return "UNKNOWN_INLINE_ERROR";
        }
    }

    [[nodiscard]] static std::string mid_error_name(const SafetyHookMid::Error& error) {
        switch (error.type) {
        case SafetyHookMid::Error::BAD_ALLOCATION:
            return "BAD_ALLOCATION";
        case SafetyHookMid::Error::BAD_INLINE_HOOK:
            return "BAD_INLINE_HOOK/" + inline_error_name(error.inline_hook_error);
        default:
            return "UNKNOWN_MID_ERROR";
        }
    }

    InlineMap inline_hooks_{};
    MidMap mid_hooks_{};
    std::vector<std::string> installation_order_{};
};

[[nodiscard]] inline HookManager& global() noexcept {
    static HookManager instance{};
    return instance;
}

} // namespace safetyhook_manager