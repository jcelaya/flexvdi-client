#include <gtk/gtk.h>

#include "flexvdi-client-app.h"
#include "flexvdi-client-win.h"

struct _ClientAppWindow {
    GtkApplicationWindow parent;
    ClientConf * conf;
    GtkLabel * version;
    GtkLabel * info;
    GtkLabel * status;
    GtkStack * stack;
    GtkEntry * host;
    GtkCheckButton * fullscreen;
};

G_DEFINE_TYPE(ClientAppWindow, client_app_window, GTK_TYPE_APPLICATION_WINDOW);

static void client_app_window_init(ClientAppWindow * win) {
    GtkCssProvider * css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css_provider, "/com/flexvdi/client/style.css");
    GdkScreen * screen = gtk_widget_get_screen(GTK_WIDGET(win));
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css_provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    gtk_widget_init_template(GTK_WIDGET(win));

    gtk_label_set_text(win->version, "version " VERSION_STRING);
}

static void client_app_window_dispose(GObject * obj) {
    ClientAppWindow * win = CLIENT_APP_WINDOW(obj);
    g_clear_object(&win->conf);
    G_OBJECT_CLASS(client_app_window_parent_class)->dispose(obj);
}

static void client_app_window_class_init(ClientAppWindowClass * class) {
    G_OBJECT_CLASS(class)->dispose = client_app_window_dispose;

    gtk_widget_class_set_template_from_resource(GTK_WIDGET_CLASS(class),
                                                "/com/flexvdi/client/window.ui");
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ClientAppWindow, version);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ClientAppWindow, info);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ClientAppWindow, status);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ClientAppWindow, stack);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ClientAppWindow, host);
    gtk_widget_class_bind_template_child(GTK_WIDGET_CLASS(class), ClientAppWindow, fullscreen);
}

void client_app_window_set_config(ClientAppWindow * win, ClientConf * conf) {
    win->conf = g_object_ref(conf);
    const gchar * host = client_conf_get_host(win->conf);
    if (host) {
        if (client_conf_get_port(win->conf) == 443) {
            gtk_entry_set_text(win->host, host);
        } else {
            g_autofree gchar * host_str = g_strdup_printf("%s:%d", host, client_conf_get_port(win->conf));
            gtk_entry_set_text(win->host, host_str);
        }
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(win->fullscreen),
        client_conf_get_fullscreen(win->conf));
}

void client_app_window_set_info(ClientAppWindow * win, const gchar * text) {
    gtk_label_set_text(win->info, text);
}

void client_app_window_set_status(ClientAppWindow * win, const gchar * text) {
    gtk_label_set_text(win->status, text);
}

void client_app_window_set_central_widget(ClientAppWindow * win, const gchar * name) {
    gtk_stack_set_visible_child_name(win->stack, name);
}

ClientAppWindow * client_app_window_new(ClientApp * app) {
    return g_object_new(CLIENT_APP_WINDOW_TYPE, "application", app, NULL);
}
