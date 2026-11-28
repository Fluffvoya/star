#pragma once

#include "scene/game_module.hpp"

namespace game_fixture {

inline constexpr star::reflect::ModuleId module_id = 793;
inline constexpr std::string_view object_type = "game_fixture::Actor";

struct Score {
    int value = 0;
    int bonus = 7;
};

struct AuxiliaryScore {
    int value = 0;
};

} // namespace game_fixture

template <> struct star::ComponentTraits<game_fixture::Score> {
    static constexpr auto name = "game_fixture::Score";
};

template <> struct star::ComponentTraits<game_fixture::AuxiliaryScore> {
    static constexpr auto name = "game_fixture::AuxiliaryScore";
};
