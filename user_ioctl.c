#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <string.h>

#define PTS_IOCTL_NOME   'v'            
#define PTS_IOCTL_SET    _IOW(PTS_IOCTL_NOME, 1, int)
#define PTS_IOCTL_TOGGLE _IO( PTS_IOCTL_NOME, 2)
#define PTS_IOCTL_GET    _IOR(PTS_IOCTL_NOME, 3, int)

int main(int argc, char **argv) {
    const char *dev = (argc > 1) ? argv[1] : "/dev/ptswitch";
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open(%s) failed: %s\n", dev, strerror(errno));
        return 1;
    }

    // TOGGLE
    if (ioctl(fd, PTS_IOCTL_TOGGLE) < 0) {
        fprintf(stderr, "ioctl(TOGGLE) failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    // GET
    int v = -1;
    if (ioctl(fd, PTS_IOCTL_GET, &v) < 0) {
        fprintf(stderr, "ioctl(GET) failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("version=%d\n", v);

    // SET 0
    v = 0;
    if (ioctl(fd, PTS_IOCTL_SET, &v) < 0) {
        fprintf(stderr, "ioctl(SET) failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    // GET again
    if (ioctl(fd, PTS_IOCTL_GET, &v) < 0) {
        fprintf(stderr, "ioctl(GET) failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("version=%d\n", v);

    close(fd);
    return 0;
}
