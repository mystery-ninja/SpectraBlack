#!/usr/bin/env bash
#
# Architecture guard.
#
# core/ is the platform-agnostic heart of the app: scene graph, properties,
# keyframes, drivers, evaluator. It must not depend on the GPU, the OS, the UI,
# or file I/O. That constraint is what makes three platforms affordable and lets
# the core be unit-tested without a window or a GL context.
#
# Avee Player did not maintain this separation -- its elements call GLES20
# directly and take android.graphics types in their public API, which is exactly
# why it is Android-only and why none of its render code can be tested.
# See docs/01-ARCHITECTURE.md and docs/09-AVEE-TEARDOWN.md.
#
# This script fails the build if that boundary is crossed. Discipline alone does
# not survive contact with a deadline; CI does.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CORE="$ROOT/core"

if [[ ! -d "$CORE" ]]; then
    echo "error: core/ not found at $CORE" >&2
    exit 2
fi

# Headers core/ is forbidden to include, as extended regexes matched against
# the text between the angle brackets or quotes of an #include.
FORBIDDEN=(
    # Graphics
    'GL/.*'  'GLES[0-9]*/.*'  'glad/.*'  'GLFW/.*'  'vulkan/.*'  'EGL/.*'
    # UI
    'imgui.*'  'QtCore/.*'  'QtWidgets/.*'
    # OS
    'windows\.h'  'Windows\.h'  'unistd\.h'  'sys/.*'  'android/.*'  'jni\.h'
    'dlfcn\.h'  'pthread\.h'
    # I/O -- core is handed data, it does not go and fetch it
    'fstream'  'filesystem'  'cstdio'  'iostream'
    # Media
    'libav.*'  'miniaudio.*'  'ft2build\.h'
)

PATTERN="$(IFS='|'; echo "${FORBIDDEN[*]}")"

mapfile -t FILES < <(find "$CORE" -type f \( -name '*.hpp' -o -name '*.h' -o -name '*.cpp' \) \
    -not -path '*/tests/*' | sort)

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "check-core-deps: no source files in core/ yet, nothing to check"
    exit 0
fi

violations=0

for file in "${FILES[@]}"; do
    while IFS= read -r hit; do
        [[ -z "$hit" ]] && continue
        lineno="${hit%%:*}"
        text="${hit#*:}"
        printf '%s:%s: forbidden include in core/ -> %s\n' \
            "${file#"$ROOT"/}" "$lineno" "$(echo "$text" | tr -d '[:space:]')" >&2
        violations=$((violations + 1))
    done < <(grep -nE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[<\"](${PATTERN})[>\"]" "$file" 2>/dev/null)
done

echo "check-core-deps: scanned ${#FILES[@]} file(s) in core/"

if [[ $violations -gt 0 ]]; then
    cat >&2 <<'EOF'

core/ must not depend on the GPU, the OS, the UI, or file I/O.

If you need one of these, the work belongs in another module:
  - rendering .............. gpu/ or render/
  - OS, files, threads ..... platform/
  - audio decode/playback .. audio/
  - editor UI .............. app-desktop/

core/ receives data and computes with it. It does not go and fetch it.
See docs/01-ARCHITECTURE.md.
EOF
    exit 1
fi

echo "check-core-deps: OK"
