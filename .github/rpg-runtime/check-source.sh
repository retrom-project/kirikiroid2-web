#!/usr/bin/env bash
set -euo pipefail

python3 .github/rpg-runtime/verify-source.py
python3 -m unittest discover -s tools/xp3 -p 'test_*.py'
python3 .github/rpg-runtime/test-bookmark-bridge.py
python3 .github/rpg-runtime/test-game-save-observer.py
python3 .github/rpg-runtime/test-native-exit.py
node tests/web/host_game_save_notification.test.mjs
python3 .github/rpg-runtime/test-display-monitors.py
python3 .github/rpg-runtime/test-window-placement.py
python3 .github/rpg-runtime/test-font-candidates.py
python3 .github/rpg-runtime/test-font-discovery.py
python3 .github/rpg-runtime/test-font-baseline.py
python3 .github/rpg-runtime/test-font-cell-height.py
python3 .github/rpg-runtime/test-window-presentation.py
python3 .github/rpg-runtime/test-sqlite.py
node tests/web/sqlite_vlfs_publish.test.mjs
python3 .github/rpg-runtime/test-pthread-futex.py
node tests/web/pthread_jspi.test.mjs
node tests/web/jspi_mainloop_scheduler.test.mjs
node .github/rpg-runtime/test-vlfs.mjs
python3 .github/rpg-runtime/test-release-assets.py
