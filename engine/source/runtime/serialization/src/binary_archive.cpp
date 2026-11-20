#include "archive_value.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace star::serialization {

namespace {

constexpr std::array magic{std::byte{'S'}, std::byte{'T'}, std::byte{'A'}, std::byte{'R'}};
constexpr std::byte format_version{1};

enum class ValueTag : std::uint8_t { Null, Plain, String, Sequence, Mapping };

struct InvalidArchive {};

class Writer {
  public:
    void writeByte(std::byte value) { m_data.push_back(value); }

    void writeSize(std::size_t size) {
        if (size > std::numeric_limits<std::uint32_t>::max()) {
            throw InvalidArchive{};
        }
        for (unsigned shift = 0; shift < 32; shift += 8) {
            writeByte(static_cast<std::byte>((size >> shift) & 0xffu));
        }
    }

    void writeString(std::string_view text) {
        writeSize(text.size());
        const auto bytes = std::as_bytes(std::span(text));
        m_data.insert(m_data.end(), bytes.begin(), bytes.end());
    }

    void writeValue(const ArchiveValue &value) {
        if (const auto *plain = std::get_if<PlainScalar>(&value.value)) {
            writeByte(static_cast<std::byte>(ValueTag::Plain));
            writeString(plain->text);
        } else if (const auto *text = std::get_if<std::string>(&value.value)) {
            writeByte(static_cast<std::byte>(ValueTag::String));
            writeString(*text);
        } else if (const auto *sequence = std::get_if<ArchiveValue::Sequence>(&value.value)) {
            writeByte(static_cast<std::byte>(ValueTag::Sequence));
            writeSize(sequence->size());
            for (const auto &entry : *sequence) {
                writeValue(entry);
            }
        } else if (const auto *mapping = std::get_if<ArchiveValue::Mapping>(&value.value)) {
            writeByte(static_cast<std::byte>(ValueTag::Mapping));
            writeSize(mapping->size());
            for (const auto &[name, entry] : *mapping) {
                writeString(name);
                writeValue(entry);
            }
        } else {
            writeByte(static_cast<std::byte>(ValueTag::Null));
        }
    }

    std::vector<std::byte> finish() { return std::move(m_data); }

  private:
    std::vector<std::byte> m_data;
};

class Reader {
  public:
    explicit Reader(std::span<const std::byte> input) : m_input(input) {}

    [[nodiscard]] bool empty() const { return m_input.empty(); }

    std::span<const std::byte> take(std::size_t size) {
        if (size > m_input.size()) {
            throw InvalidArchive{};
        }
        const auto result = m_input.first(size);
        m_input = m_input.subspan(size);
        return result;
    }

    std::uint32_t readSize() {
        std::uint32_t size = 0;
        const auto bytes = take(4);
        for (unsigned index = 0; index < 4; ++index) {
            size |= std::to_integer<std::uint32_t>(bytes[index]) << (index * 8);
        }
        return size;
    }

    std::string readString() {
        const auto bytes = take(readSize());
        return bytes.empty() ? std::string{} : std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }

    ArchiveValue readValue(unsigned depth) {
        if (depth > max_archive_depth) {
            throw InvalidArchive{};
        }
        switch (static_cast<ValueTag>(take(1).front())) {
        case ValueTag::Null:
            return ArchiveValue{};
        case ValueTag::Plain: {
            auto text = readString();
            if (!isPlainScalar(text)) {
                throw InvalidArchive{};
            }
            return ArchiveValue{PlainScalar{std::move(text)}};
        }
        case ValueTag::String:
            return ArchiveValue{readString()};
        case ValueTag::Sequence: {
            const auto count = readSize();
            if (count > m_input.size()) {
                throw InvalidArchive{};
            }
            ArchiveValue::Sequence sequence;
            for (std::uint32_t index = 0; index < count; ++index) {
                sequence.push_back(readValue(depth + 1));
            }
            return ArchiveValue{std::move(sequence)};
        }
        case ValueTag::Mapping: {
            const auto count = readSize();
            if (count > m_input.size() / 5) {
                throw InvalidArchive{};
            }
            ArchiveValue::Mapping mapping;
            for (std::uint32_t index = 0; index < count; ++index) {
                auto name = readString();
                auto entry = readValue(depth + 1);
                if (!mapping.emplace(std::move(name), std::move(entry)).second) {
                    throw InvalidArchive{};
                }
            }
            return ArchiveValue{std::move(mapping)};
        }
        }
        throw InvalidArchive{};
    }

  private:
    std::span<const std::byte> m_input;
};

} // namespace

std::expected<ArchiveDocument, ArchiveError> readBinary(std::span<const std::byte> source) {
    try {
        Reader reader(source);
        if (!std::ranges::equal(reader.take(magic.size()), magic)) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
        }
        if (reader.take(1).front() != format_version) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::UnsupportedVersion, "/"});
        }
        auto root = reader.readValue(0);
        if (!reader.empty()) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
        }
        return parseDocument(std::move(root));
    } catch (const InvalidArchive &) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    } catch (...) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, {}});
    }
}

std::expected<std::vector<std::byte>, ArchiveError> writeBinary(const ArchiveDocument &document) {
    try {
        const auto root = documentValue(document);
        if (document.type_name.empty() || document.version == 0 || !validateValue(root)) {
            return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
        }
        Writer writer;
        for (const auto byte : magic) {
            writer.writeByte(byte);
        }
        writer.writeByte(format_version);
        writer.writeValue(root);
        return writer.finish();
    } catch (const InvalidArchive &) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::InvalidInput, "/"});
    } catch (...) {
        return std::unexpected(ArchiveError{ArchiveErrorCode::OperationFailed, {}});
    }
}

} // namespace star::serialization
