#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "../include/rk_pcie_uapi.h"

#define TEST_MAX_SIZE (4u << 20)

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s info DEVICE\n"
		"  %s read DEVICE BAR OFFSET\n"
		"  %s write DEVICE BAR OFFSET VALUE\n"
		"  %s dma-mode DEVICE poll|msi\n"
		"  %s h2c DEVICE EP_ADDR SIZE [CH]\n"
		"  %s c2h DEVICE EP_ADDR SIZE [CH]\n"
		"  %s loop DEVICE EP_ADDR SIZE [CH]\n"
		"  %s irq-alloc DEVICE\n"
		"  %s irq-wait DEVICE VIRTUAL_ID TIMEOUT_MS\n"
		"  %s irq-free DEVICE VIRTUAL_ID\n"
		"\n"
		"info reads the BAR0 object version register and checks it\n"
		"against GET_INFO. That register is read-only.\n"
		"BAR0 bytes below 0x400 and the DMA BAR (BAR4) cannot be written.\n"
		"h2c/c2h/loop use one DMA channel (default 0; 1 is also locked).\n"
		"EP_ADDR must be spare endpoint memory. loop overwrites it.\n"
		"DMA interrupts stay masked until dma-mode msi.\n"
		"\n"
		"Example:\n"
		"  %s info /dev/pcie-rkep-0000:03:00.0\n"
		"  %s read /dev/pcie-rkep-0000:03:00.0 0 0x4\n",
		prog, prog, prog, prog, prog, prog, prog, prog, prog, prog,
		prog, prog);
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

static int parse_u64(const char *s, uint64_t *out)
{
	char *end = NULL;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 0);
	if (errno || !end || *end != '\0')
		return -1;
	*out = (uint64_t)v;
	return 0;
}

static const char *speed_name(unsigned int speed)
{
	switch (speed) {
	case 1: return "2.5 GT/s";
	case 2: return "5.0 GT/s";
	case 3: return "8.0 GT/s";
	case 4: return "16.0 GT/s";
	case 5: return "32.0 GT/s";
	case 6: return "64.0 GT/s";
	default: return NULL;
	}
}

static int cmd_info(int fd)
{
	struct rk_pcie_ioc_info info;
	struct rk_pcie_ioc_bar bar;
	const char *speed;
	unsigned int i;

	memset(&info, 0, sizeof(info));
	if (ioctl(fd, RK_PCIE_IOC_GET_INFO, &info) < 0) {
		perror("RK_PCIE_IOC_GET_INFO");
		return -1;
	}

	printf("ABI %u  driver 0x%08x  dma %s  channels %u  msi vectors %u\n",
	       info.abi_version, info.drv_version,
	       info.dma_completion == RK_PCIE_DMA_MSI ? "msi" : "poll",
	       info.dma_channels, info.msi_vectors);
	printf("PCIe device: %04x:%04x, subsystem %04x:%04x\n",
	       info.vendor_id, info.device_id,
	       info.subsystem_vendor_id, info.subsystem_device_id);
	printf("BDF: %04x:%02x:%02x.%x revision=0x%02x\n",
	       info.domain, info.bus, info.dev, info.func, info.revision);

	speed = speed_name(info.link_speed);
	if (speed)
		printf("Link: %s x%u\n", speed, info.link_width);
	else
		printf("Link: speed encoding %u, width x%u\n",
		       info.link_speed, info.link_width);
	printf("DevSta: 0x%04x%s%s\n", info.devsta,
	       (info.devsta & 0x0001) ? " CorrErr" : "",
	       (info.devsta & 0x0008) ? " UnsupReq" : "");
	printf("BAR0 object: off=0x%x magic=0x%08x version=0x%08x\n",
	       info.obj_off, info.obj_magic, info.obj_version);
	if (info.obj_magic != PCIE_EP_OBJ_INFO_MAGIC)
		printf("warning: magic is not RKEP (0x%08x)\n", PCIE_EP_OBJ_INFO_MAGIC);

	for (i = 0; i < RK_PCIE_MAX_BARS; i++) {
		if (!info.bar_length[i])
			continue;
		printf("BAR%u: start=0x%016" PRIx64 " length=0x%016" PRIx64
		       " flags=0x%08x\n",
		       i, info.bar_start[i], info.bar_length[i], info.bar_flags[i]);
	}

	memset(&bar, 0, sizeof(bar));
	bar.bar = RK_PCIE_BAR_CTRL;
	bar.offset = info.obj_off + RK_PCIE_OBJ_MAGIC_OFF;
	if (ioctl(fd, RK_PCIE_IOC_BAR_READ, &bar) < 0) {
		perror("BAR read magic");
		return -1;
	}
	if (bar.value != info.obj_magic) {
		fprintf(stderr, "magic register 0x%08x != GET_INFO 0x%08x\n",
			bar.value, info.obj_magic);
		return -1;
	}

	bar.offset = info.obj_off + RK_PCIE_OBJ_VERSION_OFF;
	bar.value = 0;
	if (ioctl(fd, RK_PCIE_IOC_BAR_READ, &bar) < 0) {
		perror("BAR read version");
		return -1;
	}
	printf("BAR0 version register [0x%x] = 0x%08x\n", bar.offset, bar.value);
	if (bar.value != info.obj_version) {
		fprintf(stderr, "version register 0x%08x != GET_INFO 0x%08x\n",
			bar.value, info.obj_version);
		return -1;
	}
	printf("version register matches GET_INFO\n");
	return 0;
}

