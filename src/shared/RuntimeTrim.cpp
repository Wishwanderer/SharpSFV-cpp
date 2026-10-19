// SharpSFV (C++ port) - executable size trimming for MinGW (libstdc++) builds.
//
// Copyright (C) 2025 L33T (original C# SharpSFV)
// Modified 2026-10-03: added for the C++20 port.
//
// This program is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License version 3 as published by the Free Software Foundation.
// This program is distributed WITHOUT ANY WARRANTY. See LICENSE.txt for details.
//
// Linked into the shipped executables only (not the library or the tests), MinGW builds only.
// It replaces three pieces of libstdc++ that cost far more than they give a GUI program:
//
// 1. The default std::terminate handler prints the uncaught exception's demangled type name,
//    which links the C++ demangler and a printf engine (~60 KB). A GUI program has nowhere to
//    print; this one simply aborts, as the original does after printing.
// 2. The container/string error helpers (std::__throw_length_error and friends) construct
//    std::logic_error and std::runtime_error, whose message strings drag in a
//    second, copy-on-write std::string implementation and the error-category machinery
//    (~45 KB). These replacements throw a small std::exception subclass with the same message
//    text instead. The program only ever catches std::exception or "...", so behaviour is
//    unchanged: the conditions still raise a C++ exception and are handled the same way.
//    (bad_alloc and bad_array_new_length are still thrown as themselves.)
//
// If a future libstdc++ needs another helper from the same object file, the link fails with a
// "multiple definition" error rather than misbehaving; add the helper here.
#include <cstdlib> // defines __GLIBCXX__ when the library is libstdc++
#include <exception>
#include <new>

#if defined(__MINGW32__) && defined(__GLIBCXX__)

namespace {
struct LibraryError final : std::exception {
    explicit LibraryError(const char* m) noexcept : msg(m ? m : "std library error") {}
    const char* what() const noexcept override { return msg; }
    const char* msg;
};
} // namespace

namespace __gnu_cxx {
void __verbose_terminate_handler() { std::abort(); }
} // namespace __gnu_cxx

namespace std {
[[noreturn]] void __throw_bad_exception() { throw LibraryError("std::bad_exception"); }
[[noreturn]] void __throw_bad_alloc() { throw std::bad_alloc(); }
[[noreturn]] void __throw_bad_array_new_length() { throw std::bad_array_new_length(); }
[[noreturn]] void __throw_bad_cast() { throw LibraryError("std::bad_cast"); }
[[noreturn]] void __throw_bad_typeid() { throw LibraryError("std::bad_typeid"); }
[[noreturn]] void __throw_logic_error(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_domain_error(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_invalid_argument(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_length_error(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_out_of_range(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_out_of_range_fmt(const char* m, ...) { throw LibraryError(m); }
[[noreturn]] void __throw_runtime_error(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_range_error(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_overflow_error(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_underflow_error(const char* m) { throw LibraryError(m); }
[[noreturn]] void __throw_bad_function_call() { throw LibraryError("std::bad_function_call"); }
} // namespace std

#endif
