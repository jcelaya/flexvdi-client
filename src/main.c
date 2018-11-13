#include <gtk/gtk.h>
#include <gst/gst.h>
#include "client-app.h"
#include "client-log.h"


int main (int argc, char * argv[]) {
    // Check the --version option as early as possible
    int i;
    for (i = 0; i < argc; ++i) {
        if (g_strcmp0(argv[i], "--version") == 0) {
            printf("flexVDI Client v" VERSION_STRING "\n"
                   "Copyright (C) 2018 Flexible Software Solutions S.L.\n");
            return 0;
        }
    }

    client_log_setup(argc, argv);
    g_message("Starting flexVDI Client v" VERSION_STRING);

    gst_init(&argc, &argv);
    return g_application_run(G_APPLICATION(client_app_new()), argc, argv);
}
