/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace ABI for the Rockchip PCIe endpoint driver.
 * Kernel and pcie_test both include this header.
 */
#ifndef _RK_PCIE_UAPI_H_
#define _RK_PCIE_UAPI_H_

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
#endif

#define RK_PCIE_ABI_VERSION		1
#define RK_PCIE_MAX_BARS		6
#define RK_PCIE_DMA_CH_MAX		2

/* BAR0 object header. Reads of magic/version are the safe BAR check. */
#define RK_PCIE_OBJ_MAGIC_OFF		0
#define RK_PCIE_OBJ_VERSION_OFF		4
#define RK_PCIE_OBJ_INFO_SIZE		0x400

#define RK_PCIE_BAR_CTRL		0
#define RK_PCIE_BAR_MEM			2
#define RK_PCIE_BAR_DMA			4

#define RK_PCIE_DMA_POLL		0
#define RK_PCIE_DMA_MSI			1

/* rkep device mode status definition */
#define RKEP_MODE_BOOTROM		1
#define RKEP_MODE_LOADER		2
#define RKEP_MODE_KERNEL		3
#define RKEP_MODE_FUN0			4
#define RKEP_SMODE_INIT			0
#define RKEP_SMODE_LNKRDY		1
#define RKEP_SMODE_LNKUP		2
#define RKEP_SMODE_ERR			0xff
#define RKEP_SMODE_FWDLRDY		0x10
#define RKEP_SMODE_FWDLDONE		0x11
#define RKEP_SMODE_APPRDY		0x20

struct pcie_ep_dma_cache_cfg {
	__u64 addr;
	__u32 size;
};

/*
 * local_paddr is the host DMA address from CONTINUOUS_BUFFER_ALLOC.
 * bus_paddr is the endpoint address.
 */
struct pcie_ep_dma_block {
	__u64 bus_paddr;
	__u64 local_paddr;
	__u32 size;
};

struct pcie_ep_dma_block_req {
	__u16 vir_id;	/* Default 0 */
	__u8 chn;
	__u8 wr;	/* 1: host-to-endpoint, 0: endpoint-to-host */
	__u32 flag;
#define PCIE_EP_DMA_BLOCK_FLAG_COHERENT (1U << 0)
	struct pcie_ep_dma_block block;
};

#define PCIE_EP_OBJ_INFO_MAGIC 0x524B4550

enum pcie_ep_obj_irq_type {
	OBJ_IRQ_UNKNOWN,
	OBJ_IRQ_DMA,
	OBJ_IRQ_USER,
	OBJ_IRQ_ELBI,
};

struct pcie_ep_obj_irq_dma_status {
	__u32 wr;
	__u32 rd;
};

enum pcie_ep_mmap_resource {
	PCIE_EP_MMAP_RESOURCE_DBI,
	PCIE_EP_MMAP_RESOURCE_BAR0,
	PCIE_EP_MMAP_RESOURCE_BAR2,
	PCIE_EP_MMAP_RESOURCE_BAR4,
	PCIE_EP_MMAP_RESOURCE_USER_MEM,
	PCIE_EP_MMAP_RESOURCE_RK3568_RC_DBI,
	PCIE_EP_MMAP_RESOURCE_RK3588_RC_DBI,
	PCIE_EP_MMAP_RESOURCE_BAR1,
	PCIE_EP_MMAP_RESOURCE_BAR5,
	PCIE_EP_MMAP_RESOURCE_CONTINUOUS_BUFFER,
	PCIE_EP_MMAP_RESOURCE_MAX,
};

#define PCIE_EP_OBJ_INFO_MSI_DATA_NUM	0x8
#define RKEP_EP_VIRTUAL_ID_MAX		(PCIE_EP_OBJ_INFO_MSI_DATA_NUM * 32)

/*
 * rockchip ep device information which is stored in BAR0.
 * The header occupies the first RK_PCIE_OBJ_INFO_SIZE bytes.
 * Host software reads it; the endpoint and the interrupt handler write it.
 */
struct pcie_ep_obj_info {
	__u32 magic;
	__u32 version;
	struct {
		__u16 mode;
		__u16 submode;
	} devmode;
	__u32 msi_data[PCIE_EP_OBJ_INFO_MSI_DATA_NUM];
	__u8 err_event[0x20];
	__u8 rsvd0[0x1B0];

	__u32 irq_type_rc;
	struct pcie_ep_obj_irq_dma_status dma_status_rc;
	__u32 irq_type_ep;
	struct pcie_ep_obj_irq_dma_status dma_status_ep;
	__u32 irq_user_data_rc;
	__u32 irq_user_data_ep;
	__u32 irq_rc_msi_en;
	__u8 rsvd1[0xE0];
	__u8 rsvd2_user[0x100];
};

struct pcie_ep_obj_poll_virtual_id_cfg {
	__u32 timeout_ms;	/* milliseconds; sync != 0 waits without a timeout */
	__u32 sync;
	__u32 virtual_id;
	__u32 poll_status;	/* POLLIN when the endpoint raised this id */
};

struct pcie_ep_continuous_buffer_param {
	__u64 dma_addr;
	__u32 size;
};

