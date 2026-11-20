#pragma once

#include "reflected_defaults.hpp"
#include "reflection/annotations.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace star::test {

STAR_ENUM()
enum class Visibility : signed char { Hidden = -1, Visible = 1 };

STAR_STRUCT()
struct Settings {
    STAR_PROPERTY()
    bool enabled = true;
};

STAR_CLASS(Version = 3, FormerName = "star::test::OldWidget")
class Widget : public Settings {
    STAR_GENERATED_BODY()

  public:
    [[nodiscard]] int count() const { return m_count; }
    [[nodiscard]] Visibility visibility() const { return m_visibility; }
    [[nodiscard]] const auto &history() const { return m_history; }

  private:
    STAR_PROPERTY(FormerName = "old_count", Range(0, 12))
    int m_count = STAR_TEST_VISIBILITY_VERSION;

    STAR_PROPERTY()
    Visibility m_visibility = Visibility::Visible;

    STAR_PROPERTY()
    Settings m_settings;

    STAR_PROPERTY()
    std::vector<Visibility> m_history;

    STAR_PROPERTY()
    std::array<int, 2> m_weights{1, 2};

    STAR_PROPERTY()
    std::unordered_map<std::string, int> m_named;

    STAR_FUNCTION(CallInEditor)
    void reset() { m_count = 0; }
};

} // namespace star::test

namespace star::serialization {
class Archive;
}

namespace star::test {

STAR_STRUCT(CustomSerialize)
struct CustomDocument {
    int value = 5;
    void serialize(serialization::Archive &archive);
};

} // namespace star::test
