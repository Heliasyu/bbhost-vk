#!/bin/bash
# bbhost-vk's reproducible Linux build: the same build as the CI's
# (.github/workflows/build.yml, tools/ci_build.sh) in its portable release
# form - clang + lld, RelWithDebInfo with BBHOST_RELEASE=ON, libstdc++ and
# libgcc linked statically, Vulkan-Headers, SDL3 and ffmpeg built into a
# prefix at the versions ci_build.sh pins - then ctest, then the debug info
# stripped exactly as tools/package_linux.sh strips the release binary
# (objcopy --strip-debug), with its SHA-256 beside it.
#
#   packaging/vk/build.sh                     build into build-ci/, write dist/vk/bbhost.bin
#   packaging/vk/build.sh -o DIR              write DIR/bbhost.bin and DIR/bbhost.bin.sha256
#   CI_APT=0 packaging/vk/build.sh            the packages are already installed (no sudo apt)
#   CI_WORK=/somewhere packaging/vk/build.sh  another work folder (the prefix is rebuilt there)
#   BUILD_SKIP_TESTS=1 ...                    stop after the build (ctest is part of ci_build.sh;
#                                             this only skips it when set)
#   PGO_PROFILE=bbhost.profdata ...           rc6: bbhost.bin with that merged profile (ThinLTO +
#                                             PGO), and bbhost.bin.nopgo, the same commit without
#                                             it (build-ci/build-nopgo), for an A/B. PGO_WARN=1:
#                                             the compiler reports the functions the profile no
#                                             longer matches (bbhost-build.log)
#
# Reproducible here means: the same sources (this commit, the submodules at
# their pinned commits), the same toolchain and the same prefix give the same
# binary. The reference build host is Debian 13 (trixie) with clang 19 and
# glibc 2.41; the result needs glibc 2.38 at most, so it runs on current
# Arch/CachyOS, Fedora and Ubuntu 24.04+. On another distribution or compiler
# it still builds, but the bytes differ - the script says so.
set -eu
cd "$(dirname "$(readlink -f "$0")")/../.."
SRC=$PWD
OUT=$SRC/dist/vk
while [ $# -gt 0 ]; do
    case "$1" in
        -o|--out) OUT=$(readlink -m "$2"); shift 2 ;;
        -h|--help) sed -n '2,24p' "$0"; exit 0 ;;
        *) echo "build.sh: unknown argument $1" >&2; exit 2 ;;
    esac
done

# The toolchain the reference binary was built with.
if [ -r /etc/os-release ]; then
    . /etc/os-release
    [ "${ID:-}" = debian ] && [ "${VERSION_ID:-}" = 13 ] ||
        echo "build.sh: note: the reference build host is Debian 13; this is ${PRETTY_NAME:-unknown} (the binary will differ)"
fi
if command -v clang > /dev/null; then
    cv=$(clang -dumpversion 2> /dev/null || echo "?")
    case "$cv" in 19.*) ;; *) echo "build.sh: note: the reference compiler is clang 19; this is clang $cv" ;; esac
fi

# FSR-Vulkan (third_party/FSR-Vulkan) and anything it pins.
if [ -e .git ]; then
    git submodule sync --recursive > /dev/null
    git submodule update --init --recursive
fi
[ -f third_party/FSR-Vulkan/CMakeLists.txt ] || { echo "build.sh: third_party/FSR-Vulkan is empty" >&2; exit 1; }

# Reproducible archives and paths: the commit's time, not now.
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2> /dev/null || date +%s)}

export CI_PORTABLE=1
W=${CI_WORK:-$SRC/build-ci}
PGO_ARGS="-DBBHOST_PGO_USE="
if [ -n "${PGO_PROFILE:-}" ]; then
    [ -f "$PGO_PROFILE" ] || { echo "build.sh: no PGO profile $PGO_PROFILE" >&2; exit 1; }
    pw=OFF
    [ "${PGO_WARN:-0}" = 1 ] && pw=ON
    PGO_ARGS="-DBBHOST_PGO=ON -DBBHOST_PGO_USE=$(readlink -f "$PGO_PROFILE") -DBBHOST_PGO_WARN=$pw"
fi
run_ci() {  # run_ci CMAKE-ARGS [BUILD-DIR]
    export CI_CMAKE_ARGS="-DBBHOST_RELEASE=ON $1 ${CI_CMAKE_ARGS_EXTRA:-}"
    if [ -n "${2:-}" ]; then export CI_BUILD_DIR=$2; else unset CI_BUILD_DIR; fi
    if [ "${BUILD_SKIP_TESTS:-0}" = 1 ]; then
        # ci_build.sh's steps without its ctest: run it with ctest made a no-op.
        tmp=$(mktemp -d)
        printf '#!/bin/sh\necho "ctest skipped (BUILD_SKIP_TESTS=1)"\n' > "$tmp/ctest"
        chmod +x "$tmp/ctest"
        PATH="$tmp:$PATH" tools/ci_build.sh
        rm -rf "$tmp"
    else
        tools/ci_build.sh
    fi
}
run_ci "$PGO_ARGS"

bin=$W/build/bbhost
[ -x "$bin" ] || { echo "build.sh: no $bin" >&2; exit 1; }
mkdir -p "$OUT"
objcopy --strip-debug "$bin" "$OUT/bbhost.bin" 2> /dev/null || llvm-objcopy --strip-debug "$bin" "$OUT/bbhost.bin"
(cd "$OUT" && sha256sum bbhost.bin > bbhost.bin.sha256)
if [ -n "${PGO_PROFILE:-}" ]; then
    # The same commit without the profile (the prefix is shared; the
    # packages are in by now).
    CI_APT=0 run_ci "-DBBHOST_PGO=OFF -DBBHOST_PGO_USE=" "$W/build-nopgo"
    objcopy --strip-debug "$W/build-nopgo/bbhost" "$OUT/bbhost.bin.nopgo" 2> /dev/null ||
        llvm-objcopy --strip-debug "$W/build-nopgo/bbhost" "$OUT/bbhost.bin.nopgo"
    (cd "$OUT" && sha256sum bbhost.bin.nopgo > bbhost.bin.nopgo.sha256)
fi
rev=$(git describe --tags --always --dirty 2> /dev/null || echo unknown)
glibc=$(objdump -T "$OUT/bbhost.bin" 2> /dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1 || true)
{
    echo "revision: $rev"
    echo "commit:   $(git rev-parse HEAD 2> /dev/null || echo unknown)"
    echo "fsr-vulkan: $(git -C third_party/FSR-Vulkan rev-parse HEAD 2> /dev/null || echo unknown)"
    echo "built on: ${PRETTY_NAME:-unknown}, clang ${cv:-?}"
    echo "needs:    ${glibc:-?}"
    echo "sha256:   $(cut -d' ' -f1 "$OUT/bbhost.bin.sha256")"
    if [ -n "${PGO_PROFILE:-}" ]; then
        echo "pgo:      $(basename "$PGO_PROFILE") (sha256 $(sha256sum "$PGO_PROFILE" | cut -d' ' -f1))"
        echo "nopgo:    $(cut -d' ' -f1 "$OUT/bbhost.bin.nopgo.sha256")"
    else
        echo "pgo:      none"
    fi
} > "$OUT/BUILDINFO.txt"
cat "$OUT/BUILDINFO.txt"
echo "build.sh: $OUT/bbhost.bin (stripped; the unstripped binary is $bin)"
echo "Install: copy it over bbhost.bin beside the packaging/vk/bbhost wrapper in the game's bbhost folder."
