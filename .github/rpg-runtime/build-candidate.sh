#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
output=${1:?absolute empty output directory is required}
python3 "$root/.github/rpg-runtime/candidate_descriptor.py" prepare "$output"
"$root/.github/rpg-runtime/build-web.sh" "$output"
commit=$(git -C "$root" rev-parse HEAD)
python3 "$root/.github/rpg-runtime/verify-release.py" --output "$output" \
  --repository https://github.com/retrom-project/kirikiroid2-web \
  --tag retrom-core-g338d2029f169-r999999 --commit "$commit"
rm "$output/rpg-runtime-release.json"
python3 "$root/.github/rpg-runtime/candidate_descriptor.py" finalize "$output" --core-id kirikiri2
