// swift-tools-version: 5.9
// hayahash-version: 0.4.3
//
// The hayahash-version line is the lockstep algorithm version shared with
// every other language port; scripts/bump-version.sh and the release
// workflow keep it in sync with the git tag.
import PackageDescription

let package = Package(
    name: "Hayahash",
    products: [
        .library(name: "Hayahash", targets: ["Hayahash"]),
    ],
    targets: [
        .target(name: "Hayahash"),
        .testTarget(name: "HayahashTests", dependencies: ["Hayahash"]),
    ]
)
