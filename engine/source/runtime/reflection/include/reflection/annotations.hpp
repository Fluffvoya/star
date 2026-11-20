#pragma once

namespace star::reflect {
template <typename T> struct GeneratedType;
}

#define STAR_CLASS(...)
#define STAR_STRUCT(...)
#define STAR_ENUM(...)

#if defined(STAR_REFLECTION_PARSER)
#define STAR_PROPERTY(...) __attribute__((annotate("star:property:" #__VA_ARGS__)))
#define STAR_FUNCTION(...) __attribute__((annotate("star:function:" #__VA_ARGS__)))
#else
#define STAR_PROPERTY(...)
#define STAR_FUNCTION(...)
#endif

#define STAR_GENERATED_BODY() template <typename> friend struct ::star::reflect::GeneratedType;