static int cmd_bar(int fd, int write_op, uint32_t bar_num, uint32_t offset, uint32_t value)
{
	struct rk_pcie_ioc_bar bar;

	memset(&bar, 0, sizeof(bar));
	bar.bar = bar_num;
	bar.offset = offset;
	bar.value = value;

	if (ioctl(fd, write_op ? RK_PCIE_IOC_BAR_WRITE : RK_PCIE_IOC_BAR_READ, &bar) < 0) {
		if (errno == EPERM)
			fprintf(stderr,
				"BAR%u offset 0x%x is not writable (object header and DMA BAR stay in the kernel)\n",
				bar_num, offset);
		perror(write_op ? "RK_PCIE_IOC_BAR_WRITE" : "RK_PCIE_IOC_BAR_READ");
		return -1;
	}

	if (write_op)
		printf("BAR%u[0x%08x] <= 0x%08x\n", bar_num, offset, value);
	else
		printf("BAR%u[0x%08x] = 0x%08x\n", bar_num, offset, bar.value);
	return 0;
}

struct host_buf {
	void *ptr;
	uint64_t dma;
	uint32_t size;
	uint32_t map_size;
};

static int buf_alloc(int fd, uint32_t size, struct host_buf *buf)
{
	struct pcie_ep_continuous_buffer_param param;
	long page = sysconf(_SC_PAGESIZE);
	uint32_t map_size;
	void *ptr;

	if (page <= 0)
		page = 4096;
	map_size = (size + (uint32_t)page - 1) & ~((uint32_t)page - 1);

	memset(&param, 0, sizeof(param));
	param.size = map_size;
	if (ioctl(fd, PCIE_EP_CONTINUOUS_BUFFER_ALLOC, &param) < 0) {
		perror("PCIE_EP_CONTINUOUS_BUFFER_ALLOC");
		return -1;
	}
	if (ioctl(fd, PCIE_EP_SET_MMAP_RESOURCE_CONTINUOUS_BUFFER, &param) < 0) {
		perror("PCIE_EP_SET_MMAP_RESOURCE_CONTINUOUS_BUFFER");
		ioctl(fd, PCIE_EP_CONTINUOUS_BUFFER_FREE, &param);
		return -1;
	}

	ptr = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr == MAP_FAILED) {
		perror("mmap");
		ioctl(fd, PCIE_EP_CONTINUOUS_BUFFER_FREE, &param);
		return -1;
	}

	buf->ptr = ptr;
	buf->dma = param.dma_addr;
	buf->size = size;
	buf->map_size = map_size;
	return 0;
}

static void buf_free(int fd, struct host_buf *buf)
{
	struct pcie_ep_continuous_buffer_param param;

	if (!buf->ptr)
		return;
	munmap(buf->ptr, buf->map_size);
	memset(&param, 0, sizeof(param));
	param.dma_addr = buf->dma;
	param.size = buf->map_size;
	if (ioctl(fd, PCIE_EP_CONTINUOUS_BUFFER_FREE, &param) < 0)
		perror("PCIE_EP_CONTINUOUS_BUFFER_FREE");
	buf->ptr = NULL;
}

