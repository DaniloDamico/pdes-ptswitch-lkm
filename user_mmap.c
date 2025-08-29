#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#define PTS_IOCTL_NOME 'v'
#define PTS_IOCTL_SET _IOW(PTS_IOCTL_NOME, 1, int)
#define PTS_IOCTL_TOGGLE _IO(PTS_IOCTL_NOME, 2)
#define PTS_IOCTL_GET _IOR(PTS_IOCTL_NOME, 3, int)

static void hexdump(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
    {
        printf("%02x ", b[i]);
        if ((i + 1) % 16 == 0)
            puts("");
    }
    if (n % 16)
        puts("");
}

int main(int argc, char **argv)
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/ptswitch";
    int fd = open(dev, O_RDWR);
    if (fd < 0)
    {
        perror("open");
        return 1;
    }

    void *addr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED)
    {
        perror("mmap");
        close(fd);
        return 1;
    }

    // lettura v0
    write(1, addr, 40);
    write(1, "\n", 1);
    hexdump(addr, 32);

    // toggle a v1
    if (ioctl(fd, PTS_IOCTL_TOGGLE) < 0)
    {
        perror("ioctl toggle");
        return 1;
    }

    // lettura v1 (stesso VA)
    write(1, addr, 40);
    write(1, "\n", 1);
    hexdump(addr, 32);

    // set a v0 e rilettura
    int v = 0;
    if (ioctl(fd, PTS_IOCTL_SET, &v) < 0)
    {
        perror("ioctl set");
        return 1;
    }
    write(1, addr, 40);
    write(1, "\n", 1);

    munmap(addr, 4096);
    close(fd);
    return 0;
}
