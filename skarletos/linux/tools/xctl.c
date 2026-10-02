/* xctl.c - drive an X display from scripts, for testing the SkarletOS session.
 *
 *   xctl shot FILE.ppm          save the screen
 *   xctl move X Y               move the pointer
 *   xctl click X Y [BUTTON]     press and release a button there
 *   xctl dclick X Y             double click
 *   xctl drag X1 Y1 X2 Y2       press at one point, release at the other
 *   xctl key COMBO...           e.g. alt+F1, Return, ctrl+c, super_l
 *   xctl type TEXT              type ASCII text
 *   xctl find TITLE             print "x y w h" of a managed window (by title part)
 *   xctl wait TITLE SECONDS     wait until such a window exists
 *
 * Input goes through the XTEST extension, so it arrives exactly like real
 * keyboard and mouse input (grabs and focus included).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>

/* From <X11/extensions/XTest.h> (libXtst). */
Bool XTestFakeKeyEvent(Display *, unsigned int, Bool, unsigned long);
Bool XTestFakeButtonEvent(Display *, unsigned int, Bool, unsigned long);
Bool XTestFakeMotionEvent(Display *, int, int, int, unsigned long);

static Display *dpy;

static void pause_ms(int ms)
{
    XSync(dpy, False);
    usleep((useconds_t)ms * 1000);
}

static void move(int x, int y)
{
    XTestFakeMotionEvent(dpy, DefaultScreen(dpy), x, y, CurrentTime);
    pause_ms(30);
}

static void button(int b, int down)
{
    XTestFakeButtonEvent(dpy, (unsigned)b, down, CurrentTime);
    pause_ms(30);
}

static void key_combo(const char *combo)
{
    char buf[64], *parts[8];
    int n = 0;
    snprintf(buf, sizeof buf, "%s", combo);
    for (char *p = strtok(buf, "+"); p && n < 8; p = strtok(0, "+"))
        parts[n++] = p;
    KeyCode codes[8];
    for (int i = 0; i < n; i++) {
        const char *name = parts[i];
        if (!strcmp(name, "alt")) name = "Alt_L";
        else if (!strcmp(name, "ctrl")) name = "Control_L";
        else if (!strcmp(name, "shift")) name = "Shift_L";
        else if (!strcmp(name, "super_l")) name = "Super_L";
        KeySym ks = XStringToKeysym(name);
        codes[i] = XKeysymToKeycode(dpy, ks);
        if (!codes[i]) {
            fprintf(stderr, "xctl: unknown key %s\n", parts[i]);
            exit(1);
        }
    }
    for (int i = 0; i < n; i++)
        XTestFakeKeyEvent(dpy, codes[i], True, CurrentTime);
    for (int i = n - 1; i >= 0; i--)
        XTestFakeKeyEvent(dpy, codes[i], False, CurrentTime);
    pause_ms(60);
}

static void type_text(const char *s)
{
    for (; *s; s++) {
        KeySym ks = (KeySym)(unsigned char)*s;
        if (*s == ' ') ks = XK_space;
        else if (*s == '\n') ks = XK_Return;
        KeyCode kc = XKeysymToKeycode(dpy, ks);
        int shift = 0;
        if (kc && XkbKeycodeToKeysym(dpy, kc, 0, 0) != ks)
            shift = 1; /* e.g. capitals and '*' need Shift */
        KeyCode sh = XKeysymToKeycode(dpy, XK_Shift_L);
        if (shift)
            XTestFakeKeyEvent(dpy, sh, True, CurrentTime);
        XTestFakeKeyEvent(dpy, kc, True, CurrentTime);
        XTestFakeKeyEvent(dpy, kc, False, CurrentTime);
        if (shift)
            XTestFakeKeyEvent(dpy, sh, False, CurrentTime);
        pause_ms(25);
    }
}