static uint64_t elapsed_us(const struct timespec *a, const struct timespec *b)
{
	time_t sec = b->tv_sec - a->tv_sec;
	long nsec = b->tv_nsec - a->tv_nsec;

	if (nsec < 0) {
		sec--;
		nsec += 1000000000L;
	}
	return (uint64_t)sec * 1000000ull + (uint64_t)nsec / 1000ull;
}

static int dma_xfer(int fd, int h2c, uint32_t ch, uint64_t ep, struct host_buf *buf)
{
	struct pcie_ep_dma_block_req req;
	struct timespec a, b;

	memset(&req, 0, sizeof(req));
	req.chn = (uint8_t)ch;
	req.wr = h2c ? 1 : 0;
	req.flag = PCIE_EP_DMA_BLOCK_FLAG_COHERENT;
	req.block.bus_paddr = ep;
	req.block.local_paddr = buf->dma;
	req.block.size = buf->size;

	clock_gettime(CLOCK_MONOTONIC, &a);
	if (ioctl(fd, PCIE_EP_DMA_XFER_BLOCK, &req) < 0) {
		perror(h2c ? "h2c" : "c2h");
		return -1;
	}
	clock_gettime(CLOCK_MONOTONIC, &b);
	printf("%s ch%u %u bytes ep=0x%016" PRIx64 " host=0x%016" PRIx64 " %llu us\n",
	       h2c ? "h2c" : "c2h", ch, buf->size, ep, buf->dma,
	       (unsigned long long)elapsed_us(&a, &b));
	return 0;
}

static void fill_pattern(void *ptr, uint32_t size)
{
	uint32_t *w = ptr;
	uint32_t n = size / 4;
	uint32_t i;

	for (i = 0; i < n; i++)
		w[i] = 0xA5000000u ^ i;
}

static int check_pattern(void *ptr, uint32_t size)
{
	uint32_t *w = ptr;
	uint32_t n = size / 4;
	uint32_t i;

	for (i = 0; i < n; i++) {
		uint32_t expect = 0xA5000000u ^ i;

		if (w[i] != expect) {
			fprintf(stderr, "mismatch at word %u: got 0x%08x expect 0x%08x\n",
				i, w[i], expect);
			return -1;
		}
	}
	return 0;
}

static int cmd_dma(int fd, int mode, uint64_t ep, uint32_t size, uint32_t ch)
{
	struct host_buf buf;
	int ret = -1;

	memset(&buf, 0, sizeof(buf));
	if (buf_alloc(fd, size, &buf))
		return -1;

	if (mode == 0 || mode == 2) {
		fill_pattern(buf.ptr, size);
		if (dma_xfer(fd, 1, ch, ep, &buf))
			goto out;
	}
	if (mode == 1) {
		memset(buf.ptr, 0, size);
		if (dma_xfer(fd, 0, ch, ep, &buf))
			goto out;
		printf("c2h first word 0x%08x\n", *(uint32_t *)buf.ptr);
	}
	if (mode == 2) {
		memset(buf.ptr, 0, size);
		if (dma_xfer(fd, 0, ch, ep, &buf))
			goto out;
		if (check_pattern(buf.ptr, size))
			goto out;
		printf("loop ok\n");
	}
	ret = 0;
out:
	buf_free(fd, &buf);
	return ret;
}

static int cmd_dma_mode(int fd, const char *mode)
{
	__u32 value;

	if (!strcmp(mode, "poll"))
		value = RK_PCIE_DMA_POLL;
	else if (!strcmp(mode, "msi"))
		value = RK_PCIE_DMA_MSI;
	else {
		fprintf(stderr, "dma-mode is poll or msi\n");
		return -1;
	}
	if (ioctl(fd, RK_PCIE_IOC_DMA_COMPLETION, &value) < 0) {
		perror("RK_PCIE_IOC_DMA_COMPLETION");
		return -1;
	}
	printf("dma completion: %s\n", mode);
	return 0;
}

static int cmd_irq_alloc(int fd)
{
	int id = -1;

	if (ioctl(fd, PCIE_EP_REQUEST_VIRTUAL_ID, &id) < 0) {
		perror("PCIE_EP_REQUEST_VIRTUAL_ID");
		return -1;
	}
	printf("%d\n", id);
	return 0;
}

