/* vfs.c - the in-memory file system and the files a fresh system starts with. */
#include "vfs.h"
#include "lib.h"
#include "shell.h"

struct inode {
    char used;
    char is_dir;
    short mode;
    int parent;
    int size;
    char name[VFS_NAME_MAX];
};

static struct inode nodes[VFS_MAX_NODES];
/* File contents. Each node owns one fixed-size slot: simple, if wasteful. */
static char data[VFS_MAX_NODES][VFS_FILE_MAX];

const char *vfs_strerror(int err)
{
    switch (err) {
    case VFS_ENOENT: return "No such file or directory";
    case VFS_EEXIST: return "File exists";
    case VFS_ENOTDIR: return "Not a directory";
    case VFS_EISDIR: return "Is a directory";
    case VFS_EINVAL: return "Invalid argument";
    case VFS_ENOSPC: return "No space left on device";
    case VFS_ENOTEMPTY: return "Directory not empty";
    case VFS_EBUSY: return "Device or resource busy";
    }
    return "Unknown error";
}

static int valid(int n) { return n >= 0 && n < VFS_MAX_NODES && nodes[n].used; }

int vfs_is_dir(int n) { return valid(n) && nodes[n].is_dir; }
int vfs_size(int n) { return valid(n) ? nodes[n].size : 0; }
int vfs_mode(int n) { return valid(n) ? nodes[n].mode : 0; }
int vfs_parent(int n) { return valid(n) ? nodes[n].parent : VFS_ROOT; }
const char *vfs_name(int n) { return valid(n) ? nodes[n].name : "?"; }

static int find_child(int dir, const char *name, int len)
{
    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (nodes[i].used && nodes[i].parent == dir &&
            k_strlen(nodes[i].name) == len && k_strncmp(nodes[i].name, name, len) == 0)
            return i;
    }
    return VFS_ENOENT;
}

/* Walk path component by component, starting at cwd (or / for absolute paths). */
int vfs_lookup(int cwd, const char *path)
{
    int cur = valid(cwd) ? cwd : VFS_ROOT;
    if (path[0] == '/') {
        cur = VFS_ROOT;
    } else if (path[0] == '~' && (path[1] == '/' || path[1] == 0)) {
        cur = vfs_lookup(VFS_ROOT, "/home/user");
        path++;
    }
    while (*path) {
        while (*path == '/')
            path++;
        if (!*path)
            break;
        int len = 0;
        while (path[len] && path[len] != '/')
            len++;
        if (!nodes[cur].is_dir)
            return VFS_ENOTDIR;
        if (len == 1 && path[0] == '.') {
            /* stay */
        } else if (len == 2 && path[0] == '.' && path[1] == '.') {
            cur = nodes[cur].parent;
        } else {
            cur = find_child(cur, path, len);
            if (cur < 0)
                return cur;
        }
        path += len;
    }
    return cur;
}

/* Split "a/b/c" into the parent directory node (of "a/b") and the name "c". */
static int split(int cwd, const char *path, char *name)
{
    char buf[VFS_PATH_MAX];
    k_strlcpy(buf, path, sizeof buf);
    int len = k_strlen(buf);
    while (len > 1 && buf[len - 1] == '/')
        buf[--len] = 0;

    int slash = -1;
    for (int i = 0; buf[i]; i++)
        if (buf[i] == '/')
            slash = i;

    const char *base = buf + slash + 1;
    if (!*base || k_strcmp(base, ".") == 0 || k_strcmp(base, "..") == 0)
        return VFS_EINVAL;
    if (k_strlen(base) >= VFS_NAME_MAX)
        return VFS_EINVAL;
    k_strlcpy(name, base, VFS_NAME_MAX);

    int parent;
    if (slash < 0) {
        parent = vfs_lookup(cwd, ".");
    } else if (slash == 0) {
        parent = VFS_ROOT;
    } else {
        buf[slash] = 0;
        parent = vfs_lookup(cwd, buf);
    }
    if (parent < 0)
        return parent;
    if (!nodes[parent].is_dir)
        return VFS_ENOTDIR;
    return parent;
}