static void shot(const char *path)
{
    Window root = DefaultRootWindow(dpy);
    XWindowAttributes wa;
    XGetWindowAttributes(dpy, root, &wa);
    XImage *img = XGetImage(dpy, root, 0, 0, (unsigned)wa.width, (unsigned)wa.height, AllPlanes,
                            ZPixmap);
    FILE *f = fopen(path, "wb");
    if (!f || !img) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P6\n%d %d\n255\n", wa.width, wa.height);
    for (int y = 0; y < wa.height; y++)
        for (int x = 0; x < wa.width; x++) {
            unsigned long p = XGetPixel(img, x, y);
            unsigned char rgb[3] = { (unsigned char)(p >> 16), (unsigned char)(p >> 8),
                                     (unsigned char)p };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* Find a managed window whose title contains text; print its frame geometry. */
static int find(const char *text, int print)
{
    Atom list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    Atom name = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom type;
    int format;
    unsigned long n = 0, after;
    unsigned char *data = 0;
    Window root = DefaultRootWindow(dpy);
    if (XGetWindowProperty(dpy, root, list, 0, 1024, False, XA_WINDOW, &type, &format, &n, &after,
                           &data) != Success || !data)
        return 0;
    int found = 0;
    for (unsigned long i = 0; i < n && !found; i++) {
        Window w = ((Window *)data)[i];
        unsigned char *t = 0;
        unsigned long tn = 0;
        char *plain = 0;
        const char *title = "";
        if (XGetWindowProperty(dpy, w, name, 0, 256, False, utf8, &type, &format, &tn, &after,
                               &t) == Success && t)
            title = (char *)t;
        else if (XFetchName(dpy, w, &plain) && plain)
            title = plain;
        if (strstr(title, text)) {
            found = 1;
            if (print) {
                XWindowAttributes wa;
                Window child;
                int x, y;
                XGetWindowAttributes(dpy, w, &wa);
                XTranslateCoordinates(dpy, w, root, 0, 0, &x, &y, &child);
                printf("%d %d %d %d\n", x, y, wa.width, wa.height);
            }
        }
        if (t)
            XFree(t);
        if (plain)
            XFree(plain);
    }
    XFree(data);
    return found;
}

int main(int argc, char **argv)
{
    if (argc < 2 || !(dpy = XOpenDisplay(0))) {
        fprintf(stderr, "usage: xctl shot|move|click|dclick|drag|key|type|find|wait ...\n");
        return 2;
    }
    const char *c = argv[1];
    if (!strcmp(c, "shot") && argc == 3) {
        shot(argv[2]);
    } else if (!strcmp(c, "move") && argc == 4) {
        move(atoi(argv[2]), atoi(argv[3]));
    } else if (!strcmp(c, "click") && argc >= 4) {
        int b = argc > 4 ? atoi(argv[4]) : 1;
        move(atoi(argv[2]), atoi(argv[3]));
        button(b, True);
        button(b, False);
    } else if (!strcmp(c, "dclick") && argc == 4) {
        move(atoi(argv[2]), atoi(argv[3]));
        for (int i = 0; i < 2; i++) {
            button(1, True);
            button(1, False);
        }
    } else if (!strcmp(c, "drag") && argc == 6) {
        int x1 = atoi(argv[2]), y1 = atoi(argv[3]), x2 = atoi(argv[4]), y2 = atoi(argv[5]);
        move(x1, y1);
        button(1, True);
        for (int i = 1; i <= 8; i++)
            move(x1 + (x2 - x1) * i / 8, y1 + (y2 - y1) * i / 8);
        button(1, False);
    } else if (!strcmp(c, "key")) {
        for (int i = 2; i < argc; i++)
            key_combo(argv[i]);
    } else if (!strcmp(c, "type") && argc == 3) {
        type_text(argv[2]);
    } else if (!strcmp(c, "find") && argc == 3) {
        return find(argv[2], 1) ? 0 : 1;
    } else if (!strcmp(c, "wait") && argc == 4) {
        for (int i = 0; i < atoi(argv[3]) * 10; i++) {
            if (find(argv[2], 0))
                return 0;
            usleep(100000);
        }
        fprintf(stderr, "xctl: no window \"%s\"\n", argv[2]);
        return 1;
    } else {
        fprintf(stderr, "xctl: bad arguments\n");
        return 2;
    }
    XSync(dpy, False);
    return 0;
}
