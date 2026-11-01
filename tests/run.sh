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

# Set by `make valgrind` to a memcheck command line. Running the whole suite
# under a checker is then just a matter of prefixing every invocation, which is
# what fsnap() below is for. It stays empty for a plain `make test`.
VALGRIND=${VALGRIND:-}

# A bug in the walker can make create block forever rather than fail -- opening
# a FIFO waits for a writer that never comes. Cap every invocation so that turns
# into a failing test instead of a hung suite. timeout(1) is not in POSIX, so
# fall back to running unguarded where it is missing.
if command -v timeout >/dev/null 2>&1; then
	if [ -n "$VALGRIND" ]; then
		TIMEOUT="timeout 300" # memcheck costs an order of magnitude
	else
		TIMEOUT="timeout 30"
	fi
else
	TIMEOUT=""
fi

fsnap() {
	# shellcheck disable=SC2086 # deliberate split: both are command prefixes
	$TIMEOUT $VALGRIND "$FSNAP" "$@"
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
	is "unreadable directory returns warning status" "$?" "2"
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
	"$(($(wc -l <snap.fsnap) - 1))" \
	"$(count_entries tree)"

field() { # field <path-suffix> <n>
	awk -F'|' -v s="$1" -v n="$2" 'substr($12, length($12) - length(s) + 1) == s { print $n }' snap.fsnap
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

is "paths are absolute" "$(field /tree/file.txt 12 | cut -c1)" "/"

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
is "create of an empty directory writes only the version header" \
	"$(cat empty.fsnap)" "FSNAP|1"

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

# create writes a temporary file and renames it into place; nothing may be
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
	"$(($(wc -l <odd.fsnap) - 1))" \
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
	"$(($(wc -l <snap.fsnap) - 1))"
is "diff reports every record of a full-to-empty comparison as deleted" \
	"$(fsnap diff snap.fsnap empty.fsnap | grep -c '^D	')" \
	"$(($(wc -l <snap.fsnap) - 1))"

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
{ head -n 1 snap.fsnap; tail -n +2 snap.fsnap | sort -r; } >reversed.fsnap
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
# verify
# --------------------------------------------------------------------------
# Each test starts from the same small tree, because a change to the live
# filesystem is what verify is asked to notice.
verify_fixture() {
	rm -rf vtree vsnap.fsnap
	mkdir -p vtree/sub
	printf 'hello\n' >vtree/file.txt
	printf 'nested\n' >vtree/sub/nested.txt
	ln -s file.txt vtree/link.txt
	fsnap create vtree vsnap.fsnap >/dev/null 2>&1
}

verify_fixture
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
is "verify of an unchanged tree succeeds" "$?" "0"
is "verify of an unchanged tree is silent" "$out" ""

printf 'new\n' >vtree/new.txt
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
is "verify reports a difference with a failure status" "$?" "1"
contains "a new file is reported as A" "$out" \
	"$(printf 'A\t%s/vtree/new.txt' "$WORK")"

verify_fixture
rm vtree/file.txt
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "a missing file is reported as D" "$out" \
	"$(printf 'D\t%s/vtree/file.txt' "$WORK")"

verify_fixture
printf 'more\n' >>vtree/file.txt
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "a size change is reported as S" "$out" \
	"$(printf 'S\t%s/vtree/file.txt' "$WORK")"

verify_fixture
chmod 600 vtree/file.txt
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "a permission change is reported as P" "$out" \
	"$(printf 'P\t%s/vtree/file.txt' "$WORK")"

verify_fixture
if touch -t 200001010000 vtree/file.txt 2>/dev/null; then
	out=$(fsnap verify vsnap.fsnap vtree 2>&1)
	contains "an mtime change is reported as M" "$out" \
		"$(printf 'M\t%s/vtree/file.txt' "$WORK")"
else
	skip "an mtime change is reported as M (touch -t unavailable)"
fi

verify_fixture
ln -sf other-target vtree/link.txt
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "a changed symlink target is reported as L" "$out" \
	"$(printf 'L\t%s/vtree/link.txt' "$WORK")"

verify_fixture
rm vtree/file.txt
mkdir vtree/file.txt
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "a changed file type is reported as T" "$out" \
	"$(printf 'T\t%s/vtree/file.txt' "$WORK")"

verify_fixture
rm -rf vtree/sub
: >vtree/sub
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "ENOTDIR is distinguished from a missing file" "$out" \
	"$(printf 'E\t%s/vtree/sub/nested.txt\tNot a directory' "$WORK")"

verify_fixture
if [ "$(id -u)" = "0" ]; then
	skip "EACCES is distinguished from a missing file (running as root)"
else
	chmod 000 vtree/sub
	out=$(fsnap verify vsnap.fsnap vtree 2>&1)
	contains "EACCES is distinguished from a missing file" "$out" \
		"$(printf 'E\t%s/vtree/sub/nested.txt\tPermission denied' "$WORK")"
	chmod 755 vtree/sub
fi

verify_fixture
: >vempty.fsnap
mkdir vempty
out=$(fsnap verify vempty.fsnap vempty 2>&1)
is "verify of an empty snapshot against an empty directory succeeds" "$?" "0"
is "verify of an empty snapshot against an empty directory is silent" "$out" ""

out=$(fsnap verify vempty.fsnap vtree 2>&1)
is "verify of an empty snapshot against a populated tree fails" "$?" "1"
is "every entry is reported as new" \
	"$(printf '%s\n' "$out" | grep -c '^A	')" \
	"$(count_entries vtree)"

verify_fixture
fsnap create vtree vtree/self.fsnap >/dev/null 2>&1
out=$(fsnap verify vtree/self.fsnap vtree 2>&1)
isnt "verify does not report the snapshot file as new" "$out" \
	"$(printf 'A\t%s/vtree/self.fsnap' "$WORK")"
# Writing the snapshot changed the metadata of the directory holding it, so
# silence is not guaranteed here; the point is that no A line names the
# snapshot itself.
is "verify finds no new entries when only the snapshot changed" \
	"$(printf '%s\n' "$out" | grep -c '^A	')" "0"

printf 'X|nope\n' >badv.fsnap
err=$(fsnap verify badv.fsnap vtree 2>&1)
is "verify fails on a malformed snapshot" "$?" "1"
contains "verify names the offending line" "$err" "badv.fsnap:1:"

fsnap verify /nonexistent.fsnap vtree >/dev/null 2>&1
is "verify fails on a missing snapshot" "$?" "1"
fsnap verify vsnap.fsnap /nonexistent-directory >/dev/null 2>&1
is "verify fails on a missing directory" "$?" "1"

# --------------------------------------------------------------------------
# A failed create must not leave a stale snapshot looking like a fresh one.
# --------------------------------------------------------------------------
fsnap create tree stale.fsnap >/dev/null 2>&1
before=$(cksum <stale.fsnap)
fsnap create /nonexistent-directory stale.fsnap >/dev/null 2>&1
is "a failed create leaves the destination untouched" "$(cksum <stale.fsnap)" "$before"

# --------------------------------------------------------------------------
# Checklist regressions: versioning, links, warnings, atomic publication,
# temporary directories, and reading through EOF without a counting pass.
# --------------------------------------------------------------------------
is "new snapshots declare format version 1" "$(head -n 1 snap.fsnap)" "FSNAP|1"
is "hard links share a device ID" "$(field /tree/file.txt 9)" "$(field /tree/hardlink.txt 9)"
is "hard links share an inode" "$(field /tree/file.txt 10)" "$(field /tree/hardlink.txt 10)"
is "hard links record their link count" "$(field /tree/file.txt 11)" "2"
isnt "independent files have different inodes" "$(field /tree/file.txt 10)" "$(field /tree/empty-file 10)"

for n in 9 10 11; do
	printf 'FSNAP|1\nF|0644|0|0|0|1||0|1|2|1|/f\n' >links-old.fsnap
	awk -F'|' -v OFS='|' -v n="$n" 'NR == 2 { $n = $n + 1 } { print }' links-old.fsnap >links-new.fsnap
	is "diff compares identity/link field $n" "$(fsnap diff links-old.fsnap links-new.fsnap)" "$(printf 'M\t/f')"
done
verify_fixture
ln vtree/file.txt outside-hardlink
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "verify detects a changed hard-link count" "$out" "$(printf 'H\t%s/vtree/file.txt' "$WORK")"
rm outside-hardlink
cp -p vtree/file.txt replacement
mv replacement vtree/file.txt
out=$(fsnap verify vsnap.fsnap vtree 2>&1)
contains "verify detects a replaced inode with identical size and mtime" "$out" "$(printf 'I\t%s/vtree/file.txt' "$WORK")"

# Legacy records are still accepted, and missing new metadata does not create
# spurious differences when comparing an old-format snapshot to a new one.
awk -F'|' 'NR > 1 { print $1 "|" $2 "|" $3 "|" $4 "|" $5 "|" $6 "|" $7 "|" $8 "|" $12 }' snap.fsnap >legacy.fsnap
fsnap list legacy.fsnap >/dev/null 2>&1
is "list accepts legacy headerless records" "$?" "0"
out=$(fsnap diff legacy.fsnap snap.fsnap 2>&1)
is "legacy and versioned records compare without invented metadata changes" "$out" ""
for cmd in list diff verify; do
	printf 'FSNAP|99\n' >version.fsnap
	case $cmd in
	list) out=$(fsnap list version.fsnap 2>&1) ;;
	diff) out=$(fsnap diff version.fsnap snap.fsnap 2>&1) ;;
	verify) out=$(fsnap verify version.fsnap tree 2>&1) ;;
	esac
	is "$cmd rejects an unsupported format version" "$?" "1"
	contains "$cmd explains an unsupported format version" "$out" "unsupported snapshot version"
