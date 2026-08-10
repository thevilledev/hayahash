#!/bin/sh
# Functional tests for hayasum.
#
#   sh cli/tests/run.sh [HAYASUM] [REFHASH]
#
# Covers option parsing, exit statuses, diagnostics, output escaping and
# read/write error handling, and differential-tests the streaming reader
# against the one-shot oracle in refhash.c at every buffer boundary that
# matters. Output is TAP-ish: one line per check, plan last.
#
# POSIX shell only: this runs under dash in CI.

set -u

abspath() {
	case $1 in
	/*) printf '%s\n' "$1" ;;
	*) printf '%s\n' "$PWD/$1" ;;
	esac
}

here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
HAYASUM=$(abspath "${1:-$here/../hayasum}")
REFHASH=$(abspath "${2:-$here/refhash}")
ROOT=$(CDPATH='' cd -- "$here/../.." && pwd)

for prog in "$HAYASUM" "$REFHASH"; do
	if [ ! -x "$prog" ]; then
		printf 'run.sh: %s is not executable; build it first\n' \
			"$prog" >&2
		exit 2
	fi
done

tmp=$(mktemp -d "${TMPDIR:-/tmp}/hayasum-test.XXXXXX") || exit 2
trap 'cd /; chmod -R u+rwX "$tmp" 2>/dev/null; rm -rf "$tmp"' EXIT HUP INT TERM
cd "$tmp" || exit 2

tests=0
failures=0
status=0
stdout=
stderr=

ok() {
	tests=$((tests + 1))
	printf 'ok %d - %s\n' "$tests" "$1"
}

nope() {
	tests=$((tests + 1))
	failures=$((failures + 1))
	printf 'not ok %d - %s\n' "$tests" "$1"
	printf '#   %s\n' "$2"
}

skip() {
	tests=$((tests + 1))
	printf 'ok %d - %s # SKIP %s\n' "$tests" "$1" "$2"
}

is() { # is DESC EXPECTED ACTUAL
	if [ "$2" = "$3" ]; then
		ok "$1"
	else
		nope "$1" "expected [$2], got [$3]"
	fi
}

isnt() { # isnt DESC UNEXPECTED ACTUAL
	if [ "$2" != "$3" ]; then
		ok "$1"
	else
		nope "$1" "did not expect [$2]"
	fi
}

has() { # has DESC HAYSTACK NEEDLE
	case $2 in
	*"$3"*) ok "$1" ;;
	*) nope "$1" "[$2] does not contain [$3]" ;;
	esac
}

# Stdin defaults to /dev/null so a parsing bug cannot leave a test
# blocked on a terminal.
run() {
	status=0
	"$@" </dev/null >"$tmp/out" 2>"$tmp/err" || status=$?
	stdout=$(cat "$tmp/out")
	stderr=$(cat "$tmp/err")
}

run_in() { # run_in STDIN_FILE cmd...
	_in=$1
	shift
	status=0
	"$@" <"$_in" >"$tmp/out" 2>"$tmp/err" || status=$?
	stdout=$(cat "$tmp/out")
	stderr=$(cat "$tmp/err")
}

lines() { # lines FILE -> line count without padding
	wc -l <"$1" | tr -d ' \t'
}

printf '# hayasum functional tests\n'
printf '# binary: %s\n' "$HAYASUM"

# ------------------------------------------------------------------
# Known answers. These pin the v0.5 digest series through the CLI, so a
# digest change has to be acknowledged here too, not only in
# test_vectors/.
# ------------------------------------------------------------------

run_in /dev/null "$HAYASUM"
is "empty stdin, default width" "68ac507cf298ca3f  -" "$stdout"
is "empty stdin exits 0" 0 "$status"
is "empty stdin is quiet on stderr" "" "$stderr"

run_in /dev/null "$HAYASUM" -b 128
is "empty stdin, 128-bit" "ace2141f6ba3086868ac507cf298ca3f  -" "$stdout"

printf 'hello world' >hw
run "$HAYASUM" hw
is "hello world, file operand" "4524b96611bfc05a  hw" "$stdout"

run_in hw "$HAYASUM"
is "hello world, stdin" "4524b96611bfc05a  -" "$stdout"

# ------------------------------------------------------------------
# Help and version.
# ------------------------------------------------------------------

run "$HAYASUM" -h
is "-h exits 0" 0 "$status"
is "-h is quiet on stderr" "" "$stderr"
has "-h prints usage on stdout" "$stdout" "Usage: hayasum"

run "$HAYASUM" --help
has "--help prints usage on stdout" "$stdout" "Usage: hayasum"

version=$(tr -d ' \n' <"$ROOT/VERSION")
run "$HAYASUM" --version
is "--version reports the package version" "hayasum $version" "$stdout"
is "--version exits 0" 0 "$status"
run "$HAYASUM" -V
is "-V matches --version" "hayasum $version" "$stdout"

# ------------------------------------------------------------------
# Usage errors: exit 2, a diagnostic on stderr, nothing on stdout.
# ------------------------------------------------------------------

usage_error() { # usage_error DESC args...
	_desc=$1
	shift
	run "$HAYASUM" "$@"
	if [ "$status" -eq 2 ] && [ -z "$stdout" ] && [ -n "$stderr" ]; then
		ok "$_desc"
	else
		nope "$_desc" \
			"status=$status stdout=[$stdout] stderr=[$stderr]"
	fi
}

usage_error "unknown short option" -x
usage_error "unknown long option" --frobnicate
usage_error "--help rejects a value" --help=1
usage_error "--version rejects a value" --version=1
usage_error "-s with no value" -s
usage_error "-b with no value" -b
usage_error "--seed with no value" --seed
usage_error "--bits with no value" --bits
usage_error "negative seed" -s -1
usage_error "plus-signed seed" -s +1
usage_error "space-padded seed" -s " 1"
usage_error "empty seed" -s ""
usage_error "bare 0x seed" -s 0x
usage_error "0x with a sign" -s 0x-1
usage_error "trailing garbage in seed" -s 1x
usage_error "hex digits without the prefix" -s deadbeef
usage_error "seed above 2^64-1" -s 18446744073709551616
usage_error "absurdly long seed" -s 999999999999999999999999999999999999
usage_error "width 32" -b 32
usage_error "width 0" -b 0
usage_error "width as hex" -b 0x80
usage_error "width with trailing space" -b "64 "
usage_error "width 64.0" -b 64.0
usage_error "empty width" -b ""

# Diagnostics name the offending text and stay on one line.
run "$HAYASUM" -s zzz
has "seed diagnostic quotes the value" "$stderr" "invalid seed 'zzz'"
has "diagnostics point at --help" "$stderr" "Try 'hayasum --help'"
run "$HAYASUM" -b 32
has "width diagnostic quotes the value" "$stderr" "invalid width '32'"
run "$HAYASUM" -q
has "unknown short option is named" "$stderr" "unknown option '-q'"
run "$HAYASUM" --nope
has "unknown long option is named" "$stderr" "unknown option '--nope'"
run "$HAYASUM" -s
has "missing value is named" "$stderr" "option '-s' requires an argument"

# argv reaches a terminal through diagnostics, so control bytes in it
# must not: they render as '?' and cannot add lines.
run "$HAYASUM" -s "$(printf 'a\tb\rc')"
is "a bad value cannot inject lines" 2 "$(lines "$tmp/err")"
has "control bytes render as '?'" "$stderr" "invalid seed 'a?b?c'"

# A very long value is truncated rather than echoed whole.
long=$(awk 'BEGIN { while (i++ < 400) printf "A" }')
run "$HAYASUM" -s "$long"
is "an overlong value cannot inject lines" 2 "$(lines "$tmp/err")"
if [ "$(wc -c <"$tmp/err" | tr -d ' \t')" -lt 300 ]; then
	ok "an overlong value is truncated in the diagnostic"
else
	nope "an overlong value is truncated in the diagnostic" \
		"stderr is $(wc -c <"$tmp/err") bytes"
fi

# ------------------------------------------------------------------
# Accepted option spellings all mean the same thing.
# ------------------------------------------------------------------

run "$HAYASUM" hw
plain=$stdout
run "$HAYASUM" -s 0 hw
is "-s 0 is the default" "$plain" "$stdout"
run "$HAYASUM" -s0 hw
is "an attached short value works" "$plain" "$stdout"
run "$HAYASUM" --seed 0 hw
is "--seed with a separate value works" "$plain" "$stdout"
run "$HAYASUM" --seed=0 hw
is "--seed= works" "$plain" "$stdout"
run "$HAYASUM" -b 64 hw
is "-b 64 is the default" "$plain" "$stdout"

run "$HAYASUM" -s 0x9E3779B97F4A7C15 hw
golden=$stdout
run "$HAYASUM" -s 11400714819323198485 hw
is "hex and decimal seeds agree" "$golden" "$stdout"
run "$HAYASUM" -s 0X9e3779b97f4a7c15 hw
is "0X and case-insensitive hex agree" "$golden" "$stdout"
isnt "a seed actually changes the digest" "$plain" "$golden"

run "$HAYASUM" -s 010 hw
seed10=$stdout
run "$HAYASUM" -s 10 hw
is "a leading zero is not octal" "$seed10" "$stdout"

run "$HAYASUM" -s 18446744073709551615 hw
maxdec=$stdout
run "$HAYASUM" -s 0xFFFFFFFFFFFFFFFF hw
is "the largest seed round-trips" "$maxdec" "$stdout"

run "$HAYASUM" -b 128 hw
wide=$stdout
run "$HAYASUM" -b128 hw
is "an attached width works" "$wide" "$stdout"
run "$HAYASUM" --bits=128 hw
is "--bits= works" "$wide" "$stdout"
run "$HAYASUM" -b 64 -b 128 hw
is "the last width wins" "$wide" "$stdout"
run "$HAYASUM" -b 128 -b 64 hw
is "the last width wins the other way" "$plain" "$stdout"
run "$HAYASUM" -s 1 -s 0 hw
is "the last seed wins" "$plain" "$stdout"

# -h and -V end the run and -s/-b swallow the rest of the argument, so
# only the first short option in a bundle is ever acted on.
run "$HAYASUM" -hV
has "only the first short option acts" "$stdout" "Usage: hayasum"
run "$HAYASUM" -Vh
is "and the other order picks the other one" "hayasum $version" "$stdout"
run "$HAYASUM" -sh hw
is "-s swallows the rest of the argument" 2 "$status"
has "and reports it as the seed" "$stderr" "invalid seed 'h'"

# ------------------------------------------------------------------
# Operands, "--", and POSIX option ordering.
# ------------------------------------------------------------------

printf 'x' >plain-x
run "$HAYASUM" plain-x
xdigest=$(awk '{print $1}' "$tmp/out")

printf 'x' >./-b
run "$HAYASUM" -- -b
is "-- lets a file named -b be hashed" 0 "$status"
is "and it hashes the file" "$xdigest  -b" "$stdout"

run "$HAYASUM" -b
is "without -- it is still an option" 2 "$status"

run_in hw "$HAYASUM" -- -
is "'-' after -- is still stdin" "4524b96611bfc05a  -" "$stdout"

run "$HAYASUM" hw --bits 128
is "options after the first operand are operands" 1 "$status"
is "the real operand is still hashed" 1 "$(lines "$tmp/out")"
is "and the rest are reported missing" 2 "$(lines "$tmp/err")"

run_in hw "$HAYASUM" - -
is "a repeated '-' produces a line each" 2 "$(lines "$tmp/out")"
is "the first read consumes the stream" "4524b96611bfc05a  -" \
	"$(sed -n 1p "$tmp/out")"
is "the second sees end of file" "68ac507cf298ca3f  -" \
	"$(sed -n 2p "$tmp/out")"

# ------------------------------------------------------------------
# Read errors.
# ------------------------------------------------------------------

run "$HAYASUM" no-such-file
is "a missing file exits 1" 1 "$status"
is "and prints no digest" "" "$stdout"
has "and says which file" "$stderr" "no-such-file"

mkdir adir
run "$HAYASUM" adir
is "a directory exits 1" 1 "$status"
is "and prints no digest" "" "$stdout"
has "and says which one" "$stderr" "adir"

run "$HAYASUM" hw no-such-file hw
is "one bad operand among good ones exits 1" 1 "$status"
is "and the good ones are still hashed" 2 "$(lines "$tmp/out")"

if [ "$(id -u)" -ne 0 ]; then
	printf 'x' >locked
	chmod 000 locked
	run "$HAYASUM" locked
	is "an unreadable file exits 1" 1 "$status"
	is "and prints no digest" "" "$stdout"
	chmod 644 locked
else
	skip "an unreadable file exits 1" "running as root"
	skip "and prints no digest" "running as root"
fi

# ------------------------------------------------------------------
# Write errors. A checksum that never reaches stdout must not be
# reported as success.
# ------------------------------------------------------------------

if [ -w /dev/full ]; then
	status=0
	"$HAYASUM" hw >/dev/full 2>"$tmp/err" || status=$?
	is "a failed write exits 1" 1 "$status"
	has "and names standard output" "$(cat "$tmp/err")" "standard output"
else
	skip "a failed write exits 1" "/dev/full not available"
	skip "and names standard output" "/dev/full not available"
fi

status=0
"$HAYASUM" hw >&- 2>"$tmp/err" || status=$?
is "a closed stdout exits 1" 1 "$status"

# ------------------------------------------------------------------
# Output escaping: one input, one line, always.
# ------------------------------------------------------------------

nl=$(printf 'a\nb')
printf 'x' >"$nl"
run "$HAYASUM" -- "$nl"
is "a newline in a name still yields one line" 1 "$(lines "$tmp/out")"
is "the name is escaped and the line flagged" "\\$xdigest  a\\nb" "$stdout"

cr=$(printf 'a\rb')
printf 'x' >"$cr"
run "$HAYASUM" -- "$cr"
is "a carriage return is escaped too" "\\$xdigest  a\\rb" "$stdout"

printf 'x' >'a\b'
run "$HAYASUM" -- 'a\b'
is "a backslash is doubled" "\\$xdigest  a\\\\b" "$stdout"

printf 'x' >'a b'
run "$HAYASUM" -- 'a b'
is "a space needs no escaping" "$xdigest  a b" "$stdout"

run "$HAYASUM" -- "$(printf 'gone\nx')"
is "a diagnostic cannot be split by a name either" 1 "$(lines "$tmp/err")"
has "and the name is escaped there too" "$stderr" "gone\\nx"

# ------------------------------------------------------------------
# Differential: the streaming reader against the one-shot oracle, at
# every size where the buffering behaviour changes, plus the documented
# hayahash128.lo == hayahash64 relationship.
# ------------------------------------------------------------------

dd if=/dev/urandom of=big bs=1000 count=200 2>/dev/null
if [ "$(wc -c <big | tr -d ' \t')" -ne 200000 ]; then
	skip "streaming reader matches the one-shot oracle" \
		"could not build a random corpus"
else
	sizes="0 1 2 3 7 8 15 16 17 31 32 63 64 65 127 128 129 191 192 255
	       256 319 320 321 383 384 447 448 449 511 512 575 576 1023 1024
	       4095 4096 8192 65535 65536 65537 131073"
	for n in $sizes; do
		if [ "$n" -eq 0 ]; then
			: >data
		else
			dd if=big of=data bs="$n" count=1 2>/dev/null
		fi
		bad=
		for b in 64 128; do
			for s in 0 0x9E3779B97F4A7C15; do
				want=$("$REFHASH" -b "$b" -s "$s" <data |
					awk '{print $1}')
				got=$("$HAYASUM" -b "$b" -s "$s" -- data |
					awk '{print $1}')
				pipe=$("$HAYASUM" -b "$b" -s "$s" <data |
					awk '{print $1}')
				[ "$got" = "$want" ] ||
					bad="$bad file/b$b/s$s($got!=$want)"
				[ "$pipe" = "$want" ] ||
					bad="$bad pipe/b$b/s$s"
			done
		done
		h64=$("$HAYASUM" -b 64 -- data | awk '{print $1}')
		h128=$("$HAYASUM" -b 128 -- data | awk '{print $1}')
		[ "${h128#????????????????}" = "$h64" ] ||
			bad="$bad lo!=h64"
		if [ -z "$bad" ]; then
			ok "$n bytes: reader matches the one-shot oracle"
		else
			nope "$n bytes: reader matches the one-shot oracle" \
				"$bad"
		fi
	done
fi

# ------------------------------------------------------------------

printf '1..%d\n' "$tests"
if [ "$failures" -ne 0 ]; then
	printf '# %d of %d checks failed\n' "$failures" "$tests"
	exit 1
fi
printf '# all %d checks passed\n' "$tests"
