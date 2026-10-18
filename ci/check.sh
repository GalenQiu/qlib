#!/usr/bin/env bash
#
# One-command local verification for qlib.
#
# This is the local equivalent of the job matrix in .github/workflows/ci.yml,
# for when CI is unreachable or simply not wanted for a quick change.
#
#   ./ci/check.sh                     everything
#   ./ci/check.sh --quick             skip the sanitizer build and fuzzing
#   ./ci/check.sh --fuzz-seconds=30   fuzz budget per target (default 10)
#
# Every step reports PASS, FAIL or SKIP, and the summary distinguishes them:
# a SKIP means a tool was missing, never that the check succeeded. The exit
# status is 0 only when nothing failed.
#
# Scratch build trees are named build-check-*, which .gitignore already covers.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

QUICK=0
FUZZ_SECONDS=10
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=1 ;;
        --fuzz-seconds=*) FUZZ_SECONDS="${arg#*=}" ;;
        -h|--help)
            sed -n '3,17p' "$0" | sed 's/^#\{1,2\} \{0,1\}//'
            exit 0
            ;;
        *)
            echo "check.sh: unknown option: $arg" >&2
            exit 2
            ;;
    esac
done

SCRATCH="$ROOT/build-check"
rm -rf "$SCRATCH"
mkdir -p "$SCRATCH"

PASS=0
FAIL=0
SKIP=0
FAILED_STEPS=""

have() { command -v "$1" >/dev/null 2>&1; }

report() {
    local name="$1" status="$2" detail="${3:-}"
    case "$status" in
        PASS) PASS=$((PASS + 1)) ;;
        FAIL)
            FAIL=$((FAIL + 1))
            FAILED_STEPS="$FAILED_STEPS $name"
            ;;
        SKIP) SKIP=$((SKIP + 1)) ;;
    esac
    printf '%-14s %-4s %s\n' "$name" "$status" "$detail"
}

# Steps return 0 (pass), 1 (fail) or 2 (skipped, with the reason on stdout).
run_step() {
    local name="$1"
    local log="$SCRATCH/$name.log"
    local rc=0

    "$name" >"$log" 2>&1 || rc=$?

    case "$rc" in
        0) report "$name" PASS "" ;;
        2) report "$name" SKIP "$(head -1 "$log")" ;;
        *)
            report "$name" FAIL "log: build-check/$name.log"
            sed 's/^/                 /' "$log" | tail -25
            ;;
    esac
    return 0
}

# ---------------------------------------------------------------------------
# Steps
# ---------------------------------------------------------------------------

step_links() {
    have python3 || { echo "python3 not found"; return 2; }
    python3 ci/check_links.py
}

step_i18n() {
    have python3 || { echo "python3 not found"; return 2; }
    python3 ci/check_i18n.py
}

step_format() {
    have clang-format || { echo "clang-format not found"; return 2; }

    local files
    if git rev-parse --git-dir >/dev/null 2>&1; then
        files="$(git ls-files '*.c' '*.h')"
    else
        files="$(find src include tests fuzz bench -name '*.c' -o -name '*.h' | sort)"
    fi
    # shellcheck disable=SC2086
    clang-format --dry-run --Werror $files
}

step_win32() {
    # Type-check the branch that only MSVC normally sees. See
    # ci/win32-syntax/windows.h for what this does and does not prove.
    have cc || { echo "no C compiler found"; return 2; }

    cmake -S . -B "$SCRATCH/win32" -DQLIB_BUILD_BENCH=ON >/dev/null 2>&1 || {
        echo "configure failed"
        return 1
    }
    cc -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow \
        -D_WIN32 -Ici/win32-syntax -Iinclude -I"$SCRATCH/win32/include" \
        -fsyntax-only bench/bench_main.c
}

step_build() {
    have cmake || { echo "cmake not found"; return 2; }

    cmake -S . -B "$SCRATCH/build" -DQLIB_WERROR=ON -DQLIB_BUILD_BENCH=ON >/dev/null || return 1
    cmake --build "$SCRATCH/build" >/dev/null || return 1
    ctest --test-dir "$SCRATCH/build" --output-on-failure
}

step_net_off() {
    have cmake || { echo "cmake not found"; return 2; }

    cmake -S . -B "$SCRATCH/nonet" -DQLIB_WERROR=ON -DQLIB_BUILD_NET=OFF >/dev/null || return 1
    cmake --build "$SCRATCH/nonet" >/dev/null || return 1
    ctest --test-dir "$SCRATCH/nonet" --output-on-failure
}

