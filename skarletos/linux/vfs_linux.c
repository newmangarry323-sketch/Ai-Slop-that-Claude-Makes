/* vfs_linux.c - the SkarletOS file-system interface (src/vfs.h), on Linux.
 *
 * The kernel has its own little in-memory file system (src/vfs.c).  On
 * Linux, Skarlet Files, Skarlet Write and the Folder View widget use the real
 * disk instead, through this file.  The interface identifies files by small
 * numbers ("nodes"); here a node is a slot in a table of paths we have seen,
 * found again through a hash table.  When the table is full, the oldest
 * slots are reused.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../src/lib.h"
#include "../src/vfs.h"

#define SLOTS 16384
#define BUCKETS 32768

static char *paths[SLOTS];
static int next_in_bucket[SLOTS];
static int buckets[BUCKETS];
static int next_slot = 1; /* slot 0 is "/" */
static char home[VFS_PATH_MAX] = "/home/user";

static unsigned hash(const char *s)
{
    unsigned h = 2166136261u; /* FNV-1a */
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h % BUCKETS;
}

static void unlink_slot(int id)
{
    int *p = &buckets[hash(paths[id])];
    while (*p >= 0 && *p != id)
        p = &next_in_bucket[*p];
    if (*p == id)
        *p = next_in_bucket[id];
}

/* The node for an absolute, normalised path. */
static int intern(const char *path)
{
    if (!strcmp(path, "/"))
        return VFS_ROOT;
    unsigned h = hash(path);
    for (int id = buckets[h]; id >= 0; id = next_in_bucket[id])
        if (!strcmp(paths[id], path))
            return id;
    int id = next_slot;
    next_slot = next_slot + 1 >= SLOTS ? 1 : next_slot + 1;
    if (paths[id]) {
        unlink_slot(id);
        free(paths[id]);
    }
    paths[id] = strdup(path);
    next_in_bucket[id] = buckets[h];
    buckets[h] = id;
    return id;
}

static int valid(int n) { return n >= 0 && n < SLOTS && paths[n]; }

static int err_code(int e)
{
    switch (e) {
    case ENOENT: return VFS_ENOENT;
    case EEXIST: return VFS_EEXIST;
    case ENOTDIR: return VFS_ENOTDIR;
    case EISDIR: return VFS_EISDIR;
    case ENOSPC: return VFS_ENOSPC;
    case ENOTEMPTY: return VFS_ENOTEMPTY;
    case EBUSY: return VFS_EBUSY;
    }
    return -e;
}

const char *vfs_strerror(int err) { return strerror(-err); }

void vfs_init(void)
{
    const char *h = getenv("HOME");
    if (h && *h)
        k_strlcpy(home, h, sizeof home);
    for (int i = 0; i < BUCKETS; i++)
        buckets[i] = -1;
    paths[VFS_ROOT] = strdup("/");
}

/* Resolve path against the directory node cwd: "~" is the home folder, and
 * "." and ".." are handled by name, as the shell does (no symlink games). */
int vfs_lookup(int cwd, const char *path)
{
    char buf[PATH_MAX], out[PATH_MAX];
    if (!path || !*path)
        return valid(cwd) ? cwd : VFS_ENOENT;
    if (path[0] == '~' && (path[1] == '/' || !path[1]))
        snprintf(buf, sizeof buf, "%s%s", home, path + 1);
    else if (!strncmp(path, "/home/user", 10) && (path[10] == '/' || !path[10]))
        snprintf(buf, sizeof buf, "%s%s", home, path + 10); /* SkarletOS's name for home */
    else if (path[0] == '/')
        snprintf(buf, sizeof buf, "%s", path);
    else
        snprintf(buf, sizeof buf, "%s/%s", valid(cwd) ? paths[cwd] : "/", path);
    /* Normalise. */
    int n = 0;
    out[0] = 0;
    for (char *seg = strtok(buf, "/"); seg; seg = strtok(0, "/")) {
        if (!strcmp(seg, "."))
            continue;
        if (!strcmp(seg, "..")) {
            while (n > 0 && out[n - 1] != '/')
                n--;
            if (n > 0)
                n--;
            out[n] = 0;
            continue;
        }
        if (n + 1 + (int)strlen(seg) >= (int)sizeof out)
            return VFS_EINVAL;
        out[n++] = '/';
        strcpy(out + n, seg);
        n += (int)strlen(seg);
    }
    if (n == 0)
        strcpy(out, "/");
    if (strlen(out) >= VFS_PATH_MAX)
        return VFS_EINVAL;
    struct stat st;
    if (lstat(out, &st) != 0)
        return err_code(errno);
    return intern(out);
}

int vfs_create(int cwd, const char *path, int is_dir)
{
    if (vfs_lookup(cwd, path) >= 0)
        return VFS_EEXIST;
    /* Find the absolute path by looking up the parent. */
    char parent[PATH_MAX];
    snprintf(parent, sizeof parent, "%s", path);
    char *slash = strrchr(parent, '/');
    const char *name = path;
    int dir = cwd;
    if (slash) {
        name = path + (slash - parent) + 1;
        *slash = 0;
        dir = vfs_lookup(cwd, slash == parent ? "/" : parent);
    }
    if (dir < 0)
        return dir;
    char full[PATH_MAX + 260];
    snprintf(full, sizeof full, "%s/%s", dir == VFS_ROOT ? "" : paths[dir], name);
    if (is_dir) {
        if (mkdir(full, 0755) != 0)
            return err_code(errno);
    } else {
        int fd = open(full, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd < 0)
            return err_code(errno);
        close(fd);
    }
    return intern(full);
}