static int alloc_node(int parent, const char *name, int is_dir, int mode)
{
    for (int i = 1; i < VFS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            k_memset(&nodes[i], 0, sizeof nodes[i]);
            nodes[i].used = 1;
            nodes[i].is_dir = (char)is_dir;
            nodes[i].mode = (short)mode;
            nodes[i].parent = parent;
            k_strlcpy(nodes[i].name, name, VFS_NAME_MAX);
            return i;
        }
    }
    return VFS_ENOSPC;
}

int vfs_create(int cwd, const char *path, int is_dir)
{
    char name[VFS_NAME_MAX];
    int parent = split(cwd, path, name);
    if (parent < 0)
        return parent;
    if (find_child(parent, name, k_strlen(name)) >= 0)
        return VFS_EEXIST;
    return alloc_node(parent, name, is_dir, is_dir ? 0755 : 0644);
}

int vfs_remove(int n)
{
    if (!valid(n))
        return VFS_ENOENT;
    if (n == VFS_ROOT)
        return VFS_EBUSY;
    if (nodes[n].is_dir)
        for (int i = 1; i < VFS_MAX_NODES; i++)
            if (nodes[i].used && nodes[i].parent == n)
                return VFS_ENOTEMPTY;
    nodes[n].used = 0;
    return 0;
}

int vfs_rename(int n, int new_parent, const char *new_name)
{
    if (!valid(n) || !valid(new_parent))
        return VFS_ENOENT;
    if (n == VFS_ROOT)
        return VFS_EBUSY;
    if (!nodes[new_parent].is_dir)
        return VFS_ENOTDIR;
    int len = k_strlen(new_name);
    if (len == 0 || len >= VFS_NAME_MAX || k_strchr(new_name, '/'))
        return VFS_EINVAL;
    /* A directory cannot be moved inside itself. */
    for (int p = new_parent; ; p = nodes[p].parent) {
        if (p == n)
            return VFS_EINVAL;
        if (p == VFS_ROOT)
            break;
    }
    int existing = find_child(new_parent, new_name, len);
    if (existing >= 0 && existing != n)
        return VFS_EEXIST;
    nodes[n].parent = new_parent;
    k_strlcpy(nodes[n].name, new_name, VFS_NAME_MAX);
    return 0;
}

int vfs_read(int n, char *buf, int max)
{
    if (!valid(n))
        return VFS_ENOENT;
    if (nodes[n].is_dir)
        return VFS_EISDIR;
    int len = MIN(nodes[n].size, max);
    k_memcpy(buf, data[n], len);
    return len;
}

int vfs_write(int n, const char *src, int len, int append)
{
    if (!valid(n))
        return VFS_ENOENT;
    if (nodes[n].is_dir)
        return VFS_EISDIR;
    int start = append ? nodes[n].size : 0;
    if (start + len > VFS_FILE_MAX)
        return VFS_ENOSPC;
    k_memcpy(data[n] + start, src, len);
    nodes[n].size = start + len;
    return len;
}

int vfs_list(int dir, int *out, int max)
{
    int count = 0;
    for (int i = 1; i < VFS_MAX_NODES && count < max; i++)
        if (nodes[i].used && nodes[i].parent == dir)
            out[count++] = i;
    /* Insertion sort: directories first, then by name. */
    for (int i = 1; i < count; i++) {
        int v = out[i], j = i - 1;
        while (j >= 0) {
            int a = out[j];
            int before = nodes[v].is_dir != nodes[a].is_dir
                             ? nodes[v].is_dir
                             : k_strcmp(nodes[v].name, nodes[a].name) < 0;
            if (!before)
                break;
            out[j + 1] = a;
            j--;
        }
        out[j + 1] = v;
    }
    return count;
}

void vfs_path(int n, char *out, int size)
{
    if (!valid(n) || n == VFS_ROOT) {
        k_strlcpy(out, "/", size);
        return;
    }
    int chain[32], depth = 0;
    for (int p = n; p != VFS_ROOT && depth < 32; p = nodes[p].parent)
        chain[depth++] = p;
    out[0] = 0;
    while (depth-- > 0) {
        k_strlcat(out, "/", size);
        k_strlcat(out, nodes[chain[depth]].name, size);
    }
}