done
for spec in 'device:8:-1' 'inode:9:184467440737095516160' 'link count:10:0'; do
	name=${spec%%:*}; rest=${spec#*:}; index=${rest%%:*}; value=${rest#*:}
	printf 'FSNAP|1\nF|0644|0|0|0|1||0|1|2|1|/f\n' |
		awk -F'|' -v OFS='|' -v n="$index" -v v="$value" 'NR == 2 { $(n+1) = v } { print }' >version.fsnap
	out=$(fsnap list version.fsnap 2>&1)
	is "invalid $name is rejected in version 1" "$?" "1"
	contains "invalid $name names the field and physical line" "$out" "version.fsnap:2: invalid $name"
done
printf 'FSNAP|1\nF|0644|0|0|0|1||0|/legacy\n' >version.fsnap
fsnap list version.fsnap >/dev/null 2>&1
is "a versioned snapshot cannot contain a legacy record" "$?" "1"
printf 'F|0644|0|0|0|1||0|/f\000hidden\n' >nul.fsnap
fsnap list nul.fsnap >/dev/null 2>&1
is "embedded NUL cannot hide trailing input" "$?" "1"

mkdir link-targets
ln -s 'with|pipe' link-targets/pipe
ln -s 'with\backslash' link-targets/backslash
ln -s "$(printf 'with\nnewline')" link-targets/newline
ln -s 'trailing\' link-targets/trailing
ln -s 'literal\n' link-targets/literal
fsnap create link-targets targets.fsnap >/dev/null 2>&1
is "create accepts escaped symlink targets" "$?" "0"
fsnap list targets.fsnap >/dev/null 2>&1
is "escaped targets survive create/list" "$?" "0"
out=$(fsnap verify targets.fsnap link-targets 2>&1)
is "escaped targets verify successfully" "$?" "0"
is "escaped targets retain their exact bytes" "$out" ""

mkdir partial
mkdir partial/locked
: >partial/good
chmod 000 partial/locked
if [ "$(id -u)" = 0 ]; then
	for name in 'create warning' 'verify warning' 'root scan failure' 'root create failure' 'old output preservation'; do
		skip "$name for unreadable directories (running as root)"
	done
else
	fsnap create partial partial.fsnap >out.txt 2>err.txt
	is "create publishes a partial snapshot with status 2" "$?" "2"
	fsnap verify partial.fsnap partial >out.txt 2>err.txt
	is "verify cannot declare an incomplete traversal fully successful" "$?" "2"
	fsnap scan partial/locked >/dev/null 2>&1
	is "scan fails outright on an unreadable root" "$?" "1"
	before=$(cksum <partial.fsnap)
	fsnap create partial/locked partial.fsnap >/dev/null 2>&1
	is "create fails outright on an unreadable root" "$?" "1"
	is "an unreadable root preserves the old output" "$(cksum <partial.fsnap)" "$before"
fi
chmod 755 partial/locked
: >partial/unreadable
chmod 000 partial/unreadable
if [ "$(id -u)" = 0 ]; then
	skip "unreadable regular file produces a partial snapshot (running as root)"
	skip "unreadable regular file is reported (running as root)"
	skip "unreadable regular file is omitted, not given a false checksum (running as root)"
	skip "readable sibling survives (running as root)"
else
	fsnap create partial partial.fsnap >out.txt 2>err.txt
	is "unreadable regular file produces a partial snapshot" "$?" "2"
	contains "unreadable regular file is reported" "$(cat err.txt)" "entry skipped"
	is "unreadable file is omitted, not given a false checksum" "$(grep -c '/unreadable$' partial.fsnap)" "0"
	is "a readable sibling is still recorded" "$(grep -c '/good$' partial.fsnap)" "1"
fi
chmod 600 partial/unreadable

printf 'old snapshot\n' >atomic.fsnap
ln atomic.fsnap held-old
fsnap create tree atomic.fsnap >/dev/null 2>&1
is "atomic replacement succeeds" "$?" "0"
is "atomic replacement does not truncate the old inode" "$(cat held-old)" "old snapshot"
ln -s held-old output-link
fsnap create tree output-link >/dev/null 2>&1
is "a symlink destination is rejected" "$?" "1"
is "a symlink destination cannot overwrite its referent" "$(cat held-old)" "old snapshot"
fsnap create tree tree/no-extension >/dev/null 2>&1
is "an output without a snapshot suffix can live in the tree" "$?" "0"
is "output and temporary files do not snapshot themselves" "$(grep -c 'no-extension\|snapshot_' tree/no-extension)" "0"
rm tree/no-extension
mkdir tmp-work
TMPDIR="$WORK/tmp-work" fsnap create tree tmpdir.fsnap >/dev/null 2>&1
is "create honours a valid TMPDIR" "$?" "0"
is "TMPDIR is cleaned after publication" "$(count_entries tmp-work)" "0"
TMPDIR=tmp-work fsnap create tree relative.fsnap >/dev/null 2>&1
is "a relative TMPDIR works" "$?" "0"
before=$(cksum <tmpdir.fsnap)
TMPDIR="$WORK/nonexistent" fsnap create tree tmpdir.fsnap >/dev/null 2>&1
is "an invalid TMPDIR fails instead of silently using /tmp" "$?" "1"
is "an invalid TMPDIR preserves the destination" "$(cksum <tmpdir.fsnap)" "$before"
TMPDIR="$WORK/tree" fsnap create tree inside-tmp.fsnap >/dev/null 2>&1
is "TMPDIR may be inside the scanned tree" "$?" "0"
is "a work file inside the tree is excluded" "$(grep -c 'snapshot_' inside-tmp.fsnap)" "0"

# A single FIFO writer supplies more than two allocation blocks, followed by a
# bad record. All three readers must reach it; no second open/count is needed.
for cmd in list diff verify; do
	mkfifo input.fifo
	(
		i=0
		while [ "$i" -lt 150 ]; do
			printf 'F|0644|0|0|0|1||0|/entry-%d\n' "$i"
			i=$((i + 1))
		done
		printf 'X|bad\n'
	) >input.fifo &
	writer=$!
	case $cmd in
	list) out=$(fsnap list input.fifo 2>&1) ;;
	diff) out=$(fsnap diff input.fifo snap.fsnap 2>&1) ;;
	verify) out=$(fsnap verify input.fifo tree 2>&1) ;;
	esac
	is "$cmd reads a stream through its last record" "$?" "1"
	contains "$cmd does not ignore records past an allocation boundary" "$out" "input.fifo:151:"
	wait "$writer"
	rm input.fifo
done

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
fsnap verify snap.fsnap >/dev/null 2>&1
is "verify without its second argument is an error" "$?" "1"

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
