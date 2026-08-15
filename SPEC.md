# fsnap — Project Specification

## 1. Overview

`fsnap` is a small POSIX command-line utility written in C for exploring filesystem and Unix system-programming concepts.

The program recursively walks a directory tree, creates textual snapshots of filesystem metadata, reads and validates existing snapshots, compares snapshots, and verifies them against the current filesystem state.

The project is intentionally designed around low-level POSIX and C standard-library APIs rather than high-level filesystem abstractions.

---

## 2. Goals

The main goals of the project are to practice:

* filesystem traversal;
* file and directory metadata;
* file descriptors;
* standard I/O streams;
* symbolic and hard links;
* POSIX data types;
* error handling with `errno`;
* portable Unix programming;
* resource management;
* parsing textual data;
* designing reusable C interfaces.

The implementation should remain reasonably portable across POSIX systems such as Linux and NetBSD.

---

## 3. Non-Goals

The initial version does not aim to provide:

* filesystem monitoring;
* real-time change detection;
* process parallelism;
* threads;
* cryptographic integrity guarantees;
* compression;
* network synchronization;
* platform-specific filesystem features.

These may be explored later as optional extensions.

---

# 4. Commands

## 4.1 `scan`

```text
fsnap scan <directory>
```

Recursively walks the specified directory and prints every discovered filesystem entry to standard output.

### Requirements

The walker must:

* recursively enter real directories;
* ignore `.` and `..`;
* not follow symbolic links;
* process non-directory entries as leaves;
* support filesystems where `dirent.d_type` is unavailable;
* use `lstat()` as a fallback when `d_type == DT_UNKNOWN`;
* propagate traversal and callback errors to the caller.

The traversal mechanism should be independent from the operation performed on each entry.

Conceptually:

```text
walk(directory, operation, context)
```

This allows the same walker to be reused by commands such as `scan` and `create`.

---

# 5. Snapshot Creation

## 5.1 `create`

```text
fsnap create <directory> <snapshot>
```

Creates a textual snapshot representing the state of the specified directory tree.

The command must reuse the generic filesystem walker.

For every discovered entry, the program obtains metadata using `lstat()` and serializes it into the snapshot.

---

## 5.2 Snapshot Entry

Each snapshot record should contain at least:

```text
type
permissions
uid
gid
size
mtime
path
```

Example conceptual representation:

```text
F|0644|1000|1000|125|1753999812|src/main.c
D|0755|1000|1000|512|1753999700|src
L|0777|1000|1000|8|1753999600|current
```

The exact serialization format is part of the project specification and should remain stable once the parser is implemented.

---

## 5.3 File Types

Filesystem entries should be represented explicitly.

Suggested identifiers:

```text
F = regular file
D = directory
L = symbolic link
P = FIFO
S = socket
C = character device
B = block device
? = unknown or unsupported type
```

The filesystem walker itself does not necessarily need to distinguish all of these types.

For traversal purposes, the important distinction is:

```text
directory
non-directory entry
```

The snapshot writer is responsible for determining the specific entry type.

---

# 6. Symbolic Links

Symbolic links must not be followed during traversal.

`lstat()` must therefore be used when metadata about an entry is required.

A symbolic link must be represented as a symbolic link rather than as the object it references.

A later version of the snapshot format should also store the link target obtained using:

```text
readlink()
```

Broken symbolic links must still be representable in a snapshot.

---

# 7. Hard Links

Hard links are not a separate filesystem object type.

Two paths refer to the same filesystem object when both their:

```text
st_dev
st_ino
```

values match.

Future versions of the snapshot format may store:

```text
device ID
inode number
link count
```

to allow hard-link relationships to be detected.

---

# 8. Snapshot Writing

Snapshot output must use standard C I/O streams.

Relevant APIs include:

```text
fopen()
fdopen()
fprintf()
fflush()
fclose()
```

All operations capable of failing must have their return values checked.

In particular, errors from:

```text
fprintf()
fflush()
fclose()
```

must not be ignored.

A buffered write may succeed initially and fail only when the stream is flushed or closed.

---

# 9. Temporary Files

Snapshot generation may use a temporary file created using:

```text
mkstemp()
```

`mkstemp()` returns an open file descriptor.

The descriptor may then be associated with a `FILE *` stream using:

```text
fdopen()
```

Ownership rules must be respected:

