/* config.h for embedded divsufsort */
#define HAVE_CONFIG_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDDEF_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1

/* strings.h doesn't exist on MSVC */
#ifdef _MSC_VER
#define HAVE_STRINGS_H 0
#define INLINE __inline
#else
#define HAVE_STRINGS_H 1
#define INLINE inline
#endif

#define PROJECT_VERSION_FULL "2.0.1-embedded"
