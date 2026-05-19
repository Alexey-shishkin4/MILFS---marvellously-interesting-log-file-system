# MILFS — marvellously interesting log file system

## Current status

This repository currently implements:

- An **in-memory filesystem core**
- A simple **CLI interface**
- Basic inode support
- Directory entry support
- Inode table management
- Basic file and directory operations

## Implemented features

### Core structures
- `Inode`
- `InodeTable`
- `DirectoryEntries`
- `FileSystemState`

### Supported operations
- `mkdir <path>` — create a directory
- `create <path>` — create an empty file
- `write <path> <data>` — write data into a file
- `cat <path>` / `read <path>` — read file contents
- `ls [path]` — list directory contents
- `help` — show available commands
- `exit` — exit the CLI

### Internal behavior
- Directories are represented through a mapping of **name -> inode id**
- Files and directories are tracked using an **inode table**
- File contents are currently stored in memory
- Path lookup is supported for absolute paths such as `/dir/file`
## Running MILFS via FUSE

MILFS includes a FUSE-based userspace mount target named `milfs_fuse`.  
This target mounts the filesystem into a directory and allows interacting with it using обычные shell-команды (`ls`, `mkdir`, `cat`, `mv`, `rm`, ...).

### Requirements

- Linux
- `fuse3`
- CMake
- C++ compiler with C++20 support

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y fuse3 libfuse3-dev pkg-config cmake g++
```

### Build

From the project root:

```bash
rm -rf build
cmake -S . -B build
cmake --build build -j
```

If the project uses tests and they fail, but you only need the FUSE target, first fix any global build issues in the repository.  
A successful FUSE run requires the `milfs_fuse` executable to be built without linker errors.

### Mount

Create a mountpoint:

```bash
mkdir -p ~/mnt/milfs
```

Run the filesystem in foreground mode:

```bash
./build/milfs_fuse ~/mnt/milfs -f
```

Foreground mode (`-f`) is recommended for debugging because logs and crashes are immediately visible in the terminal.

### Basic manual test

Open a second terminal and run:

```bash
ls -la ~/mnt/milfs
mkdir ~/mnt/milfs/test
touch ~/mnt/milfs/test/file.txt
echo hello > ~/mnt/milfs/test/file.txt
cat ~/mnt/milfs/test/file.txt
mv ~/mnt/milfs/test/file.txt ~/mnt/milfs/test/renamed.txt
truncate -s 2 ~/mnt/milfs/test/renamed.txt
cat ~/mnt/milfs/test/renamed.txt
rm ~/mnt/milfs/test/renamed.txt
ls -la ~/mnt/milfs/test
```

Expected behavior:
- directory creation works;
- file creation works;
- writing and reading file contents works;
- rename works;
- truncate works;

### Unmount

To unmount the filesystem:

```bash
fusermount3 -u ~/mnt/milfs
```

`fusermount3` is the standard unmount tool for FUSE 3 filesystems.

### Notes

- The current FUSE target mounts a userspace filesystem process into a host directory.
- Depending on the current implementation, the mounted filesystem may be in-memory only and may not persist data across restarts.
- In FUSE, operations are registered through `fuse_operations`; if some callback is missing, the kernel/libfuse may return `ENOSYS` for that operation.
- For FUSE 3, callbacks must use the correct signatures, for example `rename(const char*, const char*, unsigned int flags)`.

### Troubleshooting

#### 1. Build fails with `undefined reference to __asan_*`
This usually means some objects were compiled with AddressSanitizer, but the final target is not linked with ASan runtime.  
Make sure sanitizer flags are applied consistently to compile and link steps, or disable ASan for this build.

#### 2. `Transport endpoint is not connected`
This usually happens when the FUSE process crashed but the mountpoint still exists.  
Unmount it manually:

```bash
fusermount3 -u ~/mnt/milfs
```

If needed:

```bash
fusermount3 -uz ~/mnt/milfs
```

#### 3. `Function not implemented`
This means the corresponding FUSE callback is missing or returns `-ENOSYS`.  
Check whether the operation is registered in `fuse_operations` and implemented in the FUSE layer.

#### 4. Permission problems
Ensure FUSE 3 is installed correctly and that unprivileged FUSE mounts are allowed on the system.
