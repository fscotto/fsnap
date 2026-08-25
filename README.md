# fsnap

**fsnap** is a small command-line tool written in C that walks a directory tree
and records the metadata of everything it finds into a plain-text snapshot file.

> **This is a hobby project. It is not ready for production use.**
>
> It is written for learning and for personal use, has no test suite, no stable
> snapshot format, and no backwards-compatibility guarantees. Do not build a
> backup or auditing process on top of it. Several rough edges are known and
> listed under [Known limitations](#known-limitations) — read that section
> before using it on anything you care about.

[SPEC.md](SPEC.md) is the design document: goals, staged plan, and the rules the
implementation is meant to follow. This README describes what the code does
today, and points at the spec where the two differ.

## What it does today

Four commands are implemented:

| Command | Description |
| --- | --- |
| `fsnap scan <directory>` | Recursively walk `<directory>` and print every entry to stdout. |
| `fsnap create <directory> <output_file>` | Recursively walk `<directory>`, compute fingerprints, and write one metadata record per entry into `<output_file>`. |
| `fsnap list <snapshot_file>` | Read `<snapshot_file>` and validate every record. Reports the first malformed record on stderr; prints nothing when the snapshot is sound. |
| `fsnap diff <old_snapshot> <new_snapshot>` | Load both snapshots, sort their records by path, and print additions, deletions, and changes. |

`list` is a parser and validator by design, not a pretty-printer — see
[SPEC.md](SPEC.md) §12. `verify` (compare a snapshot against the live
filesystem) is not implemented. See the [Roadmap](#roadmap).

## Building

Requires a C11 compiler and **GNU make** (the `Makefile` uses GNU-specific
features by choice; on BSD systems use `gmake`).

```sh
make            # produces build/fsnap
make clean
```

The code targets Linux/glibc: it relies on `dirent.d_type`, `realpath(path, NULL)`,
`mkstemp`, and a few `_GNU_SOURCE` extensions. There is no `install` target.

[SPEC.md](SPEC.md) §19 aims wider than that — Linux *and* NetBSD, POSIX
interfaces over Linux-specific ones. The gap is `_GNU_SOURCE`, which the sources
declare per-file; the interfaces actually used behind it (`getline`, `realpath`
with a `NULL` buffer) are POSIX.1-2008 and would be reachable through
`_POSIX_C_SOURCE 200809L`. `dirent.d_type` is used as a fast path only, with an
`lstat` fallback for `DT_UNKNOWN`, as the spec requires.

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
```

The program exits with `0` on success and `1` on failure. `list` internally
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
| `fingerprint` | CRC32 checksum read through `fopen(path, "r")` for every non-directory entry; `0` for directories |
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
| `L` | Symlink target is the only differing field after the other compared fields match |
| `M` | Any other difference, including type, owner, size, time, or fingerprint |

`diff` does not print a line number or field name when an input snapshot is
malformed; it fails with `EINVAL`.

## Behaviour worth knowing

- **Symlinks are not followed by the walker.** Their metadata comes from
  `lstat()` and their target is read using `readlink()`. The subsequent
  fingerprint step opens the symlink target, however, so `create` fails for a
  broken symlink and fingerprints the referent rather than the link target.
- **Paths are absolute.** The input directory is resolved through `realpath()`
  before traversal; snapshots are tied to the machine and location where they
  were taken.
- **Entries appear in `readdir` order**, not sorted. Two snapshots of the same
  unchanged tree are not guaranteed to be byte-identical.
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
  empty directory, and `list` accepts it silently with exit `0`.

## Known limitations

These are actual, reproduced problems, not hypotheticals:

- **Writing the output is not atomic.** The destination is truncated before the
  copy starts, so a failure partway through (full disk, killed process) leaves a
  truncated file where the previous snapshot used to be.
- **One open directory handle per recursion level.** Deep trees can exhaust the
  file-descriptor limit and fail with `Too many open files`.
- **No exit code for a partial snapshot.** When directories are skipped because
  they are unreadable, the run still exits `0`. If the top-level directory itself
  is unreadable, the result is an empty snapshot reported as success.
- **No hard-link or inode information** is recorded, so hard links cannot be
  detected and identical files cannot be correlated.
- **`create` is unsafe for several non-regular entry types.** It attempts to
  open every non-directory entry to fingerprint it. A FIFO can block forever;
  devices, sockets, unreadable files, and broken symlinks can make creation
  fail.
- **Symlink targets are not escaped by the writer.** A target containing `|`, a
  newline, or a backslash can make the generated record unparsable by `list`.
- **`list` counts the lines in a separate pass over the file.** If the snapshot
  grows between the counting pass and the parsing pass, the records past the
  original count are silently ignored and `list` still exits `0`.
- **No test suite.** There is currently no `tests/` directory or automated
  coverage for the tree shapes listed in [SPEC.md](SPEC.md) §22.

## Roadmap

Rough order of intent, no timeline:

- [ ] Write the snapshot atomically — create the temporary file in the
      destination directory and `rename` it into place, falling back to a copy
      only across filesystems (`EXDEV`).
- [ ] Stop holding a directory handle per level: close each directory before
      recursing, or walk iteratively with an explicit stack.
- [ ] Introduce a distinct exit status for "completed with warnings", the way
      `tar` and `rsync` do, and fail outright when the root directory cannot be
      read.
- [ ] Add a version header to the format (e.g. `FSNAP|1`).
- [ ] Record inode, device ID, and link count so hard links can be identified.
- [ ] Report a snapshot that grew between the counting pass and the parsing pass
      instead of silently ignoring the extra records.
- [ ] `fsnap verify <snapshot> <directory>` — compare a snapshot against the live
      filesystem, distinguishing `ENOENT`, `EACCES`, `ENOTDIR` and `ELOOP`
      rather than collapsing them ([SPEC.md](SPEC.md) §18).
- [ ] Honour `TMPDIR` instead of hardcoding `/tmp`.
- [ ] Add a test suite and wire it into the `Makefile`; build with `-O2` so the
      compiler's flow analysis is actually enabled.
- [ ] Add an `install` target.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