static int cmd_irq_free(int fd, uint32_t id)
{
	int value = (int)id;

	if (ioctl(fd, PCIE_EP_RELEASE_VIRTUAL_ID, &value) < 0) {
		perror("PCIE_EP_RELEASE_VIRTUAL_ID");
		return -1;
	}
	return 0;
}

static int cmd_irq_wait(int fd, uint32_t id, uint32_t timeout_ms)
{
	struct pcie_ep_obj_poll_virtual_id_cfg cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.timeout_ms = timeout_ms;
	cfg.virtual_id = id;
	if (ioctl(fd, PCIE_EP_POLL_IRQ_USER, &cfg) < 0) {
		perror("PCIE_EP_POLL_IRQ_USER");
		return -1;
	}
	if (cfg.poll_status == POLLIN) {
		printf("virtual id %u ready\n", id);
		return 0;
	}
	printf("virtual id %u timeout\n", id);
	return 1;
}

static int parse_channel(int argc, char **argv, int idx, uint32_t *ch)
{
	*ch = 0;
	if (argc <= idx)
		return 0;
	if (argc != idx + 1 || parse_u32(argv[idx], ch) || *ch >= RK_PCIE_DMA_CH_MAX)
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	const char *cmd;
	const char *path;
	int fd;
	int ret = 1;
	uint32_t bar, offset, value, size, ch, id, timeout_ms;
	uint64_t ep;

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
		if (parse_u32(argv[3], &bar) || parse_u32(argv[4], &offset)) {
			fprintf(stderr, "invalid BAR or offset\n");
			ret = 2;
		} else {
			ret = cmd_bar(fd, 0, bar, offset, 0) ? 1 : 0;
		}
	} else if (!strcmp(cmd, "write") && argc == 6) {
		if (parse_u32(argv[3], &bar) || parse_u32(argv[4], &offset) ||
		    parse_u32(argv[5], &value)) {
			fprintf(stderr, "invalid BAR, offset or value\n");
			ret = 2;
		} else {
			ret = cmd_bar(fd, 1, bar, offset, value) ? 1 : 0;
		}
	} else if (!strcmp(cmd, "dma-mode") && argc == 4) {
		ret = cmd_dma_mode(fd, argv[3]) ? 1 : 0;
	} else if ((!strcmp(cmd, "h2c") || !strcmp(cmd, "c2h") || !strcmp(cmd, "loop")) &&
		   (argc == 5 || argc == 6)) {
		if (parse_u64(argv[3], &ep) || parse_u32(argv[4], &size) ||
		    parse_channel(argc, argv, 5, &ch) ||
		    !size || size > TEST_MAX_SIZE || (size & 3)) {
			fprintf(stderr, "invalid endpoint address, size or channel\n");
			ret = 2;
		} else if (!strcmp(cmd, "h2c")) {
			ret = cmd_dma(fd, 0, ep, size, ch) ? 1 : 0;
		} else if (!strcmp(cmd, "c2h")) {
			ret = cmd_dma(fd, 1, ep, size, ch) ? 1 : 0;
		} else {
			ret = cmd_dma(fd, 2, ep, size, ch) ? 1 : 0;
		}
	} else if (!strcmp(cmd, "irq-alloc") && argc == 3) {
		ret = cmd_irq_alloc(fd) ? 1 : 0;
	} else if (!strcmp(cmd, "irq-free") && argc == 4) {
		if (parse_u32(argv[3], &id)) {
			fprintf(stderr, "invalid virtual id\n");
			ret = 2;
		} else {
			ret = cmd_irq_free(fd, id) ? 1 : 0;
		}
	} else if (!strcmp(cmd, "irq-wait") && argc == 5) {
		if (parse_u32(argv[3], &id) || parse_u32(argv[4], &timeout_ms)) {
			fprintf(stderr, "invalid virtual id or timeout\n");
			ret = 2;
		} else {
			int waited = cmd_irq_wait(fd, id, timeout_ms);

			ret = waited < 0 ? 1 : waited;
		}
	} else {
		usage(argv[0]);
		ret = 2;
	}

	close(fd);
	return ret;
}
