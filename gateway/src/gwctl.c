#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-s socket] <status|dtc|session <xx>|read-did <hex>|clear-dtc>\n",
            prog);
}

int main(int argc, char **argv)
{
    const char *sock_path = "/run/gatewayd.sock";
    struct sockaddr_un addr;
    char line[512];
    char resp[4096];
    ssize_t n;
    size_t used;
    int fd;
    int opt;
    int i;

    while ((opt = getopt(argc, argv, "s:")) != -1) {
        if (opt == 's') sock_path = optarg;
        else {
            usage(argv[0]);
            return 1;
        }
    }
    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }

    line[0] = '\0';
    for (i = optind; i < argc; i++) {
        if (strlen(line) + strlen(argv[i]) + 2U >= sizeof line) {
            fprintf(stderr, "command too long\n");
            return 1;
        }
        if (line[0]) strcat(line, " ");
        strcat(line, argv[i]);
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof addr.sun_path, "%s", sock_path);

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        fprintf(stderr, "connect %s: %s\n", sock_path, strerror(errno));
        close(fd);
        return 1;
    }

    if (write(fd, line, strlen(line)) < 0 ||
        write(fd, "\n", 1) < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    used = 0;
    while ((n = read(fd, resp + used, sizeof resp - used - 1U)) > 0) {
        used += (size_t)n;
        if (used + 1U >= sizeof resp) break;
    }
    resp[used] = '\0';
    fputs(resp, stdout);
    if (used == 0 || resp[used - 1U] != '\n') fputc('\n', stdout);

    close(fd);
    return 0;
}