int vfs_used(void)
{
    int c = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++)
        c += nodes[i].used;
    return c;
}

int vfs_bytes_used(void)
{
    int c = 0;
    for (int i = 0; i < VFS_MAX_NODES; i++)
        if (nodes[i].used)
            c += nodes[i].size;
    return c;
}

/* ---- the initial file tree ---------------------------------------------- */

static void mkfile(const char *path, const char *text, int mode)
{
    int n = vfs_create(VFS_ROOT, path, 0);
    if (n < 0)
        return;
    nodes[n].mode = (short)mode;
    vfs_write(n, text, k_strlen(text), 0);
}

void vfs_init(void)
{
    k_memset(nodes, 0, sizeof nodes);
    nodes[0].used = 1;
    nodes[0].is_dir = 1;
    nodes[0].mode = 0755;
    nodes[0].parent = VFS_ROOT;
    k_strlcpy(nodes[0].name, "/", VFS_NAME_MAX);

    static const char *const dirs[] = {
        "/bin", "/etc", "/home", "/home/user", "/home/user/Desktop",
        "/home/user/Documents", "/home/user/Music", "/root", "/tmp", "/usr",
        "/usr/share", "/usr/share/wallpapers",
    };
    for (int i = 0; i < ARRAY_LEN(dirs); i++)
        vfs_create(VFS_ROOT, dirs[i], 1);
    nodes[vfs_lookup(VFS_ROOT, "/tmp")].mode = 01777;
    nodes[vfs_lookup(VFS_ROOT, "/root")].mode = 0700;

    /* Every shell built-in also appears as a file in /bin, like on UNIX. */
    for (int i = 0; g_shell_commands[i]; i++) {
        char path[40];
        k_snprintf(path, sizeof path, "/bin/%s", g_shell_commands[i]);
        mkfile(path, "#!builtin\n", 0755);
    }

    mkfile("/etc/hostname", "skarlet\n", 0644);
    mkfile("/etc/os-release",
           "NAME=\"SkarletOS\"\nVERSION=\"0.1\"\nID=skarletos\n"
           "PRETTY_NAME=\"SkarletOS 0.1 (x86-64)\"\n", 0644);
    mkfile("/etc/passwd",
           "root:x:0:0:root:/root:/bin/sh\n"
           "user:x:1000:1000:SkarletOS User:/home/user:/bin/sh\n", 0644);
    mkfile("/etc/motd",
           "Welcome to SkarletOS, a toy UNIX-like system with a desktop\n"
           "modelled on the 2012 KDE Plasma 4 workspace.\n"
           "Type 'help' to see the commands.\n", 0644);
    mkfile("/home/user/Desktop/README.txt",
           "SkarletOS desktop quick start\n"
           "-----------------------------\n"
           "Alt+F1   application launcher (Skarlet Launcher)\n"
           "Alt+F2   Skarlet Runner: run commands, do maths\n"
           "Alt+F12  desktop toolbox: widgets and activities\n"
           "Alt+Tab  next window, Alt+F4 close window\n"
           "Ctrl+F1..F4  switch virtual desktop\n"
           "Tab      on the desktop: focus the next widget\n"
           "Delete   remove the focused widget (when unlocked)\n", 0644);
    mkfile("/home/user/Desktop/todo.txt",
           "- read src/vfs.c to see how files work\n"
           "- add a new shell command in src/shell.c\n"
           "- write a new plasmoid in src/plasmoids.c\n", 0644);
    mkfile("/home/user/Documents/plasma-notes.txt",
           "Plasma 4 ideas this desktop copies:\n"
           "* everything on the desktop and panel is a widget (plasmoid)\n"
           "* widgets live in containments (the desktop, the panel)\n"
           "* activities: separate sets of widgets for separate tasks\n"
           "* KRunner (Skarlet Runner here): one box that launches, calculates, runs\n", 0644);
    mkfile("/home/user/.profile", "# shell startup file (not executed yet)\n", 0644);
}
