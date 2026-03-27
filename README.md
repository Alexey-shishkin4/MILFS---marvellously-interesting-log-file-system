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

## Project structure

```text
src/
  main.cpp
  cli.cpp
  fs_core.cpp
  inode.cpp
  inode_table.cpp
  headers/
    fs_core.h
    inode.h
    inode_table.h
    directory.h
