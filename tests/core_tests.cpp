#include "test_support.hpp"

#include "core/core.hpp"

#include <string>

namespace {

STAR_TEST(guidGenerateProducesValidValue) {
    const star::Guid guid = star::Guid::generate();

    STAR_CHECK(guid.isValid());
    STAR_CHECK(guid.toString().size() == 32);
    STAR_CHECK((guid.getBytes()[6] & 0xf0U) == 0x40U);
    STAR_CHECK((guid.getBytes()[8] & 0xc0U) == 0x80U);
}

STAR_TEST(guidParseRoundTripsText) {
    const star::Guid original = star::Guid::generate();
    const auto parsed = star::Guid::parse(original.toString());

    STAR_CHECK(parsed.has_value());
    STAR_CHECK(*parsed == original);
}

STAR_TEST(guidParseRejectsInvalidInput) {
    const auto short_value = star::Guid::parse("0123");
    const auto invalid_value = star::Guid::parse("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz");

    STAR_CHECK(!short_value.has_value());
    STAR_CHECK(short_value.error() == star::GuidParseError::InvalidLength);
    STAR_CHECK(!invalid_value.has_value());
    STAR_CHECK(invalid_value.error() == star::GuidParseError::InvalidCharacter);
}

STAR_TEST(nameInternsAndComparesById) {
    const star::Name first("PlayerCharacter");
    const star::Name second("PlayerCharacter");
    const star::Name different("Camera");

    STAR_CHECK(first == second);
    STAR_CHECK(first != different);
    STAR_CHECK(first.getId() == second.getId());
    STAR_CHECK(first.getString() == "PlayerCharacter");
    STAR_CHECK(star::Name{}.isEmpty());
}

} // namespace
