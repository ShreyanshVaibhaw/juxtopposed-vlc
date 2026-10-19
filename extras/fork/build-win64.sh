#!/usr/bin/env bash
# Cross-builds this fork for Windows x64 inside VideoLAN's own CI image, then
# copies the runnable player to $VLC_DEST (C:\dev\VLCREDESIGN\vlc-win64 by default).
#
# Run from WSL, with the checkout on the Linux filesystem and Docker Desktop's
# WSL integration on:  bash extras/fork/build-win64.sh
# First run takes 1-2 hours (Qt rebuild); later runs only recompile what changed.
set -euo pipefail

SRC=${VLC_SRC:-$(cd "$(dirname "$0")/../.." && pwd)}
DEST=${VLC_DEST:-/mnt/c/dev/VLCREDESIGN/vlc-win64}
IMAGE=registry.videolan.org/vlc-debian-win64-posix:20260611225331

INNER=$(cat <<'EOF'
set -euo pipefail
cd /vlc
BUILD="sh extras/package/win32/build.sh"
TRIPLET=x86_64-w64-mingw32
CONTRIB=contrib/contrib-win64
OUT=/vlc/win64/vlc-win64
# ponytail: build.sh always appends -g, so wrap the cross compilers to add -g0
# last (gcc obeys the last -g flag). Saves several GB across the Qt and VLC
# build trees. Delete this block when you need symbols for crash debugging.
mkdir -p /tmp/nodebug
for t in gcc g++; do
    real=$(command -v x86_64-w64-mingw32-$t)
    printf '#!/bin/sh\nexec %s "$@" -g0\n' "$real" > /tmp/nodebug/x86_64-w64-mingw32-$t
done
chmod +x /tmp/nodebug/*
export PATH=/tmp/nodebug:/vlc/extras/tools/build/bin:$PATH

# VideoLAN's prebuilt contribs for the upstream commit this fork branched from.
# The fork changes only two recipes on top: qt (adds Qt Network for subtitle
# search) and medialibrary (5 patches), so only those get rebuilt here.
UPSTREAM_URL=https://artifacts.videolan.org/vlc/win64/vlc-contrib-$TRIPLET-c45a62bed8f1b2b98994b13770bdea67f95d2a52.tar.zst

if [ ! -e $CONTRIB/.fork-contribs ]; then
    # Fetch the 618 MB tarball ourselves: the connection tends to drop mid-way,
    # and make's own curl gives up on that and falls back to building every
    # contrib from source. `make prebuilt` uses this file when it exists.
    TARBALL=$CONTRIB/vlc-contrib-$TRIPLET-latest.tar.zst
    if [ ! -e $TARBALL ]; then
        mkdir -p $CONTRIB
        for i in $(seq 30); do
            curl -fsSL -C - --retry 5 --retry-all-errors -o $TARBALL.part "$UPSTREAM_URL" && break
            echo "download interrupted (attempt $i), resuming"; sleep 5
        done
        zstd -tq $TARBALL.part   # fails loudly on a truncated or corrupt file
        mv $TARBALL.part $TARBALL
    fi

    # Pass 1: unpack upstream contribs. meson setup then stops on the missing
    # Qt6Network module, which is expected and costs no compile time.
    # Skipped on reruns so an already rebuilt Qt is not wiped. `make prebuilt`
    # moves lib/ into place only after a complete unpack.
    if [ ! -d contrib/$TRIPLET/lib ]; then
        $BUILD -a x86_64 -p -m || true
        test -d contrib/$TRIPLET/lib || { echo "prebuilt contribs did not unpack"; exit 1; }
    fi

    # Pass 2: rebuild the two contribs whose recipes the fork changed.
    # `make prebuilt` leaves no build stamps, so make would rebuild every qt
    # dependency from source. Overriding PKGS_FOUND tells it everything else
    # is already installed, which the prebuilt tarball guarantees.
    ALL=$(make -C $CONTRIB -s --no-print-directory --eval='print-all: ; @echo $(PKGS_ALL)' print-all)
    FOUND=$(for p in $ALL; do case $p in qt|medialibrary) ;; *) printf '%s ' $p ;; esac; done)
    make -C $CONTRIB -j"$(nproc)" .qt .medialibrary PKGS_FOUND="$FOUND"

    # Repack under the file name `make prebuilt` looks for, so every later
    # `build.sh -p` restores these contribs instead of the upstream ones.
    tar -C contrib -c $TRIPLET | zstd -q -T0 -3 -f -o $CONTRIB/vlc-contrib-$TRIPLET-latest.tar.zst
    rm -rf $CONTRIB/qt $CONTRIB/medialibrary   # build trees, several GB
    touch $CONTRIB/.fork-contribs
fi

# VLC itself builds with autotools, the path VideoLAN's CI uses with this
# image. Meson would need to run test .exe files through wine, and the image
# only ships 32-bit wine. The fork keeps Makefile.am in sync with meson.build.
if [ ! -e win64/config.status ]; then
    # Pass 3: full configure + compile, then package into $OUT.
    $BUILD -a x86_64 -p -o $OUT
else
    # Incremental: recompile only what changed since the last build.
    make -C win64 -j"$(nproc)"
    make -C win64 package-win-common
fi
EOF
)

# The image has no account for our uid, and contrib's meson (medialibrary)
# fails without a home directory. Give it one, kept between builds.
STATE=${XDG_CACHE_HOME:-$HOME/.cache}/vlc-build
mkdir -p "$STATE/home"
docker run --rm "$IMAGE" cat /etc/passwd > "$STATE/passwd"
echo "builder:x:$(id -u):$(id -g)::/home/builder:/bin/bash" >> "$STATE/passwd"

docker run --rm -u "$(id -u):$(id -g)" \
    -v "$SRC:/vlc" -v "$STATE/home:/home/builder" -v "$STATE/passwd:/etc/passwd:ro" \
    "$IMAGE" bash -c "$INNER"

mkdir -p "$DEST"
rsync -a --delete --exclude portable/ "$SRC/win64/vlc-win64/" "$DEST/"
mkdir -p "$DEST/portable"   # keeps this copy's settings next to vlc.exe
echo "Built: $(wslpath -w "$DEST")\\vlc.exe"
