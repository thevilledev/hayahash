# Website deployment

The static site deploys from `website/` through `.github/workflows/static.yml`.
The workflow builds the browser playground before it uploads the GitHub Pages
artifact.

## Playground assets

`scripts/build-playground.sh` puts every mutually dependent playground file in
one content-versioned directory:

```text
website/
  playground-assets.json
  assets/<content-hash>/
    playground.js
    bench.wasm
    vendor/
      index.js
      pure.js
      wasm.js
      wasm-module.js
      kat.txt
```

The HTML always revalidates `playground-assets.json`, then dynamically imports
the entry module named there. Module imports and the wasm/KAT fetches resolve
relative to that entry module, so one page load cannot mix files from different
builds. Files below `assets/<content-hash>/` are immutable and must never be
purged or overwritten.

The unversioned `bench.wasm` and `vendor/` files are temporary compatibility
aliases for browsers that cached the HTML shell from before content versioning.

## Cloudflare cache rules

Create these rules for `hayaha.sh`.

1. Immutable assets

   Expression:

   ```text
   http.host eq "hayaha.sh" and starts_with(http.request.uri.path, "/assets/")
   ```

   Set cache eligibility to **Eligible for cache**, Edge TTL to one year, and
   Browser TTL to one year. Add a Response Header Transform Rule that sets:

   ```text
   Cache-Control: public, max-age=31536000, immutable
   ```

2. Mutable entry points

   Expression:

   ```text
   http.host eq "hayaha.sh" and http.request.uri.path in {"/playground.html" "/playground-assets.json"}
   ```

   Set cache eligibility to **Bypass cache**. Add a Response Header Transform
   Rule that sets:

   ```text
   Cache-Control: no-cache
   ```

Cache Rules control Cloudflare's edge cache. Response Header Transform Rules
set the policy seen by browsers; changing `Cache-Control` in a transform rule
does not retroactively change Cloudflare's own cache decision.

## Purge credentials

The deploy job purges only the mutable entry points and compatibility aliases
after GitHub Pages reports a successful deployment. Configure:

- repository variable `CLOUDFLARE_ZONE_ID`
- repository secret `CLOUDFLARE_API_TOKEN`

Scope the API token to **Zone / Cache Purge / Purge** for the `hayaha.sh` zone.
The workflow fails after deployment if either value is missing or Cloudflare
rejects the purge. It deliberately does not use `purge_everything`.
