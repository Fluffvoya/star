#if defined(_WIN32)
#define STAR_TEST_MODULE_EXPORT __declspec(dllexport)
#else
#define STAR_TEST_MODULE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" STAR_TEST_MODULE_EXPORT int starTestNextValue() {
    static int value = 0;
    return ++value;
}
