# Retrom CI builds

`RPG Runtime Quality` keeps the required `quality` status and runs source/native
regressions, the reusable Web build, and a download/verification of the candidate
artifact. All three must succeed. `RPG Runtime Release` validates the annotated
tag, calls the same build, then downloads and publishes the validated assets in a
separate job. Only the publishing job has `contents: write`. A failed publish can
be retried with **Re-run failed jobs** without repeating a successful Web build,
while the build artifact remains available (seven days).

The tag workflow still builds official releases. This does not promote a PR
artifact or assume a PR merge SHA equals the final squash SHA. Release assets,
tag naming, baseline, adapter ABI, optimization flags and metadata schema are
unchanged. A new fork release still requires the next immutable `rN` tag.

## Caches

- SDK and generated Emscripten ports are cached together, keyed by the fixed
  toolchain definitions and explicit ports list.
- vcpkg's binary archives are cached, not `vcpkg_installed`, packages or buildtrees.
  vcpkg still computes package ABIs and restores or rebuilds each dependency.
  The cache key includes toolchain pins, manifests, overlay ports/triplets, the
  build script and CMake presets.
- ccache is installed explicitly and uses the existing CMake compiler launchers.
  Its key adds the source SHA to the dependency configuration; a compatible
  prefix restores previous compiler results when the source changes. Linking
  still runs, and no correctness checks or required regressions are disabled.

GitHub cache scopes matter: PR merge caches cannot be read by tags or the default
branch, and caches from different tags cannot be shared directly. Quality also
runs on default-branch changes to CI/toolchain/dependency inputs to seed caches.
For the initial seed or after eviction, manually dispatch Quality on
`retrom/g13dda190f837` and wait for it to finish before tagging. A dispatch on a
feature branch only seeds that branch. Cache misses always build normally.

## Measure cold and warm builds

The existing Quality workflow supports manual runs on a feature branch:

```bash
gh workflow run rpg-runtime-quality.yml --repo retrom-project/kirikiroid2-web \
  --ref <feature-branch> -f cold-cache=true -f build-jobs=4
# Wait for success and cache upload, then run the same source again:
gh workflow run rpg-runtime-quality.yml --repo retrom-project/kirikiroid2-web \
  --ref <feature-branch> -f cold-cache=false -f build-jobs=4
```

`cold-cache` bypasses restore without deleting any shared cache. A successful
build saves missing entries for the next run. Use `build-jobs=2` for a controlled
comparison with the previous parallelism; CI defaults to four, while local
`build-web.sh` retains its existing default of two.

The build summary reports SDK/tool preparation, ports, CMake/vcpkg dependencies,
core compilation/linking, and ccache statistics. The diagnostics artifact also
contains the full build log, phase timings, vcpkg log and `/usr/bin/time` memory
statistics. Inspect both Actions cache hits and vcpkg's restored package count:
restoring an archive alone does not prove its package ABIs matched.

Downloaded assets are checked against the original release manifest before
handoff. `verify-release.py --check-existing` fails on changed bytes or mismatched
repository/tag/commit/ABI and leaves the manifest intact. File hashes remain
transfer/cache-integrity evidence, not an alternative release identity.
