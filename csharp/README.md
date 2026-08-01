# hayahash for .NET

Bit-exact C# port of [`hayahash.h`](../hayahash.h): a small, fast,
portable 64-bit non-cryptographic hash for targets with ordinary
wrapping 64×64→64 multiply.

```csharp
using Hayahash;

ulong h = Hayahash.Hash64(buf, seed);
ulong h2 = Hayahash.Hash64(buf, offset, length, seed);
```

Requires .NET 8 or later. Package version tracks the shared
algorithm version across every language port in this repository.

```sh
dotnet test
dotnet pack src/Hayahash/Hayahash.csproj -c Release
```

Release tags author-sign the `.nupkg` / `.snupkg` with a code-signing
certificate before pushing to nuget.org (`dotnet nuget sign`, DigiCert
timestamp). Register the matching public `.cer` on the nuget.org
account that owns the package.
