#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
archive="$project_dir/.sdk/SDK-B288-6.8.7z"
mkdir -p "$project_dir/.sdk"
if [[ ! -f "$archive" ]] || [[ "$(wc -c < "$archive")" -lt 1301230510 ]]; then
    curl --http1.1 --fail --location --continue-at - --retry 2 \
        --output "$archive" \
        https://github.com/pocketbook/SDK_6.3.0/releases/download/6.8/SDK-B288-6.8.7z
fi
python3 "$project_dir/scripts/prepare-sdk.py" --sdk-root "$project_dir/.sdk/SDK-B288" --archive "$archive"
