<p align="center">
  <img src="https://upload.wikimedia.org/wikipedia/commons/thumb/0/09/Ext2-inode.svg/1200px-Ext2-inode.svg.png"/>
</p>

# tiny-file-system
A small file system inspired by `ext2` using FUSE, written in C.

## Development

Before anything, change line 10 in `benchmark/simple_test.c` to YOUR `MOUNTDIR` (read below)!

Use `run.sh` to run the project. This script automatically does the following:
1. Check if TFS is already mounted at `MOUNTDIR` (defaults to `/tmp/<user>/mountdir`). If it is, it will unmount it
2. Build the project
3. It will check that `MOUNTDIR` is a valid location, and if it doesn't exist it will create it
4. Mount TFS at that location

The ouput is color coded and is verbose with the process in order to ensure reliability, consistency.

### Change mount point
If you want to mount `TFS` somewhere else, you can set the `MOUNTDIR` variable manually, but **make sure it is an absolute path, or the unmount grep match might not work properly!**
For example, if you wanted to mount at `/home/rmcf/someother/mountpoint`, you would do the following:
```bash
$ MOUNTDIR=/home/rmcf/someother/mountpoint ./run.sh
```

You must also **change line 10 in `benchmark/simple_test.c` to the MOUNTDIR specified above!**

### Lightmon
To speed up development and productivity, I recommend using [`lightmon`](https://github.com/reaganmcf/lightmon)

This will watch the project for any files ending in `.c` or `.h`, and execute `./run.sh` whenever the files are updated.
```bash
$ lightmon shell -s run.sh -w .c,.h
```
