#pragma once

namespace star::reflect {
template <typename T> struct GeneratedType;
}

#if defined(STAR_REFLECTION_PARSER)
// A declaration in the parsed AST keeps each marker with its lexical scope.
#define STAR_CLASS(...) static_assert(true);
#define STAR_STRUCT(...) static_assert(true);
#define STAR_ENUM(...) static_assert(true);
#define STAR_PROPERTY(...) __attribute__((annotate("star:property:" #__VA_ARGS__)))
#define STAR_FUNCTION(...) __attribute__((annotate("star:function:" #__VA_ARGS__)))
#else
#define STAR_CLASS(...)
#define STAR_STRUCT(...)
#define STAR_ENUM(...)
#define STAR_PROPERTY(...)
#define STAR_FUNCTION(...)
#endif

#define STAR_GENERATED_BODY() template <typename> friend struct ::star::reflect::GeneratedType;
