# hayahash for Swift

Bit-exact Swift port of [`hayahash.h`](../hayahash.h): small, fast,
portable 64- and 128-bit non-cryptographic hashes for targets with ordinary
wrapping 64×64→64 multiply.

```swift
import Hayahash

let h = Hayahash.hash64(buf, seed: 0)
let h2 = Hayahash.hash64(buf, offset: 0, length: buf.count, seed: 0)
let h128 = Hayahash.hash128(buf, seed: 0)
```

Requires Swift 5.9+. Package version tracks the shared algorithm version
across every language port in this repository (`// hayahash-version` in
`Package.swift`).

```sh
swift test
```

Because the package lives in a monorepo subdirectory, consume it as a
local path dependency or via the `hayahash-swift-*.tar.gz` asset attached
to each GitHub release (that archive has `Package.swift` at its root).
