#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <libinput.h>
#include <swc.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))
#define TAGMASK ((1u << LENGTH(tags)) - 1u)
#define ISVISIBLE(c) ((c)->tags & (c)->scr->tagset)

#define MODKEY SWC_MOD_LOGO
#define SHIFT  SWC_MOD_SHIFT
#define CTRL   SWC_MOD_CTRL

union arg {
    int i;
    uint32_t u;
    const void *v;
};

struct bind {
    uint32_t type;
    uint32_t mods;
    uint32_t key;
    union arg arg;
    void (*fn)(void *, uint32_t, uint32_t, uint32_t);
};

struct screen;

struct client {
    struct wl_list link;
    struct swc_window *win;
    struct screen *scr;
    uint32_t tags;
};

struct screen {
    struct wl_list link;
    struct swc_screen *scr;
    uint32_t tagset;
};

static struct {
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wl_list screens;
    struct wl_list clients;
    struct screen *sel_screen;
    struct client *sel_client;
} wm;

/* appearance */
static const unsigned int borderpx = 1;
static const unsigned int gaps_outer = 8;
static const unsigned int gaps_inner = 6;
static const unsigned int master_percent = 60;
static const uint32_t bordercolor = 0xff444444;
static const uint32_t focuscolor  = 0xff005577;

/* dwm-style tags */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static const char *termcmd[] = { "foot", NULL };

static void arrange(struct screen *s);
static void focus(struct client *c);
static void sync_visibility(struct screen *s);

static void spawn(void *data, uint32_t time, uint32_t value, uint32_t state);
static void quit(void *data, uint32_t time, uint32_t value, uint32_t state);
static void kill_sel(void *data, uint32_t time, uint32_t value, uint32_t state);
static void focus_next(void *data, uint32_t time, uint32_t value, uint32_t state);
static void focus_prev(void *data, uint32_t time, uint32_t value, uint32_t state);
static void view(void *data, uint32_t time, uint32_t value, uint32_t state);
static void toggleview(void *data, uint32_t time, uint32_t value, uint32_t state);
static void tag(void *data, uint32_t time, uint32_t value, uint32_t state);
static void toggletag(void *data, uint32_t time, uint32_t value, uint32_t state);

#define TAGKEYS(KEY, TAG) \
    { SWC_BINDING_KEY, MODKEY,                  KEY, { .u = 1u << (TAG) }, view       }, \
    { SWC_BINDING_KEY, MODKEY|CTRL,             KEY, { .u = 1u << (TAG) }, toggleview }, \
    { SWC_BINDING_KEY, MODKEY|SHIFT,            KEY, { .u = 1u << (TAG) }, tag        }, \
    { SWC_BINDING_KEY, MODKEY|CTRL|SHIFT,       KEY, { .u = 1u << (TAG) }, toggletag  }

static struct bind binds[] = {
    { SWC_BINDING_KEY, MODKEY,       XKB_KEY_t, { .v = termcmd }, spawn },
    { SWC_BINDING_KEY, MODKEY,       XKB_KEY_j, { .v = NULL }, focus_next },
    { SWC_BINDING_KEY, MODKEY,       XKB_KEY_k, { .v = NULL }, focus_prev },
    { SWC_BINDING_KEY, MODKEY|SHIFT, XKB_KEY_q, { .v = NULL }, kill_sel },
    { SWC_BINDING_KEY, MODKEY|SHIFT, XKB_KEY_e, { .v = NULL }, quit },

    { SWC_BINDING_KEY, MODKEY,       XKB_KEY_0, { .u = TAGMASK }, view },
    { SWC_BINDING_KEY, MODKEY|SHIFT, XKB_KEY_0, { .u = TAGMASK }, tag },

    TAGKEYS(XKB_KEY_1, 0),
    TAGKEYS(XKB_KEY_2, 1),
    TAGKEYS(XKB_KEY_3, 2),
    TAGKEYS(XKB_KEY_4, 3),
    TAGKEYS(XKB_KEY_5, 4),
    TAGKEYS(XKB_KEY_6, 5),
    TAGKEYS(XKB_KEY_7, 6),
    TAGKEYS(XKB_KEY_8, 7),
    TAGKEYS(XKB_KEY_9, 8),
};

static bool
visible_on(const struct client *c, const struct screen *s)
{
    return c->scr == s && (c->tags & s->tagset);
}

static struct client *
first_visible(struct screen *s)
{
    struct client *c;

    wl_list_for_each(c, &wm.clients, link)
        if (visible_on(c, s))
            return c;

    return NULL;
}

static void
focus(struct client *c)
{
    if (wm.sel_client)
        swc_window_set_border(wm.sel_client->win, bordercolor, borderpx, 0, 0);

    if (c)
        swc_window_set_border(c->win, focuscolor, borderpx, 0, 0);

    swc_window_focus(c ? c->win : NULL);
    wm.sel_client = c;
    if (c)
        wm.sel_screen = c->scr;
}

