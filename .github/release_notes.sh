#!/bin/sh
# Prints a version's release notes: its CHANGELOG.md section, without the
# heading, then how to check a download. Usage: release_notes.sh v0.1.0
set -eu
ver="${1#v}"
section=$(awk -v v="$ver" '
  $0 ~ "^## \\[" v "\\]" { on = 1; next }
  on && /^## \[/ { exit }
  on { print }
' CHANGELOG.md)
[ -n "$section" ] || { echo "CHANGELOG.md has no [$ver] section" >&2; exit 1; }
printf '%s\n' "$section"
cat <<'NOTES'

### Checking your download

`SHA256SUMS` has a checksum for every file. On Linux, run
`sha256sum -c SHA256SUMS --ignore-missing` in the download folder (on macOS:
`shasum -a 256 -c SHA256SUMS --ignore-missing`). On Windows, compare
`Get-FileHash <file>` with that file's line.
NOTES
