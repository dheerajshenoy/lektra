// What LeakSanitizer leaves out of its report (only built with WITH_ASAN).
//
// The leak checker is part of the sanitizer runtime that is linked into the
// executable, so it also sees allocations made by code that is not instrumented:
// the bundled third-party libraries. Those are not Lektra's to fix, and they
// bury the leaks of Lektra's own code (MicroTeX alone allocates its macro tables
// at startup and never frees them).
//
// A "leak:" line matches any frame of the allocation stack by function, file or
// module name.

extern "C" __attribute__((used)) const char *
__lsan_default_suppressions()
{
    return "leak:thirdparty/microtex\n"
           "leak:thirdparty/tinyxml2\n"
           "leak:thirdparty/synctex\n"
           "leak:thirdparty/mupdf\n"
           "leak:thirdparty/luajit\n";
}
