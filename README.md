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

## What it does today

Two commands, and that is the whole feature set:

| Command | Description |
| --- | --- |
| `fsnap scan <directory>` | Recursively walk `<directory>` and print every entry to stdout. |
| `fsnap create <directory> <output_file>` | Recursively walk `<directory>` and write one metadata record per entry into `<output_file>`. |

Comparing two snapshots, verifying a snapshot against the live filesystem, and
listing the contents of a snapshot are **not** implemented — see the
[Roadmap](#roadmap).

## Building

Requires a C11 compiler and **GNU make** (the `Makefile` uses GNU-specific
features by choice; on BSD systems use `gmake`).

```sh
make            # produces build/fsnap
make clean
```

The code targets Linux/glibc: it relies on `dirent.d_type`, `realpath(path, NULL)`,
`mkstemp`, and a few `_GNU_SOURCE` extensions. There is no `install` target.

## Usage

```sh
$ fsnap scan ~/projects/example
[/home/user/projects/example/README.md]
[/home/user/projects/example/src]
[/home/user/projects/example/src/main.c]

$ fsnap create ~/projects/example snapshot.fsnap
$ head -3 snapshot.fsnap
F|0644|1000|1000|182|1785598700|/home/user/projects/example/README.md
D|0755|1000|1000|80|1785598700|/home/user/projects/example/src
F|0644|1000|1000|1319|1785598700|/home/user/projects/example/src/main.c
```

Exit status is `0` on success and `1` on failure, with the reason printed to
stderr.

## Snapshot format

One record per line, seven `|`-separated fields:

```
type|permissions|uid|gid|size|mtime|path
```

| Field | Description |
| --- | --- |
| `type` | `F` regular file, `D` directory, `L` symlink, `P` FIFO, `C` character device, `B` block device, `S` socket, `?` anything else |
| `permissions` | Permission bits, four octal digits |
| `uid` / `gid` | Numeric owner and group |
| `size` | Size in bytes, as reported by `lstat` |
| `mtime` | Modification time, seconds since the Unix epoch |
| `path` | Absolute path, resolved through `realpath` |

The format is ad-hoc and carries no version header. Assume it will change.

## Behaviour worth knowing

- **Symlinks are recorded, never followed.** Metadata comes from `lstat`, so a
  symlink is stored as `L` and the walk does not descend into it. Broken
  symlinks are recorded normally.
- **Paths are absolute**, resolved through `realpath`. Snapshots are therefore
  tied to the machine and the location they were taken from.
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
- **The format breaks on unusual filenames.** There is no quoting or escaping: a
  name containing `|` corrupts the field layout, and a name containing a newline
  splits into two lines.
- **No hard-link or inode information** is recorded, so hard links cannot be
  detected and identical files cannot be correlated.
- **No test suite.** The `tests/` directory exists but is empty.

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
- [ ] Quote or escape the path field, and add a version header to the format.
- [ ] Record inode and link count so hard links can be identified.
- [ ] `fsnap diff` — compare a snapshot against the current state of the tree.
- [ ] `fsnap verify` / `fsnap list` — check and inspect an existing snapshot.
- [ ] Honour `TMPDIR` instead of hardcoding `/tmp`.
- [ ] Add a test suite and wire it into the `Makefile`; build with `-O2` so the
      compiler's flow analysis is actually enabled.
- [ ] Add an `install` target.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
