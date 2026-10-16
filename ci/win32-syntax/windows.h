/*
 * A syntax stub for <windows.h>, used only to type-check qlib's code that is
 * guarded by `#ifdef _WIN32`.
 *
 * It is NOT a Windows SDK and NOT a substitute for one. It exists because the
 * 32-bit and 64-bit MSVC jobs in CI are the only place that code is normally
 * compiled, and those are unreachable when CI is not being run. Declaring just
 * the handful of Win32 names qlib uses lets a plain Linux compiler check the
 * syntax, the member names and the argument counts of that branch, which is
 * where a typo would otherwise sit undetected until a release.
 *
 * What it cannot check: anything about the real headers. If the SDK spells a
 * name differently, or a signature changes, only the real compiler will say so.
 *
 * See ci/check.sh, which compiles bench/bench_main.c with -D_WIN32 against this
 * file and the project's own warning flags.
 */

#ifndef QLIB_WIN32_SYNTAX_STUB_H
#define QLIB_WIN32_SYNTAX_STUB_H

typedef union
{
    long long QuadPart;
} LARGE_INTEGER;

int QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency);
int QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount);

#endif /* QLIB_WIN32_SYNTAX_STUB_H */
