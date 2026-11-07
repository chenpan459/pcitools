#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "../include/rk_pcie_ioctl.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s info DEVICE\n"
        "  %s read DEVICE BAR OFFSET\n"
        "  %s write DEVICE BAR OFFSET VALUE\n"
        "\nExamples:\n"
        "  %s info /dev/rk-pcie0\n"
        "  %s read /dev/rk-pcie0 0 0x0\n"
        "  %s write /dev/rk-pcie0 0 0x8 0x1\n",
        prog, prog, prog, prog, prog, prog);
}

static int parse_u32(const char *s, uint32_t *out)
{
    char *end = NULL;
    unsigned long v;

    errno = 0;
    v = strtoul(s, &end, 0);
    if (errno || !end || *end != '\0' || v > UINT32_MAX)
        return -1;

    *out = (uint32_t)v;
    return 0;
}

static int cmd_info(int fd)
{
    struct rk_pcie_ioc_info info;
    unsigned int i;

    memset(&info, 0, sizeof(info));
    if (ioctl(fd, RK_PCIE_IOC_GET_INFO, &info) < 0) {
        perror("RK_PCIE_IOC_GET_INFO");
        return -1;
    }

    printf("PCIe device: %04x:%04x, subsystem %04x:%04x\n",
           info.vendor_id, info.device_id,
           info.subsystem_vendor_id, info.subsystem_device_id);
    printf("BDF: %04x:%02x:%02x.%x revision=0x%02x\n",
           info.domain, info.bus, info.dev, info.func, info.revision);

    if (info.link_speed)
        printf("Link: speed encoding=%u, width=x%u\n",
               info.link_speed, info.link_width);
    else
        printf("Link: PCIe Link Status unavailable\n");

    for (i = 0; i < RK_PCIE_MAX_BARS; i++) {
        if (info.bar_length[i])
            printf("BAR%u: start=0x%016" PRIx64
                   " length=0x%016" PRIx64 " flags=0x%08x\n",
                   i, (uint64_t)info.bar_start[i],
                   (uint64_t)info.bar_length[i],
                   info.bar_flags[i]);
    }

    return 0;
}

static int cmd_bar(int fd, int write_op,
                   uint32_t bar_num, uint32_t offset, uint32_t value)
{
    struct rk_pcie_ioc_bar bar;

    memset(&bar, 0, sizeof(bar));
    bar.bar = bar_num;
    bar.offset = offset;
    bar.value = value;

    if (ioctl(fd, write_op ? RK_PCIE_IOC_BAR_WRITE :
                            RK_PCIE_IOC_BAR_READ, &bar) < 0) {
        perror(write_op ? "RK_PCIE_IOC_BAR_WRITE" :
                          "RK_PCIE_IOC_BAR_READ");
        return -1;
    }

    if (write_op)
        printf("BAR%u[0x%08x] <= 0x%08x\n",
               bar_num, offset, value);
    else
        printf("BAR%u[0x%08x] = 0x%08x\n",
               bar_num, offset, bar.value);

    return 0;
}

int main(int argc, char **argv)
{
    const char *cmd;
    const char *path;
    int fd;
    uint32_t bar, offset, value = 0;
    int ret = 1;

    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }

    cmd = argv[1];
    path = argv[2];

    fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(path);
        return 1;
    }

    if (!strcmp(cmd, "info") && argc == 3) {
        ret = cmd_info(fd) ? 1 : 0;
    } else if (!strcmp(cmd, "read") && argc == 5) {
        if (parse_u32(argv[3], &bar) ||
            parse_u32(argv[4], &offset)) {
            fprintf(stderr, "invalid BAR or offset\n");
            ret = 2;
        } else {
            ret = cmd_bar(fd, 0, bar, offset, 0) ? 1 : 0;
        }
    } else if (!strcmp(cmd, "write") && argc == 6) {
        if (parse_u32(argv[3], &bar) ||
            parse_u32(argv[4], &offset) ||
            parse_u32(argv[5], &value)) {
            fprintf(stderr, "invalid BAR, offset or value\n");
            ret = 2;
        } else {
            ret = cmd_bar(fd, 1, bar, offset, value) ? 1 : 0;
        }
    } else {
        usage(argv[0]);
        ret = 2;
    }

    close(fd);
    return ret;
}
