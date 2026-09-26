# Retrom KiriKiri fork maintenance rules

This fork builds the KiriKiri browser core consumed by
`retrom-project/retrom-runtime`. It must remain independent of Retrom application APIs,
databases, review workflows, credentials, and private game content.

The repository's reverse-engineering and implementation rules remain in
`CLAUDE.md`. Read that file completely before changing `cpp/`; these fork and
release rules supplement rather than replace it.

## Repository identity

- `web` is an unmodified, fast-forward-only mirror of `upstream/web`.
- `retrom/g13dda190f837` is the only active Retrom maintenance baseline and the
  repository default branch. Retrom changes and release tags originate there,
  never from `web`.
- `upstream` must point to `https://github.com/fenghengzhi/kirikiroid2-web.git`.
- `retrom-fork.json` is the machine-readable baseline and release contract.
  Never replace its upstream commit with a floating branch.
- Updating `web` may only fast-forward it to `upstream/web`. A new fixed
  baseline requires a reviewed `sync/upstream-<commit>` branch and a new
  `retrom/g<12-hex-commit>` maintenance branch.

## Branches and commits

- Use short-lived `fix/*`, `feat/*`, `build/*`, or `sync/upstream-<baseline>`
  branches created from `retrom/g13dda190f837`.
- Branch names use lowercase ASCII and hyphens. Do not create `temp`, `clean`,
  `final`, `runtime-clean`, parallel maintenance branches, or branches named
  after an agent or user.
- Keep downstream changes as small reviewable commits and squash or rebase PRs
  onto the maintenance branch; release ancestry must not contain merge commits
  after the fixed upstream baseline.
- Never force-push, move immutable tags, or delete another contributor's work.

## Quality and releases

- Before pushing, run `python3 .github/rpg-runtime/verify-source.py`.
- Changes that affect Web output must also run
  `.github/rpg-runtime/build-web.sh <empty-output-directory>` followed by
  `.github/rpg-runtime/verify-release.py` with a valid candidate identity.
- PRs to `retrom/g13dda190f837` must pass
  `.github/workflows/rpg-runtime-quality.yml`.
- Release tags are `retrom-core-g13dda190f837-rN`, with optional `-rc.N` only
  for integration candidates. Increment `rN` for any source, build, asset, or
  adapter-contract change on this baseline.
- Existing `rpg-runtime-*` tags are immutable historical records. Never create
  another tag in that retired namespace.
- Tags are annotated and immutable. The tag workflow is the only supported way
  to build and publish `index.js`, `index.wasm`, `vlfs.js`, `assets.zip`,
  `LICENSE`, and `rpg-runtime-release.json`; never publish aliases such as
  `latest` or `stable`.
- Repository, tag, tag commit, asset filename, and adapter ABI define release
  identity. Observed SHA-256 values are cache-integrity diagnostics only.

Do not add Retrom host-product logic, games, credentials, or private test data.
