# hayahash for .NET

Bit-exact C# port of [`hayahash.h`](../hayahash.h): small, fast,
portable 64- and 128-bit non-cryptographic hashes for targets with ordinary
wrapping 64×64→64 multiply.

```csharp
using Hayahash;

ulong h = Hayahash.Hash64(buf, seed);
ulong h2 = Hayahash.Hash64(buf, offset, length, seed);
Digest128 h128 = Hayahash.Hash128(buf, seed);
```

Requires .NET 8 or later. Package version tracks the shared
algorithm version across every language port in this repository.

```sh
dotnet test
dotnet pack src/Hayahash/Hayahash.csproj -c Release
```
