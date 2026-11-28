#include "game_module_fixture.hpp"

#if defined(ENTT_VERSION)
#error The independent game module must not receive the Scene private EnTT include path.
#endif

#include <array>

namespace {

class Actor final : public star::GameObject {
  public:
    Actor() { setTickEnabled(true); }

  private:
    void onCreate() noexcept override {
        if (auto *score = tryGetComponent<game_fixture::Score>()) {
            ++score->value;
        }
    }

    void onTick(float) noexcept override {
        if (auto *score = tryGetComponent<game_fixture::Score>()) {
            score->value += 10;
        }
    }
};

class ScoreSystem final : public star::System {
  public:
    void execute(star::SystemContext &context) noexcept override {
        static_cast<void>(context.each<game_fixture::Score>(
            [](star::ObjectHandle, game_fixture::Score &score) noexcept { score.value += 100; }));
    }
};

bool startup(star::GameModuleContext &context) noexcept {
    try {
        star::SystemDescriptor descriptor;
        descriptor.id = star::reflect::makeTypeId("game_fixture::ScoreSystem");
        descriptor.phase = star::TickPhase::LateUpdate;
        descriptor.writes = {star::componentTypeId<game_fixture::Score>()};
        descriptor.parallel = true;
        const bool registered =
            context.registerSystem(std::move(descriptor), std::make_unique<ScoreSystem>()).has_value();
#if defined(STAR_FIXTURE_FAIL_STARTUP)
        static_cast<void>(registered);
        return false;
#else
        return registered;
#endif
    } catch (...) {
        return false;
    }
}

} // namespace

#if defined(_WIN32)
#define STAR_GAME_FIXTURE_EXPORT __declspec(dllexport)
#else
#define STAR_GAME_FIXTURE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" STAR_GAME_FIXTURE_EXPORT const star::GameModuleDescriptor *StarGetModuleDescriptor() noexcept {
    using namespace game_fixture;
    using namespace star;
#if defined(STAR_FIXTURE_RELOAD_VERSION)
    static const std::array properties{
        reflect::makeProperty<Score, int, &Score::value>("points", reflect::PropertyFlags::None, nullptr, "value"),
        reflect::makeProperty<Score, int, &Score::bonus>("bonus"),
    };
    constexpr std::uint32_t version = 2;
#else
    static const std::array properties{reflect::makeProperty<Score, int, &Score::value>("value")};
    constexpr std::uint32_t version = 1;
#endif
    static const reflect::TypeInfo score{componentTypeId<Score>(),
                                         ComponentTraits<Score>::name,
                                         reflect::TypeKind::Struct,
                                         sizeof(Score),
                                         alignof(Score),
                                         properties,
                                         reflect::makeTypeOps<Score>(),
                                         module_id,
                                         version,
                                         nullptr};
    static const std::array components{&score};
    static const std::array objects{makeGameObjectType<Actor>(object_type)};
    static const GameModuleDescriptor descriptor{
        game_module_abi_version, module_id, "M3 Fixture", components, objects, startup};
    return &descriptor;
}
