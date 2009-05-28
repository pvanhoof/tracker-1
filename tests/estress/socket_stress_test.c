#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
    

int main(void)
{
        int s, t, len;
        struct sockaddr_un remote;
	const char *str;
	int i = 0;
	gchar *path, *tmp;
	struct sockaddr_un addr;
	GTimer *timer;

        if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            perror("socket");
            exit(1);
        }

        printf("Trying to connect...\n");

	tmp = g_strdup_printf ("tracker-%s", g_get_user_name ());
	path = g_build_filename (g_get_tmp_dir (), tmp, "socket", NULL);

	remote.sun_family = AF_UNIX;
	strcpy (remote.sun_path, path);

	g_free (tmp);
	g_free (path);

        len = strlen(remote.sun_path) + sizeof(remote.sun_family);
        if (connect(s, (struct sockaddr *)&remote, len) == -1) {
            perror("connect");
            exit(1);
        }

        printf("Connected.\n");

	str = "UPDATE {0000000032}\nINSERT { <test> a nfo:Document }";

	timer = g_timer_new ();

	g_timer_start (timer);
	for (i = 0; i < 10000; i++) {
            if (send(s, str, strlen(str), 0) == -1) {
                perror("send");
                exit(1);
            }
        }
	g_timer_stop (timer);

	printf ("%f\n", g_timer_elapsed (timer, NULL));

	g_timer_destroy (timer);

        close(s);

        return 0;
}

