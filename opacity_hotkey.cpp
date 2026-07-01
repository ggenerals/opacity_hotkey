/*
 * opacity_hotkey.cpp
 * 窗口透明度快捷键管理器 —— 最小化至系统托盘
 *
 * 快捷键：
 *   Ctrl+Shift+1  →  5% 不透明（几乎透明）
 *   Ctrl+Shift+2  →  15%
 *   ...依次 +10%
 *   Ctrl+Shift+0  →  95%
 *   Ctrl+Shift+-  →  100% 复原
 *
 * 编译：
 *   g++ -O2 -o opacity_hotkey opacity_hotkey.cpp \
 *       $(pkg-config --cflags --libs gtk+-3.0 x11) -std=c++17
 *
 * 注意：需要 X11 会话（登录界面选择 "Ubuntu on Xorg"）。
 *       GNOME 需安装 AppIndicator 扩展或 TopIcons Plus 才能看到托盘图标。
 */

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <cstdint>
#include <cstdio>
#include <vector>

// ─── 全局状态 ────────────────────────────────────────────────
static Display*  xdisplay    = nullptr;
static Window    root_window = None;

struct KeyBind {
    KeyCode     keycode;
    int         opacity_pct;   // -1 = 复原(100%)
    const char* label;
};
static std::vector<KeyBind> g_keybinds;

// ─── 工具函数 ────────────────────────────────────────────────

/// 忽略 Lock / NumLock / Mod5，grab 所有组合
static const unsigned int IGNORED_MODS[] = {
    0,
    LockMask,                       // CapsLock
    Mod2Mask,                       // NumLock
    LockMask | Mod2Mask,
    Mod5Mask,                       // ScrollLock
    LockMask | Mod5Mask,
    Mod2Mask | Mod5Mask,
    LockMask | Mod2Mask | Mod5Mask,
};

static void grab_all(Display* dpy, Window root, KeyCode kc, unsigned int base) {
    for (unsigned int m : IGNORED_MODS)
        XGrabKey(dpy, kc, base | m, root, True, GrabModeAsync, GrabModeAsync);
}

static void ungrab_all(Display* dpy, Window root, KeyCode kc, unsigned int base) {
    for (unsigned int m : IGNORED_MODS)
        XUngrabKey(dpy, kc, base | m, root);
}

/// 获取当前活动顶层窗口（_NET_ACTIVE_WINDOW）
static Window get_active_window() {
    Atom atom = XInternAtom(xdisplay, "_NET_ACTIVE_WINDOW", False);
    Atom actual_type;
    int  actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop = nullptr;
    Window win = None;

    if (XGetWindowProperty(xdisplay, root_window, atom,
                           0, 1, False, XA_WINDOW,
                           &actual_type, &actual_format,
                           &nitems, &bytes_after, &prop) == Success
        && prop && nitems > 0)
    {
        win = *reinterpret_cast<Window*>(prop);
        XFree(prop);
    }
    return win;
}

/// 设置 _NET_WM_WINDOW_OPACITY
static void set_opacity(Window win, uint32_t opacity) {
    Atom atom = XInternAtom(xdisplay, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(xdisplay, win, atom, XA_CARDINAL, 32,
                    PropModeReplace,
                    reinterpret_cast<unsigned char*>(&opacity), 1);
    XFlush(xdisplay);
}

// ─── X11 事件过滤器（在 GDK 事件循环中拦截 KeyPress）────────

static GdkFilterReturn
x11_filter(GdkXEvent* xevent, GdkEvent* /*gdk_event*/, gpointer /*data*/)
{
    auto* xev = static_cast<XEvent*>(xevent);
    if (xev->type != KeyPress)
        return GDK_FILTER_CONTINUE;

    // 只关心 Ctrl + Shift
    unsigned int mods = xev->xkey.state & (ControlMask | ShiftMask);
    if (mods != (ControlMask | ShiftMask))
        return GDK_FILTER_CONTINUE;

    for (const auto& kb : g_keybinds) {
        if (xev->xkey.keycode != kb.keycode)
            continue;

        Window active = get_active_window();
        if (active == None)
            break;

        uint32_t val;
        int pct;
        if (kb.opacity_pct < 0) {          // 复原
            val = 0xFFFFFFFFu;
            pct = 100;
        } else {
            // 百分比 → 32-bit 不透明度值
            val = static_cast<uint32_t>(
                      static_cast<double>(0xFFFFFFFFu) * kb.opacity_pct / 100.0);
            pct = kb.opacity_pct;
        }

        set_opacity(active, val);
        // printf("[Opacity] 窗口 0x%lx → %d%%  (0x%08X)\n", active, pct, val);
        return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}

// ─── 托盘菜单 ────────────────────────────────────────────────

static void on_quit(GtkMenuItem* /*item*/, gpointer /*data*/) {
    const unsigned int base = ControlMask | ShiftMask;
    for (const auto& kb : g_keybinds)
        ungrab_all(xdisplay, root_window, kb.keycode, base);
    gtk_main_quit();
}

static void on_tray_popup(GtkStatusIcon* icon, guint button,
                          guint time, gpointer /*data*/)
{
    GtkWidget* menu = gtk_menu_new();

    // ── 快捷键说明（只读） ──
    for (const auto& kb : g_keybinds) {
        GtkWidget* info = gtk_menu_item_new_with_label(kb.label);
        gtk_widget_set_sensitive(info, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), info);
    }

    // ── 分隔线 ──
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());

    // ── 退出 ──
    GtkWidget* quit_item = gtk_menu_item_new_with_label("退出 (Quit)");
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);

    gtk_widget_show_all(menu);

