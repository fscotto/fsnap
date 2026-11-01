# fsnap

**fsnap** is a small command-line tool written in C that walks a directory tree
and records the metadata of everything it finds into a plain-text snapshot file.

> **This is a hobby project. It is not ready for production use.**
>
> It is written for learning and for personal use, and has no stable snapshot
> format and no backwards-compatibility guarantees. Do not build a
> backup or auditing process on top of it. Several rough edges are known and
> listed under [Known limitations](#known-limitations) — read that section
> before using it on anything you care about.

[SPEC.md](SPEC.md) is the design document: goals, staged plan, and the rules the
implementation is meant to follow. This README describes what the code does
today, and points at the spec where the two differ.

## What it does today

Five commands are implemented:

| Command | Description |
| --- | --- |
| `fsnap scan <directory>` | Recursively walk `<directory>` and print every entry to stdout. |
| `fsnap create <directory> <output_file>` | Recursively walk `<directory>`, compute fingerprints, and write one metadata record per entry into `<output_file>`. |
| `fsnap list <snapshot_file>` | Read `<snapshot_file>` and validate every record. Reports the first malformed record on stderr; prints nothing when the snapshot is sound. |
| `fsnap diff <old_snapshot> <new_snapshot>` | Load both snapshots, sort their records by path, and print additions, deletions, and changes. |
| `fsnap verify <snapshot> <directory>` | Compare a snapshot with the live tree rooted at `<directory>` and print every lost, new, or changed entry. |

`list` is a parser and validator by design, not a pretty-printer — see
[SPEC.md](SPEC.md) §12. `verify` is the live-filesystem counterpart of `diff`;
see [SPEC.md](SPEC.md) §18.

## Building

Requires a C11 compiler and **GNU make** (the `Makefile` uses GNU-specific
features by choice; on BSD systems use `gmake`).

```sh
make            # produces build/fsnap
make clean
```

`make test` runs the suite in [tests/run.sh](tests/run.sh): 114 black-box checks
driving the built binary over the tree shapes [SPEC.md](SPEC.md) §22 asks for —
regular and empty files, nested directories, symlinks, broken symlinks, hard
links, FIFOs, sockets, unusual filenames and permission errors — plus the
parser cases from §14 and §15, the `diff` classifications, and the `verify`
findings. It is POSIX sh with no dependencies, and skips rather than fails what
the environment cannot provide.

`make valgrind` runs that same suite with every invocation under memcheck,
which takes about 25 seconds against a fraction of a second for `make test`. It
reports a leak or an invalid access as a failed test rather than as a note in
the output, and exits 2 if valgrind is not installed.

`make install` copies the binary to `$(BINDIR)`, which defaults to
`/usr/local/bin`; `make uninstall` removes it again. Both honour `DESTDIR` for
staged installs:

```sh
make install PREFIX=~/.local        # ~/.local/bin/fsnap
make install DESTDIR=/tmp/stage     # /tmp/stage/usr/local/bin/fsnap
sudo make install                   # /usr/local/bin/fsnap
```

`PREFIX`, `BINDIR` and `INSTALL` can all be overridden.

The sources declare no feature test macros of their own; the `Makefile` sets
`-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE` for the whole build. Everything
used behind them — `getline`, `realpath(path, NULL)`, `mkstemp`, `readlink`,
`pathconf` — is POSIX.1-2008, so nothing here requires `_GNU_SOURCE`, which
[SPEC.md](SPEC.md) §19 asks the project to avoid. `_DEFAULT_SOURCE` is there for
`dirent.d_type`, used as a fast path only, with an `lstat` fallback for
`DT_UNKNOWN` as the spec requires.

Only Linux/glibc has been tested.

## Usage

```sh
$ fsnap scan ~/projects/example
[/home/user/projects/example/README.md]
[/home/user/projects/example/src]
[/home/user/projects/example/src/main.c]

$ fsnap create ~/projects/example snapshot.fsnap
$ head -4 snapshot.fsnap
F|0644|1000|1000|182|1785598700||4022421711|/home/user/projects/example/README.md
D|0755|1000|1000|80|1785598700||0|/home/user/projects/example/src
F|0644|1000|1000|1319|1785598700||2184918239|/home/user/projects/example/src/main.c
L|0777|1000|1000|6|1785598700|main.c|0|/home/user/projects/example/symlink-test

$ fsnap list snapshot.fsnap        # silence means every record parsed
$ fsnap list broken.fsnap
broken.fsnap:3: invalid permissions

$ fsnap diff old.fsnap new.fsnap
A	/home/user/projects/example/new-file
P	/home/user/projects/example/README.md
D	/home/user/projects/example/old-file

$ fsnap verify snapshot.fsnap ~/projects/example
A	/home/user/projects/example/new-file
M	/home/user/projects/example/README.md
D	/home/user/projects/example/old-file
```

The program exits with `0` on success and `1` on failure. `verify` also exits
`1` when it finds a difference, after reporting it on stdout. `list` internally
returns `128` for a malformed record, but `main` maps it to exit status `1`.
Diagnostics are written to stderr. A malformed snapshot has no distinct public
exit status from another failure.

## Snapshot format

One record per line, nine `|`-separated fields:

```
type|permissions|uid|gid|size|time|target|fingerprint|path
```

| Field | Description |
| --- | --- |
| `type` | `F` regular file, `D` directory, `L` symlink, `P` FIFO, `C` character device, `B` block device, `S` socket, `?` anything else |
| `permissions` | Permission bits, four octal digits |
| `uid` / `gid` | Numeric owner and group |
| `size` | Size in bytes, as reported by `lstat` |
| `time` | Modification time, seconds since the Unix epoch |
| `target` | Symlink target returned by `readlink`; empty for non-symlink entries |
| `fingerprint` | CRC32 of the contents for a regular file, and of the target string for a symlink, which is what the link holds. `0` for a directory and for the types that cannot be read at all (FIFO, socket, device) |
| `path` | Absolute path: the input root is resolved with `realpath`, then child names are appended; it is escaped |

### Escaping rules
Only the `path` field is escaped by the writer:
- `\` is escaped to `\\`
- `|` is escaped to `\|`
- Newlines are escaped to `\n`

The parser unescapes both `path` and `target`, but the writer does not escape
`target`. Consequently, a symlink target containing `|`, a newline, or a
backslash does not produce a reliably parseable snapshot.

### `diff` output

Each change is printed as `code<TAB>path` after both inputs have been sorted by
pathname:

| Code | Current meaning |
| --- | --- |
| `A` | Present only in the new snapshot |
| `D` | Present only in the old snapshot |
| `P` | Permissions are the first differing field |
| `L` | The symlink target changed. Checked before the fingerprint, which for a link is derived from the target, so the specific answer wins over the generic one |
| `M` | Any other difference, including type, owner, size, time, or fingerprint |

`diff` reports a malformed input the way `list` does, as
`snapshot:line: invalid <field_name>` or `snapshot:line: unparsable line`, and
returns `128` internally; `main` maps it to exit status `1`.

### `verify` output

`verify` walks `<directory>` to find entries absent from the snapshot, then
`lstat`s every stored path. Each finding is printed as `code<TAB>path`, with an
extra reason for an access error. One line is printed per entry, in the first
field that differs.

| Code | Meaning |
| --- | --- |
| `A` | Present in the live tree but absent from the snapshot |
| `D` | Present in the snapshot but absent from the live tree (`ENOENT`) |
| `T` | File type changed |
| `S` | Size changed |
| `P` | Permission bits changed |
| `O` | Owner or group changed |
| `L` | Symlink target changed (checked before size and mtime, which change with it) |
| `M` | Modification time changed |
| `E` | The entry could not be inspected: the path, the reason, and `strerror` are printed. `EACCES`, `ENOTDIR`, `ELOOP` and `ENAMETOOLONG` stay distinct instead of collapsing into "missing" |

`verify` exits `0` when the tree matches, `1` when it finds a difference, and
`1` on a malformed or unreadable snapshot. A malformed snapshot is reported
with the same `snapshot:line:` diagnostic as `list` and `diff`.

## Behaviour worth knowing

- **Symlinks are not followed, anywhere.** Their metadata comes from `lstat()`,
  their target from `readlink()`, and their fingerprint from that target string
  rather than from whatever it points at. Nothing opens the path, so a link to a
  directory and a broken link both snapshot like any other entry.
- **Paths are absolute.** The input directory is resolved through `realpath()`
  before traversal; snapshots are tied to the machine and location where they
  were taken.
- **Entries are visited depth-first**, each directory in `readdir` order, so
  they are not sorted. Two snapshots of the same unchanged tree are not
  guaranteed to be byte-identical.
- **Files whose name ends in `.fsnap` are skipped by `create`**, so an existing
  snapshot inside the tree does not end up inside the new one. `scan` applies no
  such filter and lists everything.
- **Unreadable directories do not abort the run.** If `opendir` fails with
  `EACCES`, a warning goes to stderr, the directory's own record is still
  written, and the walk continues with the rest of the tree.
- **The snapshot is written to a temporary file under `/tmp` first**, then copied
  to the destination. `TMPDIR` is ignored.
- **`list` stops at the first bad record.** It reports the offending line and
  field to stderr and gives up, so it is not a full report of everything wrong
  with a snapshot.
- **`list` catches the malformed records the spec asks for.** All four examples
  in [SPEC.md](SPEC.md) §15 — missing fields, invalid permissions, unknown type,
  negative size — are rejected with the `snapshot:line: invalid <field_name>` or `snapshot:line: unparsable line` form.
- **Numeric fields must be bare digits.** Per [SPEC.md](SPEC.md) §14 the parser
  rejects out-of-range values (`ERANGE`), negative `uid`/`gid`, and any leading
  whitespace or `+` sign, rather than letting `strtoumax` wrap them.
- **An empty snapshot is valid.** `create` produces a zero-byte file for an
  empty directory, `list` accepts it silently with exit `0`, and `diff` treats
  it as carrying no records, so every entry on the other side is reported as
  added or deleted.
- **`verify` compares paths, not contents.** The stored fingerprint is not
  recomputed, so a regular file whose bytes changed while its size and mtime
  stayed the same is not reported.
- **`verify` uses the absolute paths stored in the snapshot.** It walks the
  given directory to find new entries, but every stored record is checked at
  its recorded path, so the tree must still be at the location it was scanned
  from.
- **`verify` ignores `.fsnap` files while looking for new entries**, for the
  same reason `create` does: a snapshot living inside the tree is not part of
  the tree's contents.
- **Every recorded metadata field except the fingerprint is compared**,
  including a directory's size and mtime. Adding or removing a child can
  therefore report its parent as well as the child itself.

## Known limitations

These are actual, reproduced problems, not hypotheticals:

- **Writing the output is not atomic.** The destination is truncated before the
  copy starts, so a failure partway through (full disk, killed process) leaves a
  truncated file where the previous snapshot used to be.
- **No exit code for a partial snapshot.** When directories are skipped because
  they are unreadable, the run still exits `0`. If the top-level directory itself
  is unreadable, the result is an empty snapshot reported as success.
- **No hard-link or inode information** is recorded, so hard links cannot be
  detected and identical files cannot be correlated.
- **An unreadable regular file aborts the whole run.** `create` opens every
  regular file to checksum it, and a failure there stops the walk and leaves no
  snapshot, even though the same run tolerates a directory it cannot enter.
  Running `fsnap create /etc` as an ordinary user fails on the first of the 39
  files it may not read.
- **Symlink targets are not escaped by the writer.** A target containing `|`, a
  newline, or a backslash can make the generated record unparsable by `list`.
- **`list` counts the lines in a separate pass over the file.** If the snapshot
  grows between the counting pass and the parsing pass, the records past the
  original count are silently ignored and `list` still exits `0`.

## Roadmap

Rough order of intent, no timeline:

- [ ] Write the snapshot atomically — create the temporary file in the
      destination directory and `rename` it into place, falling back to a copy
      only across filesystems (`EXDEV`).
- [x] Stop holding a directory handle per level: close each directory before
      recursing, or walk iteratively with an explicit stack.
- [ ] Introduce a distinct exit status for "completed with warnings", the way
      `tar` and `rsync` do, and fail outright when the root directory cannot be
      read.
- [ ] Add a version header to the format (e.g. `FSNAP|1`).
- [ ] Record inode, device ID, and link count so hard links can be identified.
- [ ] Report a snapshot that grew between the counting pass and the parsing pass
      instead of silently ignoring the extra records.
- [x] `fsnap verify <snapshot> <directory>` — compare a snapshot against the live
      filesystem, distinguishing `ENOENT`, `EACCES`, `ENOTDIR` and `ELOOP`
      rather than collapsing them ([SPEC.md](SPEC.md) §18).
- [ ] Honour `TMPDIR` instead of hardcoding `/tmp`.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
