#pragma once

#include <clang-c/Index.h>

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace star::header_tool {

struct Member {
    std::string name;
    std::string metadata;
    bool function = false;
    std::string nested_type;
    std::string spelled_type;
    std::string location;
};

struct Declaration {
    std::string name;
    std::string kind;
    std::string metadata;
    std::string base;
    std::vector<Member> members;
    std::vector<std::string> enumerators;
};

bool collectDeclarations(CXTranslationUnit unit, const std::filesystem::path &path,
                         std::vector<Declaration> &declarations);
bool validateProperties(const std::vector<Declaration> &declarations);
std::string generateAnnotationHeader(std::vector<Declaration> declarations);
std::string generateHeader(const std::string &module);
std::string generateSource(const std::string &module, std::uint64_t owner, const std::filesystem::path &header,
                           const std::vector<std::filesystem::path> &inputs, std::vector<Declaration> declarations);
std::string generateDepfile(const std::filesystem::path &output, const std::set<std::filesystem::path> &dependencies);

} // namespace star::header_tool