#if GTK_CHECK_VERSION(3, 22, 0)
    gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
#else
    gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr,
                   gtk_status_icon_position_menu, icon, button, time);
#endif
}

static gboolean on_tray_click(GtkStatusIcon* /*icon*/, gpointer /*data*/) {
    printf("[Opacity] 管理器运行中，右键托盘图标查看快捷键\n");
    return TRUE;
}

// ─── main ────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    gtk_init(&argc, &argv);

    // 复用 GDK 已有的 X11 连接（避免两个独立连接导致事件过滤器失效）
    GdkDisplay* gdk_disp = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gdk_disp)) {
        fprintf(stderr, "错误：当前不是 X11 会话，请在登录界面选择 "
                        "\"Ubuntu on Xorg\"。\n");
        return 1;
    }
    xdisplay    = GDK_DISPLAY_XDISPLAY(gdk_disp);
    root_window = DefaultRootWindow(xdisplay);

    // ── 定义快捷键 ──
    struct Def { KeySym sym; int pct; const char* label; };
    Def defs[] = {
        { XK_1,     5, "Ctrl+Shift+1 →  5%" },
        { XK_2,    15, "Ctrl+Shift+2 → 15%" },
        { XK_3,    25, "Ctrl+Shift+3 → 25%" },
        { XK_4,    35, "Ctrl+Shift+4 → 35%" },
        { XK_5,    45, "Ctrl+Shift+5 → 45%" },
        { XK_6,    55, "Ctrl+Shift+6 → 55%" },
        { XK_7,    65, "Ctrl+Shift+7 → 65%" },
        { XK_8,    75, "Ctrl+Shift+8 → 75%" },
        { XK_9,    85, "Ctrl+Shift+9 → 85%" },
        { XK_0,    95, "Ctrl+Shift+0 → 95%" },
        { XK_minus,-1, "Ctrl+Shift+- → 复原(100%)" },
    };

    const unsigned int base = ControlMask | ShiftMask;
    for (const auto& d : defs) {
        KeyCode kc = XKeysymToKeycode(xdisplay, d.sym);
        if (kc == 0) {
            fprintf(stderr, "警告: keysym 0x%lx 无对应 keycode\n", d.sym);
            continue;
        }
        g_keybinds.push_back({ kc, d.pct, d.label });
        grab_all(xdisplay, root_window, kc, base);
    }

    // ── 注册 X11 全局事件过滤器 ──
    gdk_window_add_filter(gdk_get_default_root_window(), x11_filter, nullptr);

    // ── 创建系统托盘图标 ──
    GtkStatusIcon* tray = gtk_status_icon_new_from_icon_name(
        "preferences-desktop-display");               // 标准图标名
    gtk_status_icon_set_title(tray, "窗口透明度管理器");
    gtk_status_icon_set_tooltip_text(tray,
        "窗口透明度管理器\n"
        "Ctrl+Shift+1~0 : 设置透明度 (5%~95%)\n"
        "Ctrl+Shift+-   : 复原 (100%)");
    gtk_status_icon_set_visible(tray, TRUE);

    g_signal_connect(tray, "activate",
                    G_CALLBACK(on_tray_click), nullptr);
    g_signal_connect(tray, "popup-menu",
                    G_CALLBACK(on_tray_popup), nullptr);

    // ── 终端提示 ──
    printf("Started\n");

    gtk_main();
    return 0;
}