step_tidy() {
    have clang-tidy || { echo "clang-tidy not found"; return 2; }

    # Threads on: the suite prototypes are behind QLIB_HAS_THREADS, and
    # clang-tidy falls back to a default command for files that have no entry in
    # compile_commands.json.
    cmake -S . -B "$SCRATCH/build" -DQLIB_WERROR=ON -DQLIB_BUILD_BENCH=ON \
        -DQLIB_BUILD_THREADS=ON -DQLIB_BUILD_LOG=ON -DQLIB_BUILD_EXAMPLES=ON \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null || return 1
    cmake --build "$SCRATCH/build" >/dev/null || return 1
    # Every directory that holds code the library ships. examples/ was missing
    # here for a while, which is why the demo collected eighteen findings the
    # moment it was added to the list.
    clang-tidy src/*.c tests/*.c bench/*.c fuzz/*.c examples/*.c -p "$SCRATCH/build"
}

step_threads() {
    have cmake || { echo "cmake not found"; return 2; }

    # Built with both optional modules that need the threading layer, and with
    # ThreadSanitizer when clang is available: a threading module that is only
    # ever tested without a race detector is not really tested.
    local flags=""
    if have clang; then
        flags="-DCMAKE_C_COMPILER=clang -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread"
    fi

    # shellcheck disable=SC2086
    cmake -S . -B "$SCRATCH/threads" -DQLIB_WERROR=ON -DQLIB_BUILD_THREADS=ON \
        -DQLIB_BUILD_LOG=ON -DQLIB_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Debug $flags >/dev/null || return 1
    cmake --build "$SCRATCH/threads" >/dev/null || return 1
    ctest --test-dir "$SCRATCH/threads" --output-on-failure
}

step_package() {
    have cmake || { echo "cmake not found"; return 2; }

    local prefix="$SCRATCH/prefix"
    cmake -S . -B "$SCRATCH/pkg" -DQLIB_WERROR=ON -DQLIB_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX="$prefix" >/dev/null || return 1
    cmake --build "$SCRATCH/pkg" >/dev/null || return 1
    cmake --install "$SCRATCH/pkg" >/dev/null || return 1

    # ci/consumer is the same fixture the CI package job builds, so this step
    # verifies exactly what CI verifies.
    echo "--- find_package ---"
    cmake -S ci/consumer -B "$SCRATCH/consumer" -DCMAKE_PREFIX_PATH="$prefix" >/dev/null || return 1
    cmake --build "$SCRATCH/consumer" >/dev/null || return 1
    "$SCRATCH/consumer/consumer" || return 1

    if have pkg-config; then
        echo "--- pkg-config ---"
        # shellcheck disable=SC2046
        cc ci/consumer/main.c \
            $(PKG_CONFIG_PATH="$prefix/lib/pkgconfig" pkg-config --cflags --libs qlib) \
            -o "$SCRATCH/consumer_pc" || return 1
        "$SCRATCH/consumer_pc" || return 1
    else
        echo "pkg-config not found; find_package path only"
    fi
}

step_docs() {
    have doxygen || { echo "doxygen not found"; return 2; }
    have cmake || { echo "cmake not found"; return 2; }

    cmake -S . -B "$SCRATCH/docs" -DQLIB_BUILD_DOCS=ON -DQLIB_BUILD_TESTS=OFF >/dev/null || return 1

    # Capture the log, then insist it exists before reading it: grepping a file
    # that is not there exits non-zero, which reads as "no warnings found".
    cmake --build "$SCRATCH/docs" --target docs >"$SCRATCH/doxygen.log" 2>&1 || return 1
    [ -f docs/html/index.html ] || { echo "docs/html/index.html was not generated"; return 1; }
    [ -f "$SCRATCH/doxygen.log" ] || { echo "no doxygen log to inspect"; return 1; }
    if grep -q "warning:" "$SCRATCH/doxygen.log"; then
        echo "doxygen reported $(grep -c 'warning:' "$SCRATCH/doxygen.log") warning(s):"
        grep "warning:" "$SCRATCH/doxygen.log"
        return 1
    fi
    echo "no Doxygen warnings"
}

step_sanitizers() {
    have cmake || { echo "cmake not found"; return 2; }
    if [ "$QUICK" -eq 1 ]; then
        echo "skipped by --quick"
        return 2
    fi

    cmake -S . -B "$SCRATCH/asan" -DQLIB_SANITIZE=ON -DQLIB_WERROR=ON \
        -DQLIB_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null || return 1
    cmake --build "$SCRATCH/asan" >/dev/null || return 1
    ctest --test-dir "$SCRATCH/asan" --output-on-failure
}

step_fuzz() {
    if [ "$QUICK" -eq 1 ]; then
        echo "skipped by --quick"
        return 2
    fi
    # -fsanitize=fuzzer is a Clang feature.
    if ! (have clang && have cmake); then
        echo "clang or cmake not found"
        return 2
    fi

    CC=clang cmake -S . -B "$SCRATCH/fuzz" -DQLIB_BUILD_FUZZ=ON -DQLIB_WERROR=ON >/dev/null || return 1
    cmake --build "$SCRATCH/fuzz" >/dev/null || return 1

    # Every target that was built, not a hard-coded list: this loop used to name
    # three of the six and silently skipped the rest.
    #
    # -artifact_prefix keeps libFuzzer's crashing input inside the scratch tree.
    # Its default is the working directory, which put crash-<sha1> files in the
    # repository root.
    local target
    for target in "$SCRATCH"/fuzz/bin/fuzz_*; do
        [ -x "$target" ] || continue
        echo "--- $(basename "$target") (${FUZZ_SECONDS}s) ---"
        "$target" -max_total_time="$FUZZ_SECONDS" -max_len=256 \
            -artifact_prefix="$SCRATCH/fuzz/" || return 1
    done
    echo "all fuzz targets clean"
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------

echo "qlib local verification"
echo "  root     $ROOT"
echo "  mode     $([ "$QUICK" -eq 1 ] && echo quick || echo full)"
for tool in cc cmake clang clang-format clang-tidy doxygen python3; do
    if have "$tool"; then
        printf '  %-12s %s\n' "$tool" "$("$tool" --version 2>/dev/null | head -1)"
    else
        printf '  %-12s %s\n' "$tool" "not found"
    fi
done
echo

run_step step_links
run_step step_i18n
run_step step_format
run_step step_win32
run_step step_build
run_step step_net_off
run_step step_tidy
run_step step_threads
run_step step_package
run_step step_docs
run_step step_sanitizers
run_step step_fuzz

echo
echo "$PASS passed, $FAIL failed, $SKIP skipped"
if [ "$FAIL" -ne 0 ]; then
    echo "failed:$FAILED_STEPS"
    exit 1
fi
exit 0
