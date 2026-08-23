# hayahash for Haskell

Bit-exact pure Haskell port of [`hayahash.h`](../hayahash.h): small, fast,
portable 64- and 128-bit non-cryptographic hashes over strict `ByteString`
values.

```haskell
import qualified Data.ByteString.Char8 as BS
import Data.Hash.Hayahash

main :: IO ()
main = do
  let input = BS.pack "hello world"
  print (hayahash64 input 0)
  print (hayahash128 input 0)
```

The incremental API is immutable. Digesting does not consume a state, and
retaining an old state naturally forks a common prefix:

```haskell
let prefix = update (newHasher 7) (BS.pack "hello ")
    complete = update prefix (BS.pack "world")
in digest64 complete
```

The incremental implementation retains at most 447 input bytes before the
bulk path and 128--191 bytes afterwards. It produces the same result as a
one-shot hash over the concatenation of every update, for any split.

Requires GHC 8.10 or newer. Build and run the conformance tests with:

```sh
cabal test
```

The package version tracks the shared algorithm version across every port in
this repository.