struct pcie_ep_elbi_data_compare_and_swap_param {
	__u32 offset;
	__u32 old_val;
	__u32 new_val;
	int result;
};

struct rk_pcie_ioc_info {
	__u32 abi_version;
	__u32 drv_version;
	__u16 vendor_id;
	__u16 device_id;
	__u16 subsystem_vendor_id;
	__u16 subsystem_device_id;
	__u32 domain;
	__u8  bus;
	__u8  dev;
	__u8  func;
	__u8  revision;
	__u8  link_speed;	/* 1=2.5GT/s, 2=5.0, 3=8.0, 4=16.0, 5=32.0 */
	__u8  link_width;
	__u16 devsta;
	__u32 obj_magic;
	__u32 obj_version;
	__u32 obj_off;		/* byte offset of the object header in BAR0 */
	__u32 msi_vectors;
	__u32 dma_completion;	/* RK_PCIE_DMA_POLL or RK_PCIE_DMA_MSI */
	__u32 dma_channels;
	__u32 reserved0;
	__u64 bar_start[RK_PCIE_MAX_BARS];
	__u64 bar_length[RK_PCIE_MAX_BARS];
	__u32 bar_flags[RK_PCIE_MAX_BARS];
	__u32 reserved1[2];
};

struct rk_pcie_ioc_bar {
	__u32 bar;
	__u32 offset;
	__u32 value;
	__u32 reserved;
};

#define PCIE_BASE	'P'
#define PCIE_EP_GET_FUNC_DRV_VERSION				_IOR(PCIE_BASE, 0x00, unsigned int)
#define PCIE_DMA_CACHE_INVALIDE					_IOW(PCIE_BASE, 0x01, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_CACHE_FLUSH					_IOW(PCIE_BASE, 0x02, struct pcie_ep_dma_cache_cfg)
#define PCIE_DMA_IRQ_MASK_ALL					_IOW(PCIE_BASE, 0x03, int)
#define PCIE_EP_RAISE_MSI					_IOW(PCIE_BASE, 0x04, int)
#define PCIE_EP_SET_MMAP_RESOURCE				_IOW(PCIE_BASE, 0x06, int)
#define PCIE_EP_RAISE_ELBI					_IOW(PCIE_BASE, 0x07, int)
#define PCIE_EP_RESET_CTRL					_IOW(PCIE_BASE, 0x08, int)
#define PCIE_EP_REQUEST_VIRTUAL_ID				_IOR(PCIE_BASE, 0x10, int)
#define PCIE_EP_RELEASE_VIRTUAL_ID				_IOW(PCIE_BASE, 0x11, int)
#define PCIE_EP_RAISE_IRQ_USER					_IOW(PCIE_BASE, 0x12, int)
#define PCIE_EP_POLL_IRQ_USER					_IOW(PCIE_BASE, 0x13, struct pcie_ep_obj_poll_virtual_id_cfg)
#define PCIE_EP_OBJ_INFO_SYNC					_IOW(PCIE_BASE, 0x14, int)
#define PCIE_EP_DMA_MSI_DETECT					_IOW(PCIE_BASE, 0x15, int)
#define PCIE_EP_ELBI_DATA_COMPARE_AND_SWAP			_IOWR(PCIE_BASE, 0x16, struct pcie_ep_elbi_data_compare_and_swap_param)

/*
 * Block DMA. Channel 0 and 1 are both usable; each engine holds its own mutex
 * for the whole transfer. Completion is status polling unless userspace
 * selects RK_PCIE_DMA_MSI. DMA controller interrupts stay masked in poll mode.
 * local_paddr must fall inside a buffer allocated on this fd.
 */
#define PCIE_EP_DMA_XFER_BLOCK					_IOW(PCIE_BASE, 0x20, struct pcie_ep_dma_block_req)

#define PCIE_EP_CONTINUOUS_BUFFER_ALLOC				_IOWR(PCIE_BASE, 0x30, struct pcie_ep_continuous_buffer_param)
#define PCIE_EP_CONTINUOUS_BUFFER_FREE				_IOW(PCIE_BASE, 0x31, struct pcie_ep_continuous_buffer_param)
#define PCIE_EP_SET_MMAP_RESOURCE_CONTINUOUS_BUFFER		_IOW(PCIE_BASE, 0x32, struct pcie_ep_continuous_buffer_param)

#define RK_PCIE_IOC_GET_INFO					_IOR(PCIE_BASE, 0x40, struct rk_pcie_ioc_info)
#define RK_PCIE_IOC_BAR_READ					_IOWR(PCIE_BASE, 0x41, struct rk_pcie_ioc_bar)
#define RK_PCIE_IOC_BAR_WRITE					_IOW(PCIE_BASE, 0x42, struct rk_pcie_ioc_bar)
#define RK_PCIE_IOC_DMA_COMPLETION				_IOW(PCIE_BASE, 0x43, __u32)

_Static_assert(sizeof(struct rk_pcie_ioc_info) == 184, "rk_pcie_ioc_info size");
_Static_assert(sizeof(struct rk_pcie_ioc_bar) == 16, "rk_pcie_ioc_bar size");

#endif
