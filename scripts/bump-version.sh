#!/bin/sh
# Set the release version in root VERSION and every port manifest at once.
#
# All ports share one version, so that a given version denotes the
# same algorithm everywhere. Root VERSION feeds the C pkg-config package
# (make install / hayahash.pc). The release workflow refuses to publish
# when any manifest disagrees with the tag, and bumping them by hand
# is easy to get half-right; use this instead.
#
# Usage: scripts/bump-version.sh 0.2.0
set -eu

if [ $# -ne 1 ]; then
	echo "usage: $0 <version>" >&2
	exit 1
fi
version=$1

# Guard against a leading "v" or a partial version reaching a manifest.
case $version in
*[!0-9.]* | *..* | . | ..)
	echo "error: '$version' is not a bare X.Y.Z version" >&2
	exit 1
	;;
[0-9]*.[0-9]*.[0-9]*) ;;
*)
	echo "error: '$version' is not a bare X.Y.Z version" >&2
	exit 1
	;;
esac

cd "$(dirname "$0")/.."

# sed -i is not portable between BSD and GNU; write and move instead.
edit() {
	file=$1
	shift
	tmp="${file}.bump.tmp"
	sed "$@" "$file" >"$tmp"
	mv "$tmp" "$file"
}

# Root VERSION feeds the C pkg-config package (make install / hayahash.pc).
printf '%s\n' "$version" >VERSION

# Updates package.json and package-lock.json together.
(cd js && npm version "$version" --no-git-tag-version --allow-same-version >/dev/null)

# Only the package's own version sits at the start of a line; the
# dependency versions live inside inline tables.
edit rust/Cargo.toml -e "s/^version = \".*\"/version = \"${version}\"/"
# Keep Cargo.lock's record of the package version in step when a lockfile
# is present (it is gitignored, so fresh checkouts often have none).
if [ -f rust/Cargo.lock ]; then
	(cd rust && cargo update --offline --workspace >/dev/null)
fi

edit zig/build.zig.zon \
	-e "s/^\\([[:space:]]*\\)\\.version = \".*\",/\\1.version = \"${version}\",/"

# The first <version> element is the project's; later ones belong to
# dependencies and plugins.
edit java/pom.xml \
	-e "1,/<version>/s|<version>.*</version>|<version>${version}</version>|"

# Package Version is a single-line MSBuild property in the library project.
edit csharp/src/Hayahash/Hayahash.csproj \
	-e "s|<Version>.*</Version>|<Version>${version}</Version>|"

# PyPA project version plus the editable-install fallback in __init__.py.
edit python/pyproject.toml -e "s/^version = \".*\"/version = \"${version}\"/"
edit python/src/hayahash/__init__.py \
	-e "s/__version__ = \".*\"/__version__ = \"${version}\"/"

# SwiftPM has no package version field; the lockstep marker lives in a
# comment at the top of Package.swift.
edit swift/Package.swift \
	-e "s/^\/\/ hayahash-version: .*/\/\/ hayahash-version: ${version}/"

printf 'c/pc:   %s\n' "$(tr -d ' \n' < VERSION)"
printf 'js:     %s\n' "$(sed -n 's/.*"version": "\(.*\)".*/\1/p' js/package.json | head -n1)"
printf 'rust:   %s\n' "$(sed -n 's/^version = "\(.*\)"$/\1/p' rust/Cargo.toml)"
printf 'zig:    %s\n' "$(sed -n 's/^[[:space:]]*\.version = "\(.*\)",$/\1/p' zig/build.zig.zon)"
printf 'java:   %s\n' "$(sed -n 's:.*<version>\(.*\)</version>.*:\1:p' java/pom.xml | head -n1)"
printf 'csharp: %s\n' "$(sed -n 's:.*<Version>\(.*\)</Version>.*:\1:p' csharp/src/Hayahash/Hayahash.csproj | head -n1)"
printf 'python: %s\n' "$(sed -n 's/^version = "\(.*\)"$/\1/p' python/pyproject.toml)"
printf 'swift:  %s\n' "$(sed -n 's/^\/\/ hayahash-version: \(.*\)$/\1/p' swift/Package.swift)"