static void
sync_visibility(struct screen *s)
{
    struct client *c;

    wl_list_for_each(c, &wm.clients, link) {
        if (c->scr != s)
            continue;
        if (visible_on(c, s))
            swc_window_show(c->win);
        else
            swc_window_hide(c->win);
    }
}

static void
arrange(struct screen *s)
{
    struct client *c;
    struct swc_rectangle geom;
    const struct swc_rectangle *area;
    size_t n = 0, i = 0;
    int32_t x, y;
    uint32_t w, h, master_w, stack_h, stack_n;

    if (!s)
        return;

    wl_list_for_each(c, &wm.clients, link)
        if (visible_on(c, s))
            n++;

    if (n == 0)
        return;

    area = &s->scr->usable_geometry;
    x = area->x + (int32_t)gaps_outer;
    y = area->y + (int32_t)gaps_outer;
    w = area->width  > gaps_outer * 2 ? area->width  - gaps_outer * 2 : area->width;
    h = area->height > gaps_outer * 2 ? area->height - gaps_outer * 2 : area->height;

    if (n == 1) {
        c = first_visible(s);
        if (!c)
            return;
        geom.x = x;
        geom.y = y;
        geom.width = w;
        geom.height = h;
        swc_window_set_geometry(c->win, &geom);
        return;
    }

    master_w = ((w - gaps_inner) * master_percent) / 100;
    stack_n = (uint32_t)n - 1;
    stack_h = (h - gaps_inner * (stack_n - 1)) / stack_n;

    wl_list_for_each(c, &wm.clients, link) {
        if (!visible_on(c, s))
            continue;

        if (i == 0) {
            geom.x = x;
            geom.y = y;
            geom.width = master_w;
            geom.height = h;
        } else {
            uint32_t si = (uint32_t)i - 1;
            geom.x = x + (int32_t)master_w + (int32_t)gaps_inner;
            geom.y = y + (int32_t)(si * (stack_h + gaps_inner));
            geom.width = w - master_w - gaps_inner;
            geom.height = stack_h;
        }

        swc_window_set_geometry(c->win, &geom);
        i++;
    }
}

static void
on_window_destroy(void *data)
{
    struct client *c = data;
    struct screen *s;

    if (!c)
        return;

    s = c->scr;
    if (wm.sel_client == c)
        wm.sel_client = NULL;

    wl_list_remove(&c->link);
    free(c);

    if (!wm.sel_client)
        focus(first_visible(s));
    arrange(s);
}

static void
on_window_entered(void *data)
{
    struct client *c = data;
    if (c && ISVISIBLE(c))
        focus(c);
}

static struct swc_window_handler window_handler = {
    .destroy = on_window_destroy,
    .entered = on_window_entered,
};

static void
on_screen_destroy(void *data)
{
    struct screen *s = data;
    if (!s)
        return;

    wl_list_remove(&s->link);
    if (wm.sel_screen == s)
        wm.sel_screen = wl_list_empty(&wm.screens) ? NULL :
            wl_container_of(wm.screens.next, wm.sel_screen, link);
    free(s);
}

static void
on_screen_usable_geometry_changed(void *data)
{
    arrange(data);
}

static struct swc_screen_handler screen_handler = {
    .destroy = on_screen_destroy,
    .usable_geometry_changed = on_screen_usable_geometry_changed,
};

static void
new_screen(struct swc_screen *scr)
{
    struct screen *s = calloc(1, sizeof(*s));
    if (!s) {
        perror("calloc screen");
        exit(EXIT_FAILURE);
    }

    s->scr = scr;
    s->tagset = 1u;
    wl_list_insert(wm.screens.prev, &s->link);
    swc_screen_set_handler(scr, &screen_handler, s);

    if (!wm.sel_screen)
        wm.sel_screen = s;
}

static void
new_window(struct swc_window *win)
{
    struct client *c;

    if (!wm.sel_screen)
        return;

    c = calloc(1, sizeof(*c));
    if (!c) {
        perror("calloc client");
        exit(EXIT_FAILURE);
    }

    c->win = win;
    c->scr = wm.sel_screen;
    c->tags = c->scr->tagset;

    wl_list_insert(wm.clients.prev, &c->link);
    swc_window_set_handler(win, &window_handler, c);
    swc_window_set_tiled(win);
    swc_window_show(win);

    focus(c);
    arrange(c->scr);
}

static void
new_device(struct libinput_device *dev)
{
    (void)dev;
}

static const struct swc_manager manager = {
    .new_screen = new_screen,
    .new_window = new_window,
    .new_device = new_device,
};

static bool
pressed(uint32_t state)
{
    return state == WL_KEYBOARD_KEY_STATE_PRESSED;
}

static void
spawn(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    union arg *a = data;
    char *const *cmd = (char *const *)a->v;
    (void)time;
    (void)value;

    if (!pressed(state))
        return;

    if (fork() == 0) {
        setsid();
        execvp(cmd[0], cmd);
        perror(cmd[0]);
        _exit(127);
    }
}

static void
quit(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    (void)data;
    (void)time;
    (void)value;
    if (pressed(state))
        wl_display_terminate(wm.display);
}

