/* vfs.h - a tiny in-memory UNIX-style file system.
 *
 * Like a real UNIX file system it is a tree of "inodes": every file and
 * directory is a numbered node that knows its name, its parent directory
 * and its permission bits.  "/" is node 0.  Paths are resolved one
 * component at a time, with "." and ".." handled the UNIX way.
 * Nothing is saved to disk: it is rebuilt every boot.
 */
#ifndef SKARLET_VFS_H
#define SKARLET_VFS_H

/* Limits of the in-memory file system.  On Linux, linux/vfs_linux.c
 * implements this same interface over the real disk, with larger limits. */
#ifndef VFS_MAX_NODES
#define VFS_MAX_NODES 160 /* files and folders; also the most a folder lists */
#define VFS_NAME_MAX  28
#define VFS_FILE_MAX  2048 /* bytes in a file (and in Skarlet Write) */
#define VFS_PATH_MAX  128
#endif
#define VFS_ROOT      0

/* Error codes (negative return values), named after the real errno values. */
enum {
    VFS_ENOENT = -2,  /* no such file or directory */
    VFS_EEXIST = -17, /* file exists */
    VFS_ENOTDIR = -20,
    VFS_EISDIR = -21,
    VFS_EINVAL = -22,
    VFS_ENOSPC = -28, /* no space left on device */
    VFS_ENOTEMPTY = -39,
    VFS_EBUSY = -16,
};

void vfs_init(void);
const char *vfs_strerror(int err);

/* Resolve path (absolute, or relative to the directory node cwd). */
int vfs_lookup(int cwd, const char *path);
/* Create a file or directory. Returns the new node or an error. */
int vfs_create(int cwd, const char *path, int is_dir);
int vfs_remove(int node);
int vfs_rename(int node, int new_parent, const char *new_name);

int vfs_read(int node, char *buf, int max);           /* returns bytes read */
int vfs_write(int node, const char *data, int len, int append);

int vfs_is_dir(int node);
int vfs_size(int node);
int vfs_mode(int node);         /* permission bits, e.g. 0755 */
int vfs_parent(int node);
const char *vfs_name(int node);
/* Children of dir, directories first then alphabetical. Returns count. */
int vfs_list(int dir, int *out, int max);
void vfs_path(int node, char *out, int size); /* full path of a node */
int vfs_used(void);                            /* nodes in use */
int vfs_bytes_used(void);

#endif
