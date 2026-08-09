# Security Policy

## Threat model

hayahash is a **non-cryptographic** hash family. It is intended for
hash tables, checksums, fingerprinting, and similar integrity checks
against accidental corruption or non-adversarial collisions.

It is **not**:

- a cryptographic hash (not collision-resistant under adversarial choice)
- a message authentication code (a secret seed does not make it a MAC)
- a password hash or key-derivation function

Do not use hayahash where an attacker chooses inputs or can influence
seeds in order to cause collisions, preimages, or authentication bypass.

## Seed secrecy and HashDoS

When hayahash is used as a hash-table mixer on untrusted keys, treat the
seed as a secret process-local value (as you would for SipHash-style
table hashing). A public or fixed seed lets an attacker craft colliding
keys and degrade table performance (hash flooding).

A secret seed reduces HashDoS risk; it does **not** provide
cryptographic authenticity or collision resistance.

## Quality and implementation issues

Report the following privately when disclosure could help an attacker
before a fix lands:

- digest or streaming mismatches against the C reference that could
  silently diverge ports
- undefined behavior or memory unsafety in the reference header or a
  maintained port
- systematic collision classes that defeat the documented quality goals
  for non-adversarial use

Ordinary SMHasher3 failures, benchmark claims, docs typos, and public
API questions belong in ordinary GitHub issues.

## Supported versions

Fixes land on the latest release first. Pre-1.0 digests may still change
for quality or API reasons; see [`docs/stability.md`](docs/stability.md).

## Reporting a vulnerability

Please report security-sensitive issues privately via GitHub Security
Advisories:

https://github.com/thevilledev/hayahash/security/advisories/new

Do not open a public issue for vulnerabilities until a fix or coordinated
disclosure is ready. This project is maintained on a reasonable-effort
basis; allow time to investigate and patch before public exposure.
