#include "TestSupport.hpp"

#include "Core/Core.hpp"

#include <string>

namespace {

STAR_TEST(GuidGenerateProducesValidValue) {
    const star::Guid guid = star::Guid::Generate();

    STAR_CHECK(guid.IsValid());
    STAR_CHECK(guid.ToString().size() == 32);
    STAR_CHECK((guid.GetBytes()[6] & 0xf0U) == 0x40U);
    STAR_CHECK((guid.GetBytes()[8] & 0xc0U) == 0x80U);
}

STAR_TEST(GuidParseRoundTripsText) {
    const star::Guid original = star::Guid::Generate();
    const auto parsed = star::Guid::Parse(original.ToString());

    STAR_CHECK(parsed.has_value());
    STAR_CHECK(*parsed == original);
}

STAR_TEST(GuidParseRejectsInvalidInput) {
    const auto shortValue = star::Guid::Parse("0123");
    const auto invalidValue = star::Guid::Parse("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");

    STAR_CHECK(!shortValue.has_value());
    STAR_CHECK(shortValue.error() == star::GuidParseError::InvalidLength);
    STAR_CHECK(!invalidValue.has_value());
    STAR_CHECK(invalidValue.error() == star::GuidParseError::InvalidCharacter);
}

STAR_TEST(NameInternsAndComparesById) {
    const star::Name first("PlayerCharacter");
    const star::Name second("PlayerCharacter");
    const star::Name different("Camera");

    STAR_CHECK(first == second);
    STAR_CHECK(first != different);
    STAR_CHECK(first.GetId() == second.GetId());
    STAR_CHECK(first.GetString() == "PlayerCharacter");
    STAR_CHECK(star::Name{}.IsEmpty());
}

} // namespace
