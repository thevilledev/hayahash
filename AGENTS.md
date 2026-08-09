# Agent notes

- [`CHANGELOG.md`](CHANGELOG.md) is the release history. Read it before
  digest-related work; update `[Unreleased]` when your change lands.
- Mark digest-breaking edits with `DIGEST` in the changelog entry. Those
  changes alter every output for the same input and seed.
- [`hayahash.h`](hayahash.h) is the authoritative reference. Ports must
  stay bit-exact with it.
