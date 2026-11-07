#ifndef RK_PCIE_IOCTL_H
#define RK_PCIE_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define RK_PCIE_IOC_MAGIC       'R'
#define RK_PCIE_MAX_BARS        6

struct rk_pcie_ioc_info {
    __u16 vendor_id;
    __u16 device_id;
    __u16 subsystem_vendor_id;
    __u16 subsystem_device_id;

    __u32 domain;
    __u8  bus;
    __u8  dev;
    __u8  func;
    __u8  revision;

    /* PCIe Link Status: speed encoding and negotiated width.
     * link_speed: 1=2.5GT/s, 2=5.0GT/s, 3=8.0GT/s, etc.
     * Zero means Link Status could not be read / PCIe capability absent.
     */
    __u8  link_speed;
    __u8  link_width;
    __u16 reserved0;

    __u64 bar_start[RK_PCIE_MAX_BARS];
    __u64 bar_length[RK_PCIE_MAX_BARS];
    __u32 bar_flags[RK_PCIE_MAX_BARS];
};

struct rk_pcie_ioc_bar {
    __u32 bar;
    __u32 offset;
    __u32 value;
    __u32 reserved;
};

#define RK_PCIE_IOC_GET_INFO \
    _IOR(RK_PCIE_IOC_MAGIC, 0x01, struct rk_pcie_ioc_info)

#define RK_PCIE_IOC_BAR_READ \
    _IOWR(RK_PCIE_IOC_MAGIC, 0x02, struct rk_pcie_ioc_bar)

#define RK_PCIE_IOC_BAR_WRITE \
    _IOW(RK_PCIE_IOC_MAGIC, 0x03, struct rk_pcie_ioc_bar)

#endif
