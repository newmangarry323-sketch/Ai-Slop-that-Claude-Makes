/* testapp.c - a minimal X11 program for testing the SkarletOS session: a
 * window that turns green when it gets a key and blue when clicked, and
 * reports both on standard output.  It closes politely (WM_DELETE_WINDOW). */
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int main(int argc, char **argv)
{
    const char *title = argc > 1 ? argv[1] : "Test App";
    Display *dpy = XOpenDisplay(0);
    if (!dpy)
        return 1;
    int s = DefaultScreen(dpy);
    Window w = XCreateSimpleWindow(dpy, RootWindow(dpy, s), 50, 50, 400, 300, 0, 0, 0xc0c0c0);
    XStoreName(dpy, w, title);
    Atom del = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, w, &del, 1);
    XSelectInput(dpy, w, KeyPressMask | ButtonPressMask | ExposureMask);
    XMapWindow(dpy, w);
    setvbuf(stdout, 0, _IONBF, 0);
    for (;;) {
        XEvent e;
        XNextEvent(dpy, &e);
        if (e.type == KeyPress) {
            char buf[8] = { 0 };
            KeySym ks;
            XLookupString(&e.xkey, buf, sizeof buf - 1, &ks, 0);
            printf("key %s\n", XKeysymToString(ks));
            XSetWindowBackground(dpy, w, 0x20a040);
            XClearWindow(dpy, w);
        } else if (e.type == ButtonPress) {
            printf("click %d %d\n", e.xbutton.x, e.xbutton.y);
            XSetWindowBackground(dpy, w, 0x2050c0);
            XClearWindow(dpy, w);
        } else if (e.type == ClientMessage && (Atom)e.xclient.data.l[0] == del) {
            printf("closed\n");
            return 0;
        }
    }
}
