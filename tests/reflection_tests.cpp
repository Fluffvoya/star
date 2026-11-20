#include "reflection/reflection.hpp"

#include "test_support.hpp"

#include <array>
#include <string>

namespace {

struct Base {
    virtual ~Base() = default;
    int base_value = 0;
    friend bool operator==(const Base &, const Base &) = default;
};

struct Player final : Base {
    float speed = 3.0f;
    std::string name = "Player";
    friend bool operator==(const Player &, const Player &) = default;
};

constexpr star::reflect::PropertyInfo base_properties[] = {
    star::reflect::makeProperty<Base, int, &Base::base_value>("base_value"),
};
constexpr star::reflect::TypeInfo base_type{star::reflect::makeTypeId("tests::Base"),
                                            "tests::Base",
                                            star::reflect::TypeKind::Class,
                                            sizeof(Base),
                                            alignof(Base),
                                            base_properties,
                                            star::reflect::makeTypeOps<Base>(),
                                            42,
                                            1,
                                            nullptr};

constexpr star::reflect::PropertyInfo player_properties[] = {
    star::reflect::makeProperty<Player, float, &Player::speed>("speed"),
    star::reflect::makeProperty<Player, std::string, &Player::name>("name"),
};
constexpr star::reflect::TypeInfo player_type{star::reflect::makeTypeId("tests::Player"),
                                              "tests::Player",
                                              star::reflect::TypeKind::Class,
                                              sizeof(Player),
                                              alignof(Player),
                                              player_properties,
                                              star::reflect::makeTypeOps<Player>(),
                                              42,
                                              1,
                                              &base_type};

} // namespace

STAR_TEST(reflectionRegistryRegistersAndRemovesModuleTypes) {
    star::reflect::TypeRegistry registry;
    const std::array types{&base_type, &player_type};

    STAR_CHECK(registry.registerModule(42, types).has_value());
    STAR_CHECK(registry.find("tests::Base") == &base_type);
    STAR_CHECK(registry.find(star::reflect::makeTypeId("tests::Player")) == &player_type);

    const auto derived = registry.derivedFrom(base_type);
    STAR_CHECK(derived.size() == 1);
    STAR_CHECK(derived.front() == &player_type);

    registry.unregisterModule(42);
    STAR_CHECK(registry.find("tests::Base") == nullptr);
    STAR_CHECK(registry.find("tests::Player") == nullptr);
}

STAR_TEST(reflectionPropertiesExposeTypedMemberAccess) {
    Player player;
    auto *speed = static_cast<float *>(player_type.properties[0].get_mutable(&player));
    auto *name = static_cast<std::string *>(player_type.properties[1].get_mutable(&player));
    *speed = 7.5f;
    *name = "Runner";

    STAR_CHECK(player.speed == 7.5f);
    STAR_CHECK(player.name == "Runner");
    STAR_CHECK(player_type.ops.equals(&player, &player));
}
