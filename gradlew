#!/usr/bin/env bash
set -euo pipefail

GRADLE_VERSION="8.9"
DIST_URL="https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip"
CACHE_ROOT="${GRADLE_USER_HOME:-${HOME}/.gradle}/aetheris-wrapper"
DIST_DIR="${CACHE_ROOT}/gradle-${GRADLE_VERSION}"
ZIP_PATH="${CACHE_ROOT}/gradle-${GRADLE_VERSION}-bin.zip"

mkdir -p "${CACHE_ROOT}"

if [[ ! -x "${DIST_DIR}/bin/gradle" ]]; then
    command -v curl >/dev/null 2>&1 || { echo "curl is required." >&2; exit 1; }
    command -v unzip >/dev/null 2>&1 || { echo "unzip is required." >&2; exit 1; }

    if [[ ! -f "${ZIP_PATH}" ]]; then
        tmp="${ZIP_PATH}.tmp"
        trap 'rm -f "${tmp}"' EXIT
        curl --fail --location --retry 4 --retry-delay 2 --connect-timeout 20 --max-time 300             "${DIST_URL}" -o "${tmp}"
        mv "${tmp}" "${ZIP_PATH}"
        trap - EXIT
    fi

    rm -rf "${DIST_DIR}.tmp"
    mkdir -p "${DIST_DIR}.tmp"
    unzip -q "${ZIP_PATH}" -d "${DIST_DIR}.tmp"
    extracted="${DIST_DIR}.tmp/gradle-${GRADLE_VERSION}"
    test -x "${extracted}/bin/gradle"
    rm -rf "${DIST_DIR}"
    mv "${extracted}" "${DIST_DIR}"
    rm -rf "${DIST_DIR}.tmp"
fi

exec "${DIST_DIR}/bin/gradle" "$@"