```text
mkstemp succeeds + fdopen fails
    -> close(fd)

fdopen succeeds
    -> fclose(stream)
```

The same descriptor must never be closed twice.

Temporary files must be removed during cleanup when no longer required.

---

# 10. Error Handling

Errors should be propagated rather than silently ignored.

The program should use:

```text
0
```

for successful operations and a non-zero result for failures.

Library-style internal functions should generally return:

```text
0
```

on success and:

```text
-1
```

on failure where appropriate.

Diagnostic output should be written to:

```text
stderr
```

while normal command output should be written to:

```text
stdout
```

When cleanup operations occur after an error, the original `errno` should be preserved whenever possible.

---

# 11. Resource Management

Every acquired resource must have a clearly defined owner.

Resources include:

* dynamically allocated memory;
* directory streams;
* file descriptors;
* `FILE *` streams;
* temporary files.

Every successful:

```text
malloc()
opendir()
open()
mkstemp()
fopen()
```

must eventually have a corresponding cleanup operation.

Cleanup must also occur on error paths.

---

# 12. Snapshot Parser

## 12.1 `list`

```text
fsnap list <snapshot>
```

Reads and validates an existing snapshot.

The main purpose of this command is to implement a robust parser for the snapshot format.

The parser should transform each textual record into an internal structure representing one snapshot entry.

Conceptually:

```text
snapshot_entry
├── type
├── permissions
├── uid
├── gid
├── size
├── modification time
└── path
```

The exact C structure is intentionally left as an implementation decision.

---

# 13. Reading Snapshot Records

Records must be read without assuming an arbitrary fixed maximum line length.

The parser should therefore use dynamically sized input rather than constructs such as:

```text
char line[1024]
```

POSIX facilities such as `getline()` are appropriate.

The program must distinguish:

* successful read;
* end of file;
* I/O error;
* malformed record.

---

# 14. Numeric Parsing

Numeric fields must be validated carefully.

The parser must detect:

* empty numeric fields;
* invalid characters;
* trailing characters;
* negative values where forbidden;
* values outside the representable range;
* integer overflow and underflow.

Functions such as:

```text
strtol()
strtoul()
strtoimax()
strtoumax()
```

may be used with appropriate validation.

The parser must not assume that POSIX types such as:

```text
uid_t
gid_t
off_t
time_t
ino_t
```

are equivalent to a specific primitive C type.

---

# 15. Record Validation

The parser must reject malformed records.

Examples include:

```text
F|0644|1000
```

Missing fields.

```text
F|hello|1000|1000|12|123|file
```

Invalid permissions.

```text
X|0644|1000|1000|12|123|file
```

Unknown file type.

```text
F|0644|1000|1000|-10|123|file
```

Invalid size for a regular file.

Errors should preferably identify the offending snapshot and line number.

Example:

```text
snapshot.fsnap:17: invalid uid
```

---

# 16. Path Encoding

Unix filenames may contain almost any byte except:

```text
NUL
/
```

inside a path component.

Therefore filenames may legally contain characters used by the snapshot format, including:

```text
|
newline
backslash
```

Before the snapshot format is considered stable, it must define how such paths are encoded.

Possible strategies include:

### Escaping

For example:

```text
\  -> \\
|  -> \|
newline -> \n
```

### Length-prefixed fields

Store the pathname length explicitly before its contents.

The chosen encoding must allow the parser to reconstruct the original pathname without ambiguity.

---

# 17. Snapshot Comparison

## 17.1 `diff`

Future command:

```text
fsnap diff <old-snapshot> <new-snapshot>
```

Compares two snapshots and reports changes.

Possible result categories:

```text
A = added
D = deleted
M = metadata or contents changed
P = permissions changed
L = symbolic link changed
```

The first implementation may load all snapshot records into memory and sort them by pathname.

---

# 18. Filesystem Verification

## 18.1 `verify`

Future command:

```text
fsnap verify <snapshot> <directory>
```

Compares a stored snapshot with the current filesystem.

It should eventually distinguish conditions such as:

```text
missing file
new file
type changed
size changed
permissions changed
owner changed
mtime changed
symbolic-link target changed
access error
```

Errors such as:

```text
ENOENT
EACCES
ENOTDIR
ELOOP
```

must not all be interpreted as equivalent conditions.

---

# 19. Portability