static void
kill_sel(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    (void)data;
    (void)time;
    (void)value;
    if (pressed(state) && wm.sel_client)
        swc_window_close(wm.sel_client->win);
}

static void
focus_next(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    struct client *c;
    bool seen = false;
    (void)data;
    (void)time;
    (void)value;

    if (!pressed(state) || !wm.sel_screen)
        return;

    if (!wm.sel_client) {
        focus(first_visible(wm.sel_screen));
        return;
    }

    wl_list_for_each(c, &wm.clients, link) {
        if (!visible_on(c, wm.sel_screen))
            continue;
        if (seen) {
            focus(c);
            return;
        }
        if (c == wm.sel_client)
            seen = true;
    }

    focus(first_visible(wm.sel_screen));
}

static void
focus_prev(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    struct client *c, *last = NULL;
    (void)data;
    (void)time;
    (void)value;

    if (!pressed(state) || !wm.sel_screen)
        return;

    wl_list_for_each(c, &wm.clients, link) {
        if (!visible_on(c, wm.sel_screen))
            continue;
        if (c == wm.sel_client) {
            if (last) {
                focus(last);
                return;
            }
            break;
        }
        last = c;
    }

    last = NULL;
    wl_list_for_each(c, &wm.clients, link)
        if (visible_on(c, wm.sel_screen))
            last = c;
    focus(last);
}

static void
view(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    union arg *a = data;
    uint32_t newtags;
    (void)time;
    (void)value;

    if (!pressed(state) || !wm.sel_screen)
        return;

    newtags = a->u & TAGMASK;
    if (!newtags)
        return;

    wm.sel_screen->tagset = newtags;
    sync_visibility(wm.sel_screen);

    if (!wm.sel_client || !visible_on(wm.sel_client, wm.sel_screen))
        focus(first_visible(wm.sel_screen));
    arrange(wm.sel_screen);
}

static void
toggleview(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    union arg *a = data;
    uint32_t newtags;
    (void)time;
    (void)value;

    if (!pressed(state) || !wm.sel_screen)
        return;

    newtags = wm.sel_screen->tagset ^ (a->u & TAGMASK);
    if (!newtags)
        return;

    wm.sel_screen->tagset = newtags;
    sync_visibility(wm.sel_screen);

    if (!wm.sel_client || !visible_on(wm.sel_client, wm.sel_screen))
        focus(first_visible(wm.sel_screen));
    arrange(wm.sel_screen);
}

static void
tag(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    union arg *a = data;
    uint32_t newtags;
    struct screen *s;
    (void)time;
    (void)value;

    if (!pressed(state) || !wm.sel_client)
        return;

    newtags = a->u & TAGMASK;
    if (!newtags)
        return;

    s = wm.sel_client->scr;
    wm.sel_client->tags = newtags;
    sync_visibility(s);
    focus(first_visible(s));
    arrange(s);
}

static void
toggletag(void *data, uint32_t time, uint32_t value, uint32_t state)
{
    union arg *a = data;
    uint32_t newtags;
    struct screen *s;
    (void)time;
    (void)value;

    if (!pressed(state) || !wm.sel_client)
        return;

    newtags = wm.sel_client->tags ^ (a->u & TAGMASK);
    if (!newtags)
        return;

    s = wm.sel_client->scr;
    wm.sel_client->tags = newtags;
    sync_visibility(s);
    if (!visible_on(wm.sel_client, s))
        focus(first_visible(s));
    arrange(s);
}

static void
handle_signal(int sig)
{
    (void)sig;
    if (wm.display)
        wl_display_terminate(wm.display);
}

int
main(void)
{
    const char *socket;
    size_t i;

    wl_list_init(&wm.screens);
    wl_list_init(&wm.clients);

    wm.display = wl_display_create();
    if (!wm.display) {
        fprintf(stderr, "minidwc: wl_display_create failed\n");
        return EXIT_FAILURE;
    }

    wm.event_loop = wl_display_get_event_loop(wm.display);
    if (!swc_initialize(wm.display, wm.event_loop, &manager)) {
        fprintf(stderr, "minidwc: swc_initialize failed\n");
        wl_display_destroy(wm.display);
        return EXIT_FAILURE;
    }

    for (i = 0; i < LENGTH(binds); i++)
        swc_add_binding(binds[i].type, binds[i].mods, binds[i].key,
                        binds[i].fn, &binds[i].arg);

    socket = wl_display_add_socket_auto(wm.display);
    if (!socket) {
        fprintf(stderr, "minidwc: wl_display_add_socket_auto failed\n");
        swc_finalize();
        wl_display_destroy(wm.display);
        return EXIT_FAILURE;
    }

    setenv("WAYLAND_DISPLAY", socket, 1);
    fprintf(stderr, "minidwc: WAYLAND_DISPLAY=%s\n", socket);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    wl_display_run(wm.display);

    swc_finalize();
    wl_display_destroy(wm.display);
    return EXIT_SUCCESS;
}
