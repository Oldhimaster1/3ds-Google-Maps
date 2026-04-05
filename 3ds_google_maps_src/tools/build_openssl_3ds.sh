#!/usr/bin/env bash
# =============================================================================
# build_openssl_3ds.sh
# Cross-compile OpenSSL 3.6.0 for Nintendo 3DS (armv6k) using devkitARM.
#
# Run from MSYS2 bash with devkitPro set up:
#   export DEVKITPRO=/c/devkitPro
#   export DEVKITARM=$DEVKITPRO/devkitARM
#   bash tools/build_openssl_3ds.sh
#
# Output: thirdparty/openssl-3ds/
#   include/openssl/*.h   <- OpenSSL headers
#   lib/libssl.a          <- TLS library
#   lib/libcrypto.a       <- Crypto library
# =============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
INSTALL_DIR="$PROJECT_ROOT/thirdparty/openssl-3ds"
BUILD_DIR="$PROJECT_ROOT/thirdparty/openssl-3ds-src"

REPO_URL="https://github.com/kynex7510/3ds_openssl.git"
REPO_BRANCH="openssl-3.6.0-n3ds"

echo "========================================================"
echo "  3DS OpenSSL Build Script"
echo "========================================================"
echo "  Source:  $BUILD_DIR"
echo "  Install: $INSTALL_DIR"
echo ""

# ---- Check prerequisites -------------------------------------------------

if [ -z "$DEVKITPRO" ]; then
    echo "ERROR: DEVKITPRO not set."
    echo ""
    echo "In MSYS bash, run:"
    echo "  export DEVKITPRO=/c/devkitPro"
    echo "  export DEVKITARM=\$DEVKITPRO/devkitARM"
    echo "  bash tools/build_openssl_3ds.sh"
    exit 1
fi

if [ -z "$DEVKITARM" ]; then
    export DEVKITARM="$DEVKITPRO/devkitARM"
fi

if [ ! -f "$DEVKITARM/bin/arm-none-eabi-gcc" ]; then
    echo "ERROR: arm-none-eabi-gcc not found in $DEVKITARM/bin/"
    echo "Check that DEVKITARM points to a valid devkitARM installation."
    exit 1
fi

if ! command -v perl &>/dev/null; then
    echo "ERROR: perl not found."
    echo "In MSYS2, install it with: pacman -S perl"
    exit 1
fi

# Text::Template is required by OpenSSL's Configure script.
if ! perl -e 'use Text::Template' 2>/dev/null; then
    echo "Text::Template not found — attempting automatic install via curl..."
    # Bypass CPAN SSL issues by downloading the tarball directly.
    TT_URL="https://cpan.metacpan.org/authors/id/M/MS/MSCHOUT/Text-Template-1.61.tar.gz"
    TT_TMP="/tmp/Text-Template.tar.gz"
    if ! curl -fksSL "$TT_URL" -o "$TT_TMP"; then
        echo "ERROR: curl failed to download Text::Template. Install manually:"
        echo "  curl -kLo /tmp/tt.tar.gz $TT_URL"
        echo "  cd /tmp && tar xzf tt.tar.gz && cd Text-Template-* && perl Makefile.PL && make install"
        exit 1
    fi
    cd /tmp && tar xzf Text-Template.tar.gz
    TT_DIR=$(find /tmp -maxdepth 1 -type d -name 'Text-Template-*' | head -1)
    cd "$TT_DIR" && perl Makefile.PL PREFIX=/usr && make && make install DESTDIR=
    cd "$PROJECT_ROOT"
    if ! perl -e 'use Text::Template' 2>/dev/null; then
        echo "ERROR: Text::Template install failed. Try manually:"
        echo "  cd '$TT_DIR' && perl Makefile.PL && make install"
        exit 1
    fi
    echo "Text::Template installed successfully."
fi

# ---- Already built? -------------------------------------------------------

if [ -f "$INSTALL_DIR/lib/libssl.a" ] && [ -f "$INSTALL_DIR/lib/libcrypto.a" ]; then
    echo "OpenSSL for 3DS is already built."
    echo "  $INSTALL_DIR/lib/libssl.a"
    echo "  $INSTALL_DIR/lib/libcrypto.a"
    echo ""
    echo "Delete '$INSTALL_DIR' to force a rebuild."
    exit 0
fi

# ---- Clone source ---------------------------------------------------------

if [ ! -d "$BUILD_DIR/.git" ]; then
    echo "Cloning $REPO_URL (branch: $REPO_BRANCH)..."
    git clone --branch "$REPO_BRANCH" --depth 1 "$REPO_URL" "$BUILD_DIR"
    echo "Clone done."
else
    echo "Source already cloned at $BUILD_DIR"
fi

cd "$BUILD_DIR"

# ---- Configure ------------------------------------------------------------

export PATH="$DEVKITARM/bin:$PATH"

# The 51-n3ds.conf already hardcodes full paths from $DEVKITARM for CC/AR/RANLIB.
# Do NOT set CROSS_COMPILE or CC here — that would cause the prefix to be doubled
# (e.g. arm-none-eabi-arm-none-eabi-gcc).
unset CROSS_COMPILE CC CXX AR RANLIB

mkdir -p "$INSTALL_DIR"

echo ""
echo "Configuring OpenSSL for 'n3ds' target..."
echo "(This uses the 3DS-specific configuration in Configurations/51-n3ds.conf)"
echo ""

perl Configure n3ds \
    --prefix="$INSTALL_DIR" \
    --openssldir="$INSTALL_DIR" \
    no-shared \
    no-tests \
    no-apps \
    no-engine \
    no-comp \
    no-idea \
    no-seed \
    no-whirlpool \
    no-rc2 \
    no-rc4 \
    no-rc5 \
    no-md4 \
    no-bf \
    no-cast \
    no-camellia \
    no-aria \
    no-sm2 \
    no-sm3 \
    no-sm4 \
    no-ocsp \
    no-ts \
    no-srp \
    no-gost \
    no-ct \
    no-ui-console

# ---- Build ----------------------------------------------------------------

CORES=$(nproc 2>/dev/null || echo 4)
echo ""
echo "Building with $CORES parallel jobs..."
echo "(This will take 5-15 minutes on first build.)"
echo ""

make -j"$CORES" build_libs

# ---- Install headers and libraries ----------------------------------------

echo ""
echo "Installing headers and libraries to $INSTALL_DIR ..."
make install_dev

echo ""
echo "========================================================"
echo "  Build complete!"
echo "  libssl.a   : $INSTALL_DIR/lib/libssl.a"
echo "  libcrypto.a: $INSTALL_DIR/lib/libcrypto.a"
echo "  Headers    : $INSTALL_DIR/include/openssl/"
echo ""
echo "Now run: python tools/generate_cert.py"
echo "Then rebuild the 3DS app: make"
echo "========================================================"