The project should prefer POSIX interfaces and avoid unnecessary Linux-specific functionality.

Code should be tested on multiple Unix-like systems where practical.

Current target environments include at least:

```text
Linux
NetBSD
```

The implementation must not rely on `dirent.d_type` always being populated.

`DT_UNKNOWN` must be handled correctly.

---

# 20. Build Requirements

The project should be compilable using a standard Makefile.

Recommended compiler configuration:

```text
-std=c11
-Wall
-Wextra
-Wpedantic
-Wconversion
```

Headers should reside under:

```text
include/
```

and compiler include paths should be configured through:

```text
-Iinclude
```

Automatic header dependency generation with:

```text
-MMD
-MP
```

is recommended.

---

# 21. Suggested Project Structure

```text
fsnap/
├── Makefile
├── README.md
├── include/
│   └── fsnap.h
├── src/
│   ├── main.c
│   ├── walk.c
│   ├── scan.c
│   ├── create.c
│   ├── parser.c
│   ├── compare.c
│   └── utility.c
└── tests/
```

The exact module layout may evolve as responsibilities become clearer.

---

# 22. Testing

The filesystem walker and snapshot writer should be tested against trees containing:

```text
regular files
empty files
directories
nested directories
symbolic links
broken symbolic links
hard links
FIFOs
character or block devices where available
unusual filenames
permission errors
```

Example test environment:

```sh
mkdir -p test/subdir

printf 'hello\n' > test/file.txt

ln test/file.txt test/hardlink.txt

ln -s file.txt test/link.txt

ln -s missing.txt test/broken-link

mkfifo test/pipe
```

The walker must never recursively follow `link.txt`, even if a symbolic link points to a directory.

---

# 23. Development Stages

The project is divided into incremental stages.

## Stage 1 — Filesystem Walker

Implement:

```text
fsnap scan
```

Goals:

* directory traversal;
* `dirent`;
* `d_type`;
* `DT_UNKNOWN`;
* `lstat()`;
* callback-based walker.

**Status: implemented.**

## Stage 2 — Snapshot Writer

Implement:

```text
fsnap create
```

Goals:

* filesystem metadata;
* `struct stat`;
* standard I/O;
* serialization;
* temporary files;
* robust cleanup.

**Status: implemented.**

## Stage 3 — Snapshot Parser

Implement:

```text
fsnap list
```

Goals:

* `getline()`;
* parsing;
* numeric conversion;
* input validation;
* dynamic memory;
* snapshot entry representation.

**Status: next development stage.**

## Stage 4 — Snapshot Comparison

Implement:

```text
fsnap diff
```

Goals:

* collections of records;
* sorting;
* comparison;
* filesystem identity and metadata semantics.

## Stage 5 — Filesystem Verification

Implement:

```text
fsnap verify
```

Goals:

* compare persisted metadata against the live filesystem;
* classify changes;
* handle filesystem errors correctly.

---

# 24. Optional Extensions

Once the basic project is complete, possible extensions include:

### Content hashing

Calculate checksums or hashes for regular files.

This introduces additional practice with:

```text
open()
read()
close()
```

and contrasts raw file I/O with standard I/O.

### Sparse-file detection

Compare logical file size with allocated blocks using fields such as:

```text
st_size
st_blocks
```

### Hard-link detection

Track:

```text
st_dev
st_ino
```

to identify multiple paths referring to the same inode.

### Snapshot format versioning

Add a header such as:

```text
FSNAP|1
```

to allow the on-disk format to evolve without making older snapshots impossible to recognize.

### Atomic snapshot replacement

Write the completed snapshot to a temporary file and atomically replace the destination where the target filesystem permits it.

### Process-based hashing

After studying Unix process management, file hashing may be distributed across worker processes.

### Signal handling

After studying Unix signals, long-running snapshot creation could respond cleanly to interruption and remove incomplete temporary files.

---

# 25. Learning Objectives

By the end of the project, the implementation should demonstrate practical understanding of concepts covered by the early chapters of *Advanced Programming in the UNIX Environment*, particularly:

* Unix filesystem semantics;
* file descriptors;
* file metadata;
* directories;
* inode-related concepts;
* symbolic links;
* standard I/O;
* buffering;
* error handling;
* POSIX types;
* portability;
* resource ownership.

The project should favor correctness, explicit error handling, and understanding of Unix behavior over minimizing the amount of code.

