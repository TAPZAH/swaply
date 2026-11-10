// Swaply for Linux/X11 — tray icon and settings window.
// Copyright (C) 2026 Tap3ah. GNU GPL v3+.

#include "config_linux.h"

#include <gtk/gtk.h>

#include <csignal>
#include <fstream>
#include <string>

#include <sys/types.h>
#include <unistd.h>

namespace {

LinuxConfig g_config;
GtkWidget* g_menu = nullptr;
GtkWidget* g_enabled_item = nullptr;
GtkWidget* g_auto_item = nullptr;
GtkWidget* g_autostart_item = nullptr;
bool g_updating = false;

void persist() {
    save_config(g_config);
}

void refresh_menu_state() {
    g_updating = true;
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_enabled_item), g_config.enabled ? TRUE : FALSE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_auto_item), g_config.auto_switch ? TRUE : FALSE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_autostart_item), g_config.start_with_windows ? TRUE : FALSE);
    g_updating = false;
}

void refresh_from_disk() {
    g_config = load_config();
    refresh_menu_state();
}

void on_enabled_toggled(GtkCheckMenuItem* item, gpointer) {
    if (g_updating) {
        return;
    }
    g_config.enabled = gtk_check_menu_item_get_active(item) != FALSE;
    persist();
}

void on_auto_toggled(GtkCheckMenuItem* item, gpointer) {
    if (g_updating) {
        return;
    }
    g_config.auto_switch = gtk_check_menu_item_get_active(item) != FALSE;
    persist();
}

void on_autostart_toggled(GtkCheckMenuItem* item, gpointer) {
    if (g_updating) {
        return;
    }
    g_config.start_with_windows = gtk_check_menu_item_get_active(item) != FALSE;
    persist();
    apply_autostart(g_config.start_with_windows);
}

[[nodiscard]] long daemon_pid() {
    std::ifstream input(config_directory() + "/swaply.pid");
    long pid = 0;
    if (!(input >> pid) || pid <= 1) {
        return 0;
    }
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    if (!(comm >> name) || name != "swaply-x11") {
        return 0;
    }
    return pid;
}

void stop_daemon() {
    const long pid = daemon_pid();
    if (pid > 1) {
        kill(static_cast<pid_t>(pid), SIGTERM);
    }
}

void ensure_daemon() {
    if (daemon_pid() > 1) {
        return;
    }
    std::string daemon = "swaply-x11";
    char self[4096]{};
    const ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (length > 0) {
        const std::string path(self, static_cast<std::size_t>(length));
        const auto slash = path.find_last_of('/');
        if (slash != std::string::npos) {
            const std::string candidate = path.substr(0, slash + 1) + "swaply-x11";
            if (access(candidate.c_str(), X_OK) == 0) {
                daemon = candidate;
            }
        }
    }
    const pid_t child = fork();
    if (child == 0) {
        setsid();
        execl(daemon.c_str(), daemon.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
}

void on_quit(GtkMenuItem*, gpointer) {
    stop_daemon();
    gtk_main_quit();
}

void on_settings(GtkMenuItem*, gpointer) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Настройки Swaply",
        nullptr,
        GTK_DIALOG_MODAL,
        "Отмена",
        GTK_RESPONSE_CANCEL,
        "Сохранить",
        GTK_RESPONSE_ACCEPT,
        nullptr);

    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 16);
    gtk_container_add(GTK_CONTAINER(area), grid);

    GtkWidget* enabled = gtk_check_button_new_with_label("Включить Swaply");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), g_config.enabled ? TRUE : FALSE);
    gtk_grid_attach(GTK_GRID(grid), enabled, 0, 0, 2, 1);

    GtkWidget* auto_switch = gtk_check_button_new_with_label("Автоматически исправлять раскладку");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(auto_switch), g_config.auto_switch ? TRUE : FALSE);
    gtk_grid_attach(GTK_GRID(grid), auto_switch, 0, 1, 2, 1);

    GtkWidget* autostart = gtk_check_button_new_with_label("Запускать при входе в систему");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autostart), g_config.start_with_windows ? TRUE : FALSE);
    gtk_grid_attach(GTK_GRID(grid), autostart, 0, 2, 2, 1);

    GtkWidget* length_label = gtk_label_new("Минимальная длина слова");
    gtk_widget_set_halign(length_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), length_label, 0, 3, 1, 1);

    GtkWidget* length = gtk_spin_button_new_with_range(2, 8, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(length), static_cast<gdouble>(g_config.min_word_length));
    gtk_grid_attach(GTK_GRID(grid), length, 1, 3, 1, 1);

    GtkWidget* hint = gtk_label_new("Хоткей конвертации: Pause (меняется в config.json)");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 4, 2, 1);

    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        g_config.enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled)) != FALSE;
        g_config.auto_switch = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(auto_switch)) != FALSE;
        g_config.start_with_windows = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(autostart)) != FALSE;
        g_config.min_word_length = static_cast<std::size_t>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(length)));
        persist();
        apply_autostart(g_config.start_with_windows);
        refresh_menu_state();
    }
    gtk_widget_destroy(dialog);
}

void popup_menu() {
    gtk_menu_popup_at_pointer(GTK_MENU(g_menu), nullptr);
}

void on_popup_menu(GtkStatusIcon*, guint, guint, gpointer) {
    popup_menu();
}

void on_activate(GtkStatusIcon*, gpointer) {
    refresh_from_disk();
    popup_menu();
}

}  // namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    g_config = load_config();
    ensure_daemon();

    g_menu = gtk_menu_new();

    g_enabled_item = gtk_check_menu_item_new_with_label("Включено");
    g_signal_connect(g_enabled_item, "toggled", G_CALLBACK(on_enabled_toggled), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_enabled_item);

    g_auto_item = gtk_check_menu_item_new_with_label("Автоконвертация");
    g_signal_connect(g_auto_item, "toggled", G_CALLBACK(on_auto_toggled), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_auto_item);

    g_autostart_item = gtk_check_menu_item_new_with_label("Запускать при входе");
    g_signal_connect(g_autostart_item, "toggled", G_CALLBACK(on_autostart_toggled), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), g_autostart_item);

    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), gtk_separator_menu_item_new());

    GtkWidget* settings = gtk_menu_item_new_with_label("Настройки…");
    g_signal_connect(settings, "activate", G_CALLBACK(on_settings), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), settings);

    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), gtk_separator_menu_item_new());

    GtkWidget* quit = gtk_menu_item_new_with_label("Выход");
    g_signal_connect(quit, "activate", G_CALLBACK(on_quit), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu), quit);

    gtk_widget_show_all(g_menu);
    refresh_menu_state();

    // Prefer the themed application icon ("swaply", installed with the deb into
    // hicolor); fall back to a generic keyboard icon when running from a build
    // tree where the icon is not registered with the desktop theme.
    const char* icon_name =
        gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "swaply") ? "swaply" : "input-keyboard";
    GtkStatusIcon* icon = gtk_status_icon_new_from_icon_name(icon_name);
    gtk_status_icon_set_tooltip_text(icon, "Swaply — EN/RU");
    gtk_status_icon_set_visible(icon, TRUE);
    g_signal_connect(icon, "popup-menu", G_CALLBACK(on_popup_menu), nullptr);
    g_signal_connect(icon, "activate", G_CALLBACK(on_activate), nullptr);

    gtk_main();
    return 0;
}
