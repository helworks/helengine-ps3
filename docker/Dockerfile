# syntax=docker/dockerfile:1
#
# Helengine PS3 SDK image.
#
# Builds the ps3dev PPU cross-toolchain (binutils, gcc, newlib) and the PSL1GHT PPU
# libraries plus host tools from immutable upstream revisions, then copies the
# installed SDK into a slim runtime stage. Every input is pinned:
#   - the Debian base image by digest,
#   - apt packages through a snapshot.debian.org timestamp,
#   - ps3toolchain and PSL1GHT by commit,
#   - the toolchain source archives by the SHA-256 table inside the pinned
#     ps3toolchain commit (archives/archives.txt),
#   - the GCC in-tree prerequisites by the SHA-512 table inside the pinned GCC
#     release (contrib/download_prerequisites).
# The proprietary NVIDIA Cg toolkit is NOT part of this image. cgcomp loads
# libCg.so at run time from a read-only mount supplied by scripts/build.ps1.
#
# scripts/prepare_toolchain.ps1 supplies every ARG from toolchain-lock.json.

ARG BASE_IMAGE=debian:12-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171

FROM ${BASE_IMAGE} AS toolchain-builder

ARG APT_SNAPSHOT=20260901T000000Z
ARG PS3TOOLCHAIN_REPOSITORY=https://github.com/ps3dev/ps3toolchain.git
ARG PS3TOOLCHAIN_COMMIT=e13aa539c07abe86cdbcd095dc1340492dbca213
ARG PSL1GHT_REPOSITORY=https://github.com/ps3dev/PSL1GHT.git
ARG PSL1GHT_COMMIT=f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe
ARG BUILD_JOBS=8

ENV PS3DEV=/opt/ps3dev \
    PSL1GHT=/opt/ps3dev \
    DEBIAN_FRONTEND=noninteractive \
    NO_SAVANNAH=1
ENV PATH=/opt/ps3dev/bin:/opt/ps3dev/ppu/bin:/opt/ps3dev/spu/bin:${PATH}

# Pin apt to a Debian snapshot so package versions are reproducible. The base
# image ships without ca-certificates, so the snapshot is fetched over http and
# verified by apt's signed Release files.
RUN set -eux; \
    rm -f /etc/apt/sources.list.d/debian.sources; \
    printf 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/%s bookworm main\n' "${APT_SNAPSHOT}" > /etc/apt/sources.list; \
    printf 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/%s bookworm-security main\n' "${APT_SNAPSHOT}" >> /etc/apt/sources.list; \
    printf 'Acquire::Retries "5";\nAcquire::http::Timeout "60";\n' > /etc/apt/apt.conf.d/80-helengine-retries; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        autoconf automake bison build-essential bzip2 ca-certificates curl flex git \
        libelf-dev libgmp-dev libncurses-dev libssl-dev libtool libtool-bin \
        patch pkg-config texinfo wget xz-utils zlib1g-dev; \
    dpkg-query -W -f '${binary:Package}=${Version}\n' | sort > /opt/builder-apt-packages.txt; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build

# ps3toolchain at the pinned commit. Only the PPU steps run (001 binutils,
# 002 gcc+newlib, 004 symlinks); the SPU toolchain is deferred with SPU work.
RUN set -eux; \
    git init ps3toolchain; \
    git -C ps3toolchain remote add origin "${PS3TOOLCHAIN_REPOSITORY}"; \
    git -C ps3toolchain fetch --depth 1 origin "${PS3TOOLCHAIN_COMMIT}"; \
    git -C ps3toolchain checkout --detach "${PS3TOOLCHAIN_COMMIT}"; \
    test "$(git -C ps3toolchain rev-parse HEAD)" = "${PS3TOOLCHAIN_COMMIT}"

RUN set -eux; \
    cd ps3toolchain; \
    mkdir -p "${PS3DEV}"; \
    ./toolchain.sh 1 2 4 2>&1 | tail -n 200; \
    test -x "${PS3DEV}/ppu/bin/ppu-gcc"; \
    test -x "${PS3DEV}/ppu/bin/ppu-g++"; \
    test -x "${PS3DEV}/ppu/bin/ppu-readelf"

# PSL1GHT at the pinned commit. The PPU runtime, sprx import stubs, librsx and
# the host tools (cgcomp, sprxlinker, make_self, fself, bin2s, sfo/pkg, raw2h)
# are built; the SPU libraries and libspumars are deferred with SPU work.
RUN set -eux; \
    git init PSL1GHT; \
    git -C PSL1GHT remote add origin "${PSL1GHT_REPOSITORY}"; \
    git -C PSL1GHT fetch --depth 1 origin "${PSL1GHT_COMMIT}"; \
    git -C PSL1GHT checkout --detach "${PSL1GHT_COMMIT}"; \
    test "$(git -C PSL1GHT rev-parse HEAD)" = "${PSL1GHT_COMMIT}"

RUN set -eux; \
    cd PSL1GHT; \
    make install-ctrl; \
    make -C ppu -j"${BUILD_JOBS}"; \
    make -C ppu install; \
    make -C tools; \
    make -C tools install; \
    test -f "${PSL1GHT}/ppu_rules"; \
    test -f "${PSL1GHT}/ppu/lib/librsx.a"; \
    test -f "${PSL1GHT}/ppu/lib/libgcm_sys.a"; \
    test -f "${PSL1GHT}/ppu/lib/libsysutil.a"; \
    test -x "${PS3DEV}/bin/cgcomp"; \
    test -x "${PS3DEV}/bin/sprxlinker"; \
    test -x "${PS3DEV}/bin/make_self"; \
    test -x "${PS3DEV}/bin/fself"; \
    test -x "${PS3DEV}/bin/bin2s"