int vfs_remove(int node)
{
    if (!valid(node) || node == VFS_ROOT)
        return VFS_EBUSY;
    struct stat st;
    if (lstat(paths[node], &st) != 0)
        return err_code(errno);
    if ((S_ISDIR(st.st_mode) ? rmdir(paths[node]) : unlink(paths[node])) != 0)
        return err_code(errno);
    return 0;
}

int vfs_rename(int node, int new_parent, const char *new_name)
{
    if (!valid(node) || !valid(new_parent))
        return VFS_ENOENT;
    char to[PATH_MAX];
    snprintf(to, sizeof to, "%s/%s", new_parent == VFS_ROOT ? "" : paths[new_parent], new_name);
    if (rename(paths[node], to) != 0)
        return err_code(errno);
    return 0;
}

int vfs_read(int node, char *buf, int max)
{
    if (!valid(node))
        return VFS_ENOENT;
    int fd = open(paths[node], O_RDONLY);
    if (fd < 0)
        return err_code(errno);
    int total = 0;
    while (total < max) {
        ssize_t r = read(fd, buf + total, (size_t)(max - total));
        if (r <= 0)
            break;
        total += (int)r;
    }
    close(fd);
    return total;
}

int vfs_write(int node, const char *data, int len, int append)
{
    if (!valid(node))
        return VFS_ENOENT;
    int fd = open(paths[node], O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0)
        return err_code(errno);
    int done = 0;
    while (done < len) {
        ssize_t w = write(fd, data + done, (size_t)(len - done));
        if (w <= 0) {
            int e = errno;
            close(fd);
            return err_code(e);
        }
        done += (int)w;
    }
    close(fd);
    return done;
}

int vfs_is_dir(int n)
{
    struct stat st;
    return valid(n) && stat(paths[n], &st) == 0 && S_ISDIR(st.st_mode);
}

int vfs_size(int n)
{
    struct stat st;
    if (!valid(n) || stat(paths[n], &st) != 0)
        return 0;
    return st.st_size > INT_MAX ? INT_MAX : (int)st.st_size;
}

int vfs_mode(int n)
{
    struct stat st;
    return valid(n) && lstat(paths[n], &st) == 0 ? (int)(st.st_mode & 07777) : 0;
}

int vfs_parent(int n)
{
    if (!valid(n) || n == VFS_ROOT)
        return VFS_ROOT;
    char buf[PATH_MAX];
    snprintf(buf, sizeof buf, "%s", paths[n]);
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf)
        return VFS_ROOT;
    *slash = 0;
    return intern(buf);
}

const char *vfs_name(int n)
{
    if (!valid(n) || n == VFS_ROOT)
        return "/";
    const char *slash = strrchr(paths[n], '/');
    return slash ? slash + 1 : paths[n];
}

struct entry {
    char name[256];
    int dir;
};

static int by_kind_then_name(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (x->dir != y->dir)
        return y->dir - x->dir; /* folders first */
    return strcasecmp(x->name, y->name);
}

/* A folder's contents, folders first, then by name.  Hidden files (names
 * starting with a dot) are left out, as file managers do by default. */
int vfs_list(int dir, int *out, int max)
{
    if (!valid(dir))
        return 0;
    /* Interning the children may reuse table slots, the folder's own
     * included, so work from a copy of its path. */
    char base[PATH_MAX];
    snprintf(base, sizeof base, "%s", dir == VFS_ROOT ? "" : paths[dir]);
    DIR *d = opendir(dir == VFS_ROOT ? "/" : base);
    if (!d)
        return 0;
    struct entry *list = malloc(sizeof *list * (size_t)max);
    int n = 0;
    struct dirent *de;
    while (list && n < max && (de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        snprintf(list[n].name, sizeof list[n].name, "%s", de->d_name);
        list[n].dir = de->d_type == DT_DIR;
        if (de->d_type == DT_LNK || de->d_type == DT_UNKNOWN) {
            struct stat st;
            char full[PATH_MAX + 260];
            snprintf(full, sizeof full, "%s/%s", base, de->d_name);
            list[n].dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        }
        n++;
    }
    closedir(d);
    if (!list)
        return 0;
    qsort(list, (size_t)n, sizeof *list, by_kind_then_name);
    int count = 0;
    for (int i = 0; i < n; i++) {
        char full[PATH_MAX + 260];
        snprintf(full, sizeof full, "%s/%s", base, list[i].name);
        if (strlen(full) < VFS_PATH_MAX)
            out[count++] = intern(full);
    }
    free(list);
    return count;
}

void vfs_path(int node, char *out, int size)
{
    k_strlcpy(out, valid(node) ? paths[node] : "/", size);
}

/* The in-memory system's usage counters mean nothing here. */
int vfs_used(void) { return 0; }
int vfs_bytes_used(void) { return 0; }
