#!/bin/sh
# fsnap test suite.
#
# Black-box: everything is driven through the built binary, because that is the
# only interface the tool actually has. Output is TAP-ish; the exit status is 0
# only when every test passed.
#
# POSIX sh, no bashisms: SPEC.md section 19 targets NetBSD as well as Linux.
#
#   make test
#   FSNAP=/path/to/fsnap sh tests/run.sh

set -u

FSNAP=${FSNAP:-build/fsnap}
case $FSNAP in
/*) ;;
*) FSNAP=$(pwd)/$FSNAP ;;
esac

if [ ! -x "$FSNAP" ]; then
	echo "no fsnap binary at $FSNAP (run make first)" >&2
	exit 2
fi

# A bug in the walker can make create block forever rather than fail -- opening
# a FIFO waits for a writer that never comes. Cap every invocation so that turns
# into a failing test instead of a hung suite. timeout(1) is not in POSIX, so
# fall back to running unguarded where it is missing.
if command -v timeout >/dev/null 2>&1; then
	TIMEOUT="timeout 30"
else
	TIMEOUT=""
fi

fsnap() {
	# shellcheck disable=SC2086 # deliberate split: TIMEOUT is a command prefix
	$TIMEOUT "$FSNAP" "$@"
}

testno=0
failed=0
skipped=0

ok() {
	testno=$((testno + 1))
	printf 'ok %d - %s\n' "$testno" "$1"
}

notok() {
	testno=$((testno + 1))
	failed=$((failed + 1))
	printf 'not ok %d - %s\n' "$testno" "$1"
	shift
	for line in "$@"; do printf '#   %s\n' "$line"; done
}

skip() {
	testno=$((testno + 1))
	skipped=$((skipped + 1))
	printf 'ok %d - # SKIP %s\n' "$testno" "$1"
}

# is <description> <actual> <expected>
is() {
	if [ "$2" = "$3" ]; then
		ok "$1"
	else
		notok "$1" "got:  $2" "want: $3"
	fi
}

# isnt <description> <actual> <unwanted>
isnt() {
	if [ "$2" != "$3" ]; then
		ok "$1"
	else
		notok "$1" "got the unwanted value: $2"
	fi
}

# contains <description> <haystack> <needle>
contains() {
	case $2 in
	*"$3"*) ok "$1" ;;
	*) notok "$1" "output:   $2" "expected: something containing $3" ;;
	esac
}

# Counting `find` output with wc -l overcounts as soon as a filename contains a
# newline, which is legal and which this suite deliberately creates. Print one
# fixed line per entry instead.
count_entries() {
	find "$1" -mindepth 1 -exec printf 'x\n' \; | wc -l | tr -d ' '
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/fsnap-test.XXXXXX") || exit 2
trap 'rm -rf "$WORK"' EXIT INT TERM
cd "$WORK" || exit 2

# --------------------------------------------------------------------------
# Fixture, covering the tree shapes SPEC.md section 22 asks for.
# --------------------------------------------------------------------------
mkdir -p tree/sub/deeper tree/empty-dir
printf 'hello\n' >tree/file.txt          # crc32 909783072
: >tree/empty-file                       # crc32 0
printf 'nested\n' >tree/sub/nested.txt
ln tree/file.txt tree/hardlink.txt
ln -s file.txt tree/link.txt
ln -s missing.txt tree/broken-link
ln -s sub tree/link-to-dir
ln -s /absolute/somewhere tree/abs-link
mkfifo tree/pipe 2>/dev/null || rm -f tree/pipe

have_socket=no
if command -v python3 >/dev/null 2>&1; then
	if python3 -c "
import socket
s = socket.socket(socket.AF_UNIX)
s.bind('$WORK/tree/sock')
" 2>/dev/null; then
		have_socket=yes
	fi
fi

mkdir empty-tree

# --------------------------------------------------------------------------
# scan
# --------------------------------------------------------------------------
out=$(fsnap scan tree 2>/dev/null | sort)
is "scan lists every entry once" \
	"$(printf '%s\n' "$out" | wc -l | tr -d ' ')" \
	"$(count_entries tree)"

isnt "scan does not emit . or .." "$(printf '%s\n' "$out" | grep -c '/\.\.\?\]$')" "1"

contains "scan descends into a real directory" "$out" "/tree/sub/deeper]"

# link-to-dir points at sub/, which holds nested.txt. Following it would
# produce a path under link-to-dir/.
isnt "scan does not follow a symlink to a directory" \
	"$(printf '%s\n' "$out" | grep -c 'link-to-dir/')" "1"
contains "scan still reports the symlink itself" "$out" "/tree/link-to-dir]"

fsnap scan /nonexistent-directory >/dev/null 2>&1
is "scan fails on a missing directory" "$?" "1"

fsnap scan "" >/dev/null 2>&1
is "scan rejects an empty path" "$?" "1"

out=$(fsnap scan empty-tree 2>&1)
is "scan of an empty directory succeeds" "$?" "0"
is "scan of an empty directory prints nothing" "$out" ""

# An unreadable directory is a warning, not an abort.
mkdir -p noperm/locked noperm/readable
: >noperm/readable/f
chmod 000 noperm/locked
if [ "$(id -u)" = "0" ]; then
	skip "unreadable directory does not abort the walk (running as root)"
	skip "unreadable directory is reported on stderr (running as root)"
else
	out=$(fsnap scan noperm 2>err.txt)
	is "unreadable directory does not abort the walk" "$?" "0"
	contains "unreadable directory is reported on stderr" "$(cat err.txt)" "access denied"
fi
chmod 755 noperm/locked

# --------------------------------------------------------------------------
# create
# --------------------------------------------------------------------------
fsnap create tree snap.fsnap 2>err.txt
is "create succeeds over a tree with every entry type" "$?" "0"
is "create over a mixed tree is silent" "$(cat err.txt)" ""

is "create writes one record per entry" \
	"$(wc -l <snap.fsnap | tr -d ' ')" \
	"$(count_entries tree)"

field() { # field <path-suffix> <n>
	awk -F'|' -v s="$1" -v n="$2" 'substr($9, length($9) - length(s) + 1) == s { print $n }' snap.fsnap
}

is "regular file is recorded as F" "$(field /tree/file.txt 1)" "F"
is "directory is recorded as D" "$(field /tree/sub 1)" "D"
is "symlink is recorded as L" "$(field /tree/link.txt 1)" "L"
is "hard link is recorded as F, like any other name" "$(field /tree/hardlink.txt 1)" "F"
if [ -p tree/pipe ]; then
	is "FIFO is recorded as P" "$(field /tree/pipe 1)" "P"
else
	skip "FIFO is recorded as P (mkfifo unavailable)"
fi
if [ "$have_socket" = yes ]; then
	is "socket is recorded as S" "$(field /tree/sock 1)" "S"
else
	skip "socket is recorded as S (could not create one)"
fi

is "permissions are four octal digits" "$(field /tree/file.txt 2)" "0644"
is "size comes from lstat" "$(field /tree/file.txt 5)" "6"
is "symlink target is recorded" "$(field /tree/link.txt 7)" "file.txt"
is "absolute symlink target is recorded" "$(field /tree/abs-link 7)" "/absolute/somewhere"
is "non-symlink has an empty target" "$(field /tree/file.txt 7)" ""

is "regular file is checksummed" "$(field /tree/file.txt 8)" "909783072"
is "empty file checksums to 0" "$(field /tree/empty-file 8)" "0"
is "directory keeps fingerprint 0" "$(field /tree/sub 8)" "0"
# A symlink is checksummed over the string it holds, not over its referent:
# crc32("file.txt").
is "symlink is checksummed over its target string" "$(field /tree/link.txt 8)" "3774289445"
is "hard link shares the fingerprint of its twin" \
	"$(field /tree/hardlink.txt 8)" "$(field /tree/file.txt 8)"
if [ -p tree/pipe ]; then
	is "FIFO keeps fingerprint 0 rather than blocking" "$(field /tree/pipe 8)" "0"
fi

is "paths are absolute" "$(field /tree/file.txt 9 | cut -c1)" "/"

# Regressions: each of these used to abort create or hang it.
for kind in link-to-dir broken-link pipe; do
	[ -e "tree/$kind" ] || [ -L "tree/$kind" ] || continue
	rm -rf one; mkdir one
	cp -R "tree/$kind" one/ 2>/dev/null ||
		ln -s "$(readlink "tree/$kind")" "one/$kind" 2>/dev/null ||
		mkfifo "one/$kind"
	fsnap create one one.fsnap >/dev/null 2>&1
	is "create handles a lone $kind" "$?" "0"
done

fsnap create empty-tree empty.fsnap
is "create of an empty directory succeeds" "$?" "0"
is "create of an empty directory writes an empty snapshot" \
	"$(wc -c <empty.fsnap | tr -d ' ')" "0"

# Snapshots living inside the tree are skipped so they do not end up in
# their own successor.
cp snap.fsnap tree/inner.fsnap
fsnap create tree with-inner.fsnap
is "create skips .fsnap files" "$(grep -c 'inner\.fsnap' with-inner.fsnap)" "0"
rm -f tree/inner.fsnap

fsnap create /nonexistent-directory out.fsnap >/dev/null 2>&1
is "create fails on a missing source" "$?" "1"
fsnap create tree "" >/dev/null 2>&1
is "create rejects an empty destination" "$?" "1"

# create writes a temporary file and copies it into place; nothing may be
# left behind either way.
count_temps() {
	find "${TMPDIR:-/tmp}" -maxdepth 1 -name 'snapshot_*' 2>/dev/null |
		wc -l | tr -d ' '
}
before=$(count_temps)
fsnap create tree tmpcheck.fsnap >/dev/null 2>&1
fsnap create /nonexistent-directory tmpcheck.fsnap >/dev/null 2>&1
after=$(count_temps)
is "create leaves no temporary files behind" "$after" "$before"

# mkstemp turns XXXXXX into a real name only on success; before that the
# cleanup must not unlink the literal template.
victim=${TMPDIR:-/tmp}/snapshot_XXXXXX
if [ ! -e "$victim" ]; then
	echo victim >"$victim"
	fsnap create /nonexistent-directory out.fsnap >/dev/null 2>&1
	if [ -f "$victim" ]; then ok "a failed create does not unlink the mkstemp template"
	else notok "a failed create does not unlink the mkstemp template" "$victim was removed"
	fi
	rm -f "$victim"
else
	skip "a failed create does not unlink the mkstemp template ($victim exists)"
fi

if [ -c /dev/full ]; then
	out=$(fsnap create tree /dev/full 2>&1)
	is "a full destination fails" "$?" "1"
	isnt "a full destination reports a real errno, not Success" "$out" "create failed: Success"
else
	skip "a full destination reports a real errno (no /dev/full)"
	skip "a full destination fails (no /dev/full)"
fi

# --------------------------------------------------------------------------
# Unusual filenames. The format escapes \ | and newline in the path.
# --------------------------------------------------------------------------
mkdir -p odd
: >'odd/with|pipe'
: >'odd/with\backslash'
: >'odd/with space'
: >"$(printf 'odd/with\nnewline')" 2>/dev/null || true
fsnap create odd odd.fsnap 2>/dev/null
is "create handles unusual filenames" "$?" "0"
is "one record per unusual name" \
	"$(wc -l <odd.fsnap | tr -d ' ')" \
	"$(count_entries odd)"
fsnap list odd.fsnap >/dev/null 2>&1
is "unusual filenames survive a create/list round trip" "$?" "0"
contains "a pipe in a name is escaped" "$(cat odd.fsnap)" 'with\|pipe'
contains "a backslash in a name is escaped" "$(cat odd.fsnap)" 'with\\backslash'
contains "a newline in a name is escaped, keeping the record on one line" \
	"$(cat odd.fsnap)" 'with\nnewline'

# --------------------------------------------------------------------------
# list
# --------------------------------------------------------------------------
out=$(fsnap list snap.fsnap 2>&1)
is "list accepts a snapshot it just produced" "$?" "0"
is "list is silent when the snapshot is sound" "$out" ""

fsnap list empty.fsnap >/dev/null 2>&1
is "list accepts an empty snapshot" "$?" "0"

fsnap list /nonexistent.fsnap >/dev/null 2>&1
is "list fails on a missing file" "$?" "1"

# SPEC.md section 15: the parser must reject each of these.
malformed() { # malformed <description> <record> <expected stderr fragment>
	printf '%s\n' "$2" >bad.fsnap
	err=$(fsnap list bad.fsnap 2>&1)
	status=$?
	if [ "$status" = "0" ]; then
		notok "$1" "accepted a record it should have rejected: $2"
		return
	fi
	contains "$1" "$err" "$3"
}

malformed "missing fields are rejected" \
	'F|0644|1000' 'bad.fsnap:1: unparsable line'
malformed "an invalid permission field is rejected" \
	'F|hello|1000|1000|12|123||0|/f' 'bad.fsnap:1: invalid permissions'
malformed "an unknown file type is rejected" \
	'X|0644|1000|1000|12|123||0|/f' 'bad.fsnap:1: invalid file type'
malformed "a negative size is rejected" \
	'F|0644|1000|1000|-10|123||0|/f' 'bad.fsnap:1: invalid size'
malformed "a negative uid is rejected" \
	'F|0644|-1|1000|12|123||0|/f' 'bad.fsnap:1: invalid uid'
malformed "an empty path is rejected" \
	'F|0644|1000|1000|12|123||0|' 'bad.fsnap:1: invalid path'
malformed "leading whitespace in a number is rejected" \
	'F|0644| 1000|1000|12|123||0|/f' 'bad.fsnap:1: invalid uid'
malformed "a plus sign in a number is rejected" \
	'F|0644|+1000|1000|12|123||0|/f' 'bad.fsnap:1: invalid uid'
malformed "an out-of-range number is rejected" \
	'F|0644|99999999999999999999999999|1000|12|123||0|/f' 'bad.fsnap:1: invalid uid'
malformed "a three-digit permission field is rejected" \
	'F|644|1000|1000|12|123||0|/f' 'bad.fsnap:1: invalid permissions'

# The reported line number must be the offending one, not the first.
printf 'F|0644|1000|1000|1|1||0|/a\nF|0644|1000|1000|1|1||0|/b\nX|nope\n' >bad.fsnap
err=$(fsnap list bad.fsnap 2>&1)
contains "the reported line number is the offending one" "$err" "bad.fsnap:3:"

# --------------------------------------------------------------------------
# diff
# --------------------------------------------------------------------------
out=$(fsnap diff snap.fsnap snap.fsnap 2>&1)
is "diff of a snapshot against itself succeeds" "$?" "0"
is "diff of a snapshot against itself is silent" "$out" ""

is "diff reports every record of an empty-to-full comparison as added" \
	"$(fsnap diff empty.fsnap snap.fsnap | grep -c '^A	')" \
	"$(wc -l <snap.fsnap | tr -d ' ')"
is "diff reports every record of a full-to-empty comparison as deleted" \
	"$(fsnap diff snap.fsnap empty.fsnap | grep -c '^D	')" \
	"$(wc -l <snap.fsnap | tr -d ' ')"

out=$(fsnap diff empty.fsnap empty.fsnap 2>&1)
is "diff of two empty snapshots succeeds" "$?" "0"
is "diff of two empty snapshots is silent" "$out" ""

# Classification, one differing field at a time.
base='F|0644|1000|1000|10|100||111|/f'
printf '%s\n' "$base" >a.fsnap

printf 'F|0755|1000|1000|10|100||111|/f\n' >b.fsnap
is "a permission change is reported as P" "$(fsnap diff a.fsnap b.fsnap)" "$(printf 'P\t/f')"

printf 'F|0644|1000|1000|10|100||222|/f\n' >b.fsnap
is "a fingerprint change is reported as M" "$(fsnap diff a.fsnap b.fsnap)" "$(printf 'M\t/f')"

printf 'F|0644|1000|1000|10|200||111|/f\n' >b.fsnap
is "an mtime change is reported as M" "$(fsnap diff a.fsnap b.fsnap)" "$(printf 'M\t/f')"

# A symlink's fingerprint is derived from its target, so a changed target must
# still be reported as L rather than being absorbed into M.
printf 'L|0777|1000|1000|3|100|old|111|/l\n' >a.fsnap
printf 'L|0777|1000|1000|3|100|new|222|/l\n' >b.fsnap
is "a changed symlink target is reported as L, not M" \
	"$(fsnap diff a.fsnap b.fsnap)" "$(printf 'L\t/l')"

printf 'F|0644|1000|1000|10|100||111|/only-in-old\n' >a.fsnap
printf 'F|0644|1000|1000|10|100||111|/only-in-new\n' >b.fsnap
out=$(fsnap diff a.fsnap b.fsnap)
contains "a record only in the old snapshot is reported as D" "$out" "D	/only-in-old"
contains "a record only in the new snapshot is reported as A" "$out" "A	/only-in-new"

# Fields wider than int: comparing by subtraction truncates, and a difference
# of exactly 2^32 used to compare equal, losing the change entirely.
printf 'F|0644|1000|1000|0|100||0|/big\n' >a.fsnap
printf 'F|0644|1000|1000|4294967296|100||0|/big\n' >b.fsnap
is "a size difference of exactly 2^32 is not lost" \
	"$(fsnap diff a.fsnap b.fsnap)" "$(printf 'M\t/big')"
printf 'F|0644|1000|1000|0|100||1|/big\n' >a.fsnap
printf 'F|0644|1000|1000|0|100||4294967297|/big\n' >b.fsnap
is "a fingerprint difference of exactly 2^32 is not lost" \
	"$(fsnap diff a.fsnap b.fsnap)" "$(printf 'M\t/big')"

# diff sorts by path, so readdir order must not leak into the result.
fsnap diff snap.fsnap snap.fsnap >/dev/null 2>&1
is "diff is insensitive to record order" "$?" "0"
sort -r snap.fsnap >reversed.fsnap
out=$(fsnap diff snap.fsnap reversed.fsnap 2>&1)
is "diff of a reordered snapshot reports no changes" "$out" ""

printf 'X|nope\n' >bad.fsnap
err=$(fsnap diff snap.fsnap bad.fsnap 2>&1)
is "diff fails on a malformed input" "$?" "1"
contains "diff names the offending line of a malformed input" "$err" "bad.fsnap:1:"
isnt "diff reports a real diagnostic, not Success" "$err" "diff failed: Success"

fsnap diff /nonexistent.fsnap snap.fsnap >/dev/null 2>&1
is "diff fails on a missing input" "$?" "1"

# --------------------------------------------------------------------------
# A failed create must not leave a stale snapshot looking like a fresh one.
# --------------------------------------------------------------------------
fsnap create tree stale.fsnap >/dev/null 2>&1
before=$(cksum <stale.fsnap)
fsnap create /nonexistent-directory stale.fsnap >/dev/null 2>&1
is "a failed create leaves the destination untouched" "$(cksum <stale.fsnap)" "$before"

# --------------------------------------------------------------------------
# Argument handling
# --------------------------------------------------------------------------
fsnap >/dev/null 2>&1
is "no arguments is an error" "$?" "1"
fsnap nonsense >/dev/null 2>&1
is "an unknown command is an error" "$?" "1"
fsnap scan >/dev/null 2>&1
is "scan without its argument is an error" "$?" "1"
fsnap create tree >/dev/null 2>&1
is "create without its second argument is an error" "$?" "1"
fsnap diff snap.fsnap >/dev/null 2>&1
is "diff without its second argument is an error" "$?" "1"

# --------------------------------------------------------------------------
printf '\n1..%d\n' "$testno"
if [ "$failed" -eq 0 ]; then
	printf '# %d passed' "$((testno - skipped))"
	[ "$skipped" -gt 0 ] && printf ', %d skipped' "$skipped"
	printf '\n'
	exit 0
fi
printf '# %d passed, %d FAILED' "$((testno - failed - skipped))" "$failed"
[ "$skipped" -gt 0 ] && printf ', %d skipped' "$skipped"
printf '\n'
exit 1
