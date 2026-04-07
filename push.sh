#!/usr/bin/env bash
# push.sh — git push wrapper with optional release tagging
#
# Usage:
#   ./push.sh              plain push, no release
#   ./push.sh +1           bump patch  (1.0.0 → 1.0.1)
#   ./push.sh +10          bump minor  (1.4.2 → 1.5.2)
#   ./push.sh +100         bump major  (1.4.2 → 2.4.2)
#   ./push.sh 2.1.0        explicit version

set -euo pipefail

if [ $# -eq 0 ]; then
  git push
  exit
fi

ARG="$1"

CONF="gooey/src-tauri/tauri.conf.json"
CURRENT=$(python3 -c "import json; print(json.load(open('$CONF'))['version'])")
IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT"

case "$ARG" in
  +1)
    PATCH=$((PATCH + 1))
    ;;
  +10)
    MINOR=$((MINOR + 1))
    ;;
  +100)
    MAJOR=$((MAJOR + 1))
    ;;
  [0-9]*)
    IFS='.' read -r MAJOR MINOR PATCH <<< "$ARG"
    ;;
  *)
    echo "error: unrecognised flag '$ARG'" >&2
    echo "usage: $0 [+1 | +10 | +100 | X.Y.Z]" >&2
    exit 1
    ;;
esac

NEW="${MAJOR}.${MINOR}.${PATCH}"
TAG="v${NEW}"

echo "→ ${CURRENT}  →  ${NEW}  (tag: ${TAG})"

python3 - <<PYEOF
import json, re, pathlib

p = pathlib.Path('$CONF')
d = json.loads(p.read_text())
d['version'] = '$NEW'
p.write_text(json.dumps(d, indent=2) + '\n')

p = pathlib.Path('gooey/pyproject.toml')
t = re.sub(r'^(version\s*=\s*")[^"]+(")', r'\g<1>${NEW}\2', p.read_text(), flags=re.MULTILINE)
p.write_text(t)

p = pathlib.Path('gooey/package.json')
d = json.loads(p.read_text())
d['version'] = '$NEW'
p.write_text(json.dumps(d, indent=2) + '\n')
PYEOF

git add "$CONF" gooey/pyproject.toml gooey/package.json
git commit -m "chore: release ${NEW}"
git tag "$TAG"
git push
git push origin "$TAG"

if command -v gh &>/dev/null; then
  echo "triggering docs build..."
  gh workflow run build-docs.yml --ref "$TAG"
else
  echo "note: gh CLI not found — trigger docs build manually or install gh"
fi

echo "✓ released ${TAG}"
