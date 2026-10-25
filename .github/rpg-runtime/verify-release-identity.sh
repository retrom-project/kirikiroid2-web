#!/usr/bin/env bash
set -euo pipefail
[[ "$GITHUB_REF_NAME" =~ ^retrom-core-g13dda190f837-r[1-9][0-9]*$ ]]
git fetch --force origin \
  "refs/tags/$GITHUB_REF_NAME:refs/tags/$GITHUB_REF_NAME"
test "$(git cat-file -t "refs/tags/$GITHUB_REF_NAME")" = tag
test "$(git rev-list -n 1 "refs/tags/$GITHUB_REF_NAME")" = "$GITHUB_SHA"
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
git fetch --no-tags origin \
  '+refs/heads/retrom/g13dda190f837:refs/remotes/origin/retrom/g13dda190f837'
git merge-base --is-ancestor "$GITHUB_SHA" origin/retrom/g13dda190f837
git merge-base --is-ancestor 13dda190f8370d02b6cf59286a088529b355658c "$GITHUB_SHA"
test -z "$(git rev-list --min-parents=2 13dda190f8370d02b6cf59286a088529b355658c.."$GITHUB_SHA")"
python3 .github/rpg-runtime/verify-source.py
python3 .github/rpg-runtime/test-bookmark-bridge.py
node tests/web/jspi_mainloop_scheduler.test.mjs
