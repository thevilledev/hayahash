# Agent notes

- [`CHANGELOG.md`](CHANGELOG.md) is the release history. Read it before
  digest-related work; update `[Unreleased]` when your change lands.
- Mark digest-breaking edits with `DIGEST` in the changelog entry. Those
  changes alter every output for the same input and seed.
- [`hayahash.h`](hayahash.h) is the authoritative reference. Ports must
  stay bit-exact with it.
- Root [`VERSION`](VERSION) is the C/pkg-config package version consumed by
  [`Makefile`](Makefile) / [`hayahash.pc.in`](hayahash.pc.in). Keep it in
  lockstep with every port manifest via
  [`scripts/bump-version.sh`](scripts/bump-version.sh); the release
  workflow refuses to publish when any of them disagree with the tag.