# Record the resolved identity of everything that went into the SDK, and keep
# the upstream licence notices next to the installed binaries.
RUN set -eux; \
    mkdir -p "${PS3DEV}/helengine"; \
    cp ps3toolchain/LICENSE "${PS3DEV}/helengine/LICENSE.ps3toolchain"; \
    cp PSL1GHT/LICENSE "${PS3DEV}/helengine/LICENSE.PSL1GHT"; \
    cp ps3toolchain/archives/archives.txt "${PS3DEV}/helengine/ps3toolchain-archives.txt"; \
    cp /opt/builder-apt-packages.txt "${PS3DEV}/helengine/builder-apt-packages.txt"; \
    { \
      echo "Ps3ToolchainCommit=${PS3TOOLCHAIN_COMMIT}"; \
      echo "Psl1ghtCommit=${PSL1GHT_COMMIT}"; \
      echo "AptSnapshot=${APT_SNAPSHOT}"; \
      echo "CompilerC=$(ppu-gcc --version | head -n1)"; \
      echo "CompilerCxx=$(ppu-g++ --version | head -n1)"; \
      echo "CompilerTarget=$(ppu-gcc -dumpmachine)"; \
      echo "Binutils=$(ppu-ld --version | head -n1)"; \
      echo "BinutilsArchive=$(grep -E 'binutils-' ps3toolchain/archives/archives.txt | awk '{print $1" "$3}')"; \
      echo "GccArchive=$(grep -E 'gcc-13' ps3toolchain/archives/archives.txt | awk '{print $1" "$3}')"; \
      echo "NewlibArchive=$(grep -E 'newlib-' ps3toolchain/archives/archives.txt | awk '{print $1" "$3}')"; \
      echo "GccPrerequisites=$(grep -E '^(gmp|mpfr|mpc|isl|gettext)=' ps3toolchain/build/gcc-13.2.0/contrib/download_prerequisites | tr '\n' ' ')"; \
      echo "GccPrerequisiteVerification=$(grep -c 'sha512' ps3toolchain/build/gcc-13.2.0/contrib/download_prerequisites) sha512 references in contrib/download_prerequisites"; \
      echo "NewlibPatchSha256=$(sha256sum ps3toolchain/patches/newlib-1.20.0-PS3.patch | awk '{print $1}')"; \
      echo "GccPatchSha256=$(sha256sum ps3toolchain/patches/gcc-13.2.0-PS3.patch | awk '{print $1}')"; \
      echo "BinutilsPatchSha256=$(sha256sum ps3toolchain/patches/binutils-2.22-PS3.patch | awk '{print $1}')"; \
      echo "MachDep=-mhard-float -fmodulo-sched -ffunction-sections -fdata-sections"; \
      echo "CpuFlag=-mcpu=cell"; \
      echo "SpuToolchain=not built (deferred with SPU work)"; \
    } > "${PS3DEV}/helengine/toolchain-record.txt"; \
    cat "${PS3DEV}/helengine/toolchain-record.txt"

# ---------------------------------------------------------------------------
# Runtime stage: only the installed SDK and the shared libraries its host tools
# need (libelf for sprxlinker, gmp/crypto/zlib for make_self, libstdc++ for
# cgcomp and the compiler drivers). The proprietary Cg toolkit is mounted at
# /opt/cg by the build wrapper and is not present in any layer.
# ---------------------------------------------------------------------------
FROM ${BASE_IMAGE} AS sdk

ARG APT_SNAPSHOT=20260901T000000Z
ARG PS3TOOLCHAIN_COMMIT=e13aa539c07abe86cdbcd095dc1340492dbca213
ARG PSL1GHT_COMMIT=f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe

ENV PS3DEV=/opt/ps3dev \
    PSL1GHT=/opt/ps3dev \
    DEBIAN_FRONTEND=noninteractive \
    HELENGINE_PS3_CG_ROOT=/opt/cg
ENV PATH=/opt/ps3dev/bin:/opt/ps3dev/ppu/bin:/opt/ps3dev/spu/bin:${PATH}
ENV LD_LIBRARY_PATH=/opt/cg/lib64

RUN set -eux; \
    rm -f /etc/apt/sources.list.d/debian.sources; \
    printf 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/%s bookworm main\n' "${APT_SNAPSHOT}" > /etc/apt/sources.list; \
    printf 'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/%s bookworm-security main\n' "${APT_SNAPSHOT}" >> /etc/apt/sources.list; \
    printf 'Acquire::Retries "5";\nAcquire::http::Timeout "60";\n' > /etc/apt/apt.conf.d/80-helengine-retries; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
        make libelf1 libgmp10 libssl3 libstdc++6 zlib1g; \
    mkdir -p /opt/ps3dev/helengine; \
    dpkg-query -W -f '${binary:Package}=${Version}\n' | sort > /opt/ps3dev/helengine/runtime-apt-packages.txt; \
    rm -rf /var/lib/apt/lists/*

COPY --from=toolchain-builder /opt/ps3dev /opt/ps3dev

LABEL org.helengine.ps3.ps3toolchain-commit="${PS3TOOLCHAIN_COMMIT}" \
      org.helengine.ps3.psl1ght-commit="${PSL1GHT_COMMIT}" \
      org.helengine.ps3.apt-snapshot="${APT_SNAPSHOT}" \
      org.helengine.ps3.compiler-target="powerpc64-ps3-elf" \
      org.helengine.ps3.cg-toolkit="mounted read-only at /opt/cg; not embedded"

WORKDIR /out
