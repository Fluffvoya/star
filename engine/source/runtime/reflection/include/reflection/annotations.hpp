#pragma once

#if defined(STAR_REFLECTION_PARSER)
#define STAR_CLASS(...) __attribute__((annotate("star:class:" #__VA_ARGS__)))
#define STAR_STRUCT(...) __attribute__((annotate("star:struct:" #__VA_ARGS__)))
#define STAR_ENUM(...) __attribute__((annotate("star:enum:" #__VA_ARGS__)))
#define STAR_PROPERTY(...) __attribute__((annotate("star:property:" #__VA_ARGS__)))
#define STAR_FUNCTION(...) __attribute__((annotate("star:function:" #__VA_ARGS__)))
#else
#define STAR_CLASS(...)
#define STAR_STRUCT(...)
#define STAR_ENUM(...)
#define STAR_PROPERTY(...)
#define STAR_FUNCTION(...)
#endif

#define STAR_GENERATED_BODY()
