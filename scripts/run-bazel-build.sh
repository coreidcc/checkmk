#!/usr/bin/env bash

# Create a uniform Bazel call to ensure we're using the cache and inject
# system properties to avoid poisonous cache hits.
#
# This might be a workaround, feel free to inform team CI about a more
# sophisitcated way to do this, but we already know, we should take the whole
# build chain into the sandbox - this is just a rough approach.

set -e

ROOT_DIR="$(dirname "$(dirname "$(realpath "$0")")")"

TARGET="$*"
TARGET_S1=$(echo "${TARGET}" | sed -e 's/\/\///g' -e 's/@//g' -e 's/:/_/g' -e 's/\//_/g')
EXECUTION_LOG_FILE_NAME="${ROOT_DIR}/bazel_execution_log-${TARGET_S1}.json"

# explicitly create, and later `eval` since run directly with `eval` this script
# would not abort on error
BUILD_ENVIRONMENT="$(
    "${ROOT_DIR}"/scripts/run-pipenv run \
        "${ROOT_DIR}"/scripts/create_build_environment_variables.py \
        "eval:os-release-name:cat /etc/os-release | grep PRETTY | cut -d '\"' -f2" \
        "pathhash:/usr/lib/x86_64-linux-gnu/libc.so" \
        "pathhash:/lib64/libc.so.6" \
        "pathhash:/usr/lib64/libc.so" \
        "pathhash:/opt/gcc-13.2.0" \
        "env:PATH:/opt/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
)"

eval "${BUILD_ENVIRONMENT}"

echo "========================================================================="
echo "Environment variables taken into account by Bazel building \"$*\""
echo "  BAZEL_EXTRA_ARGS: $BAZEL_EXTRA_ARGS"
echo "  PATH: $PATH"
echo "  SYSTEM_DIGEST: $SYSTEM_DIGEST"
echo
echo "A file ${EXECUTION_LOG_FILE_NAME} will be generated by Bazel containing"
echo "information about caching"
echo "========================================================================="
bazel --version

if [ -z "${BAZEL_CACHE_URL}" ] || [ -z "${BAZEL_CACHE_USER}" ] || [ -z "${BAZEL_CACHE_PASSWORD}" ]; then
    echo
    echo "BAZEL REMOTE CACHING NOT CONFIGURED!"
    echo "To do so, set BAZEL_CACHE_URL, BAZEL_CACHE_USER and BAZEL_CACHE_PASSWORD"
    echo
    BAZEL_REMOTE_CACHE_ARGUMENT="--remote_cache="""
else
    echo "Bazel remote cache configured to \"${BAZEL_CACHE_URL}\""
    BAZEL_REMOTE_CACHE_ARGUMENT="--remote_cache=grpcs://${BAZEL_CACHE_USER}:${BAZEL_CACHE_PASSWORD}@${BAZEL_CACHE_URL}"
fi
echo "========================================================================="

# We encountered false cache hits with remote caching due to environment variables
# not being propagated to external dependeny builds
# In that case `--host_action_env=...` (in addition to `--action_env`) might help
# Currently we don't use any external dependencies though.

# shellcheck disable=SC2086
bazel build \
    --verbose_failures \
    --sandbox_debug \
    --subcommands=pretty_print \
    --execution_log_json_file="${EXECUTION_LOG_FILE_NAME}" \
    --action_env=PATH="$PATH" \
    --action_env=SYSTEM_DIGEST="$SYSTEM_DIGEST" \
    --host_action_env=PATH="$PATH" \
    --host_action_env=SYSTEM_DIGEST="$SYSTEM_DIGEST" \
    --experimental_ui_max_stdouterr_bytes=10000000 \
    --jobs=4 \
    "${BAZEL_REMOTE_CACHE_ARGUMENT}" \
    ${BAZEL_EXTRA_ARGS} \
    "${TARGET}"
