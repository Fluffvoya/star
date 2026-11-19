#include "test_support.hpp"

#include "core/cvar.hpp"

#include <array>
#include <atomic>
#include <limits>
#include <string>
#include <thread>
#include <variant>

namespace {

const star::CVar<int> static_variable{"test.Static", 2, "Static variable", star::CVarFlags::Scalability};

STAR_TEST(cvarSharesValuesAcrossTypedAndNamedAccess) {
    STAR_CHECK(static_variable.get() == 2);
    star::CVar<int> variable{"test.Quality", 2, "Quality level", star::CVarFlags::Scalability};
    STAR_CHECK(variable.get() == 2);
    STAR_CHECK(star::CVarRegistry::setFromString("test.Quality", "3"));
    STAR_CHECK(variable.get() == 3);
    STAR_CHECK(variable.set(1));

    const auto info = star::CVarRegistry::getInfo("test.Quality");
    STAR_CHECK(info && info->description == "Quality level" && info->flags == star::CVarFlags::Scalability);
    STAR_CHECK(std::get<int>(info->default_value) == 2);
    STAR_CHECK(std::get<int>(info->value) == 1);

    const auto wrong_case = star::CVarRegistry::getInfo("test.quality");
    STAR_CHECK(!wrong_case && wrong_case.error() == star::CVarError::NotFound);
}

STAR_TEST(cvarOwnsMetadataAndUnregistersWithItsHandle) {
    {
        std::string name = "test.Reloaded";
        std::string description = "Temporary description";
        star::CVar<int> variable{name, 2, description};
        name.assign("changed");
        description.assign("changed");

        {
            const star::CVar<bool> duplicate{"test.Reloaded", false, "Duplicate"};
            const auto error = duplicate.get();
            STAR_CHECK(!error && error.error() == star::CVarError::DuplicateName);
        }
        const auto original = star::CVarRegistry::getInfo("test.Reloaded");
        STAR_CHECK(original && original->description == "Temporary description");
        STAR_CHECK(variable.set(4));
    }

    const auto expired = star::CVarRegistry::setFromString("test.Reloaded", "3");
    STAR_CHECK(!expired && expired.error() == star::CVarError::NotFound);
    const star::CVar<bool> replacement{"test.Reloaded", true, "Replacement"};
    STAR_CHECK(replacement.get() == true);

    const star::CVar<int> invalid{"test..Invalid", 0, "Invalid"};
    const auto error = invalid.get();
    STAR_CHECK(!error && error.error() == star::CVarError::InvalidName);
}

STAR_TEST(cvarParsesTextAndPreservesValuesOnInvalidInput) {
    star::CVar<bool> boolean{"test.Boolean", false, "Boolean"};
    star::CVar<int> integer{"test.Integer", 0, "Integer"};
    star::CVar<float> number{"test.Float", 0.0f, "Float"};
    star::CVar<std::string> string{"test.String", "original", "String"};

    STAR_CHECK(star::CVarRegistry::setFromString("test.Boolean", "true"));
    STAR_CHECK(star::CVarRegistry::setFromString("test.Integer", "-8"));
    STAR_CHECK(star::CVarRegistry::setFromString("test.Float", "1.25e1"));
    STAR_CHECK(star::CVarRegistry::setFromString("test.String", "  preserved text  "));
    STAR_CHECK(boolean.get() == true);
    STAR_CHECK(integer.get() == -8);
    STAR_CHECK(number.get() == 12.5f);
    STAR_CHECK(string.get() == "  preserved text  ");

    const auto invalid_bool = star::CVarRegistry::setFromString("test.Boolean", "yes");
    const auto invalid_int = star::CVarRegistry::setFromString("test.Integer", "8x");
    const auto overflow = star::CVarRegistry::setFromString("test.Integer", "999999999999999999999999");
    const auto invalid_float = star::CVarRegistry::setFromString("test.Float", "1,25");
    const auto trailing_space = star::CVarRegistry::setFromString("test.Float", "1.25 ");
    const auto infinite = number.set(std::numeric_limits<float>::infinity());
    for (const auto &result : {invalid_bool, invalid_int, overflow, invalid_float, trailing_space, infinite}) {
        STAR_CHECK(!result && result.error() == star::CVarError::InvalidValue);
    }
    STAR_CHECK(boolean.get() == true);
    STAR_CHECK(integer.get() == -8);
    STAR_CHECK(number.get() == 12.5f);
}

STAR_TEST(cvarSupportsConcurrentTypedReadsAndNamedWrites) {
    const star::CVar<std::string> text{"test.Concurrent", "initial", "Concurrent"};
    std::atomic_bool succeeded = true;
    {
        std::array<std::jthread, 4> threads;
        for (auto &thread : threads) {
            thread = std::jthread([&] {
                for (int iteration = 0; iteration < 100; ++iteration) {
                    const auto set = star::CVarRegistry::setFromString("test.Concurrent", "updated");
                    if (!set || text.get() != "updated") {
                        succeeded.store(false, std::memory_order_relaxed);
                        return;
                    }
                }
            });
        }
    }
    STAR_CHECK(succeeded.load(std::memory_order_relaxed));
}

} // namespace
