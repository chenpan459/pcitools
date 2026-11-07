// SPDX-License-Identifier: GPL-2.0
/*
 * RK3588 Linux 6.1 FPGA PCIe access driver - Phase 1
 *
 * Scope:
 *   - PCI probe/remove and dynamic ID binding
 *   - Request and map memory BARs
 *   - Character device /dev/rk-pcieN
 *   - ioctl: device information and 32-bit BAR access
 *
 * Not implemented in Phase 1:
 *   - XDMA engine programming / H2C / C2H
 *   - DMA buffer allocation / mmap
 *   - MSI/MSI-X event handling
 *
 * Bind an FPGA by writing its IDs to:
 *   /sys/bus/pci/drivers/rk-pcie-sdk/new_id
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/ida.h>
#include <linux/io.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "../include/rk_pcie_ioctl.h"

#define DRV_NAME        "rk-pcie-sdk"
#define DEV_NAME        "rk-pcie"
#define RK_PCIE_MINORS  256

struct rk_pcie_bar {
    void __iomem *vaddr;
    resource_size_t start;
    resource_size_t length;
    unsigned long flags;
};

struct rk_pcie_dev {
    struct pci_dev *pdev;
    struct rk_pcie_bar bars[RK_PCIE_MAX_BARS];

    struct cdev cdev;
    dev_t devt;
    struct device *chardev;
    int minor;

    struct mutex lock;
    struct kref refcount;
    bool dead;
    bool regions_requested;
    bool enabled;
};

static dev_t rk_pcie_devt;
static struct class *rk_pcie_class;
static DEFINE_IDA(rk_pcie_ida);

static void rk_pcie_release_ref(struct kref *ref)
{
    struct rk_pcie_dev *pcie =
        container_of(ref, struct rk_pcie_dev, refcount);

    if (pcie->pdev)
        pci_dev_put(pcie->pdev);

    kfree(pcie);
}

static int rk_pcie_open(struct inode *inode, struct file *file)
{
    struct rk_pcie_dev *pcie =
        container_of(inode->i_cdev, struct rk_pcie_dev, cdev);

    if (!kref_get_unless_zero(&pcie->refcount))
        return -ENODEV;

    mutex_lock(&pcie->lock);
    if (pcie->dead) {
        mutex_unlock(&pcie->lock);
        kref_put(&pcie->refcount, rk_pcie_release_ref);
        return -ENODEV;
    }
    mutex_unlock(&pcie->lock);

    file->private_data = pcie;
    return 0;
}

static int rk_pcie_release(struct inode *inode, struct file *file)
{
    struct rk_pcie_dev *pcie = file->private_data;

    if (pcie)
        kref_put(&pcie->refcount, rk_pcie_release_ref);

    return 0;
}

static int rk_pcie_get_info(struct rk_pcie_dev *pcie,
                            struct rk_pcie_ioc_info *info)
{
    struct pci_dev *pdev = pcie->pdev;
    u16 linksta = 0;
    int i;

    memset(info, 0, sizeof(*info));

    info->vendor_id = pdev->vendor;
    info->device_id = pdev->device;
    info->subsystem_vendor_id = pdev->subsystem_vendor;
    info->subsystem_device_id = pdev->subsystem_device;
    info->domain = pci_domain_nr(pdev->bus);
    info->bus = pdev->bus->number;
    info->dev = PCI_SLOT(pdev->devfn);
    info->func = PCI_FUNC(pdev->devfn);
    info->revision = pdev->revision;

    if (!pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &linksta)) {
        info->link_speed = linksta & PCI_EXP_LNKSTA_CLS;
        info->link_width =
            (linksta & PCI_EXP_LNKSTA_NLW) >> PCI_EXP_LNKSTA_NLW_SHIFT;
    }

    for (i = 0; i < RK_PCIE_MAX_BARS; i++) {
        info->bar_start[i] = pcie->bars[i].start;
        info->bar_length[i] = pcie->bars[i].length;
        info->bar_flags[i] = (u32)pcie->bars[i].flags;
    }

    return 0;
}

static int rk_pcie_check_bar_access(struct rk_pcie_dev *pcie,
                                    const struct rk_pcie_ioc_bar *bar)
{
    if (bar->bar >= RK_PCIE_MAX_BARS)
        return -EINVAL;

    if (!pcie->bars[bar->bar].vaddr)
        return -ENODEV;

    if ((bar->offset & 0x3) != 0)
        return -EINVAL;

    if (pcie->bars[bar->bar].length < sizeof(u32))
        return -EINVAL;

    if ((resource_size_t)bar->offset >
        pcie->bars[bar->bar].length - sizeof(u32))
        return -EINVAL;

    return 0;
}

static long rk_pcie_ioctl(struct file *file,
                          unsigned int cmd,
                          unsigned long arg)
{
    struct rk_pcie_dev *pcie = file->private_data;
    void __user *argp = (void __user *)arg;
    long ret = 0;

    if (!pcie)
        return -ENODEV;

    mutex_lock(&pcie->lock);

    if (pcie->dead) {
        ret = -ENODEV;
        goto out_unlock;
    }

    switch (cmd) {
    case RK_PCIE_IOC_GET_INFO: {
        struct rk_pcie_ioc_info info;

        rk_pcie_get_info(pcie, &info);
        if (copy_to_user(argp, &info, sizeof(info)))
            ret = -EFAULT;
        break;
    }

    case RK_PCIE_IOC_BAR_READ: {
        struct rk_pcie_ioc_bar bar;

        if (copy_from_user(&bar, argp, sizeof(bar))) {
            ret = -EFAULT;
            break;
        }

        ret = rk_pcie_check_bar_access(pcie, &bar);
        if (ret)
            break;

        bar.value = ioread32(pcie->bars[bar.bar].vaddr +
                             bar.offset);

        if (copy_to_user(argp, &bar, sizeof(bar)))
            ret = -EFAULT;
        break;
    }

    case RK_PCIE_IOC_BAR_WRITE: {
        struct rk_pcie_ioc_bar bar;

        if (copy_from_user(&bar, argp, sizeof(bar))) {
            ret = -EFAULT;
            break;
        }

        ret = rk_pcie_check_bar_access(pcie, &bar);
        if (ret)
            break;

        iowrite32(bar.value,
                  pcie->bars[bar.bar].vaddr + bar.offset);
        /* Flush posted MMIO write before returning to userspace. */
        (void)ioread32(pcie->bars[bar.bar].vaddr + bar.offset);
        break;
    }

    default:
        ret = -ENOTTY;
        break;
    }

out_unlock:
    mutex_unlock(&pcie->lock);
    return ret;
}

static const struct file_operations rk_pcie_fops = {
    .owner          = THIS_MODULE,
    .open           = rk_pcie_open,
    .release        = rk_pcie_release,
    .unlocked_ioctl = rk_pcie_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl   = rk_pcie_ioctl,
#endif
    .llseek         = no_llseek,
};

static void rk_pcie_unmap_bars(struct rk_pcie_dev *pcie)
{
    int i;

    for (i = 0; i < RK_PCIE_MAX_BARS; i++) {
        if (pcie->bars[i].vaddr) {
            pci_iounmap(pcie->pdev, pcie->bars[i].vaddr);
            pcie->bars[i].vaddr = NULL;
        }
    }
}

static int rk_pcie_map_bars(struct rk_pcie_dev *pcie)
{
    struct pci_dev *pdev = pcie->pdev;
    int i;

    for (i = 0; i < PCI_STD_NUM_BARS; i++) {
        unsigned long flags = pci_resource_flags(pdev, i);
        resource_size_t len;

        if (!(flags & IORESOURCE_MEM))
            continue;

        len = pci_resource_len(pdev, i);
        if (!len)
            continue;

        pcie->bars[i].start = pci_resource_start(pdev, i);
        pcie->bars[i].length = len;
        pcie->bars[i].flags = flags;
        pcie->bars[i].vaddr = pci_iomap(pdev, i, 0);

        if (!pcie->bars[i].vaddr) {
            dev_err(&pdev->dev, "failed to map BAR%d\n", i);
            return -ENOMEM;
        }

        dev_info(&pdev->dev, "BAR%d: start=%pa len=%pa flags=0x%lx mapped\n",
                 i, &pcie->bars[i].start,
                 &pcie->bars[i].length, flags);
    }

    return 0;
}

static int rk_pcie_chrdev_add(struct rk_pcie_dev *pcie)
{
    int ret;

    pcie->minor = ida_alloc_range(&rk_pcie_ida, 0,
                                  RK_PCIE_MINORS - 1, GFP_KERNEL);
    if (pcie->minor < 0)
        return pcie->minor;

    pcie->devt = MKDEV(MAJOR(rk_pcie_devt),
                       MINOR(rk_pcie_devt) + pcie->minor);

    cdev_init(&pcie->cdev, &rk_pcie_fops);
    pcie->cdev.owner = THIS_MODULE;

    ret = cdev_add(&pcie->cdev, pcie->devt, 1);
    if (ret)
        goto err_ida;

    pcie->chardev = device_create(rk_pcie_class, &pcie->pdev->dev,
                                  pcie->devt, pcie,
                                  DEV_NAME "%d", pcie->minor);
    if (IS_ERR(pcie->chardev)) {
        ret = PTR_ERR(pcie->chardev);
        pcie->chardev = NULL;
        goto err_cdev;
    }

    return 0;

err_cdev:
    cdev_del(&pcie->cdev);
err_ida:
    ida_free(&rk_pcie_ida, pcie->minor);
    pcie->minor = -1;
    return ret;
}

static void rk_pcie_chrdev_del(struct rk_pcie_dev *pcie)
{
    if (pcie->chardev) {
        device_destroy(rk_pcie_class, pcie->devt);
        pcie->chardev = NULL;
    }

    cdev_del(&pcie->cdev);

    if (pcie->minor >= 0) {
        ida_free(&rk_pcie_ida, pcie->minor);
        pcie->minor = -1;
    }
}

static int rk_pcie_probe(struct pci_dev *pdev,
                         const struct pci_device_id *id)
{
    struct rk_pcie_dev *pcie;
    int ret;

    pcie = kzalloc(sizeof(*pcie), GFP_KERNEL);
    if (!pcie)
        return -ENOMEM;

    pcie->pdev = pci_dev_get(pdev);
    pcie->minor = -1;
    mutex_init(&pcie->lock);
    kref_init(&pcie->refcount);

    ret = pci_enable_device_mem(pdev);
    if (ret)
        goto err_put;

    ret = pci_request_regions(pdev, DRV_NAME);
    if (ret)
        goto err_disable;
    pcie->regions_requested = true;

    ret = rk_pcie_map_bars(pcie);
    if (ret)
        goto err_unmap;

    pci_set_drvdata(pdev, pcie);

    ret = rk_pcie_chrdev_add(pcie);
    if (ret)
        goto err_drvdata;

    dev_info(&pdev->dev,
             "bound %04x:%04x, char device /dev/%s%d\n",
             pdev->vendor, pdev->device, DEV_NAME, pcie->minor);
    return 0;

err_drvdata:
    pci_set_drvdata(pdev, NULL);
err_unmap:
    rk_pcie_unmap_bars(pcie);
    if (pcie->regions_requested) {
        pci_release_regions(pdev);
        pcie->regions_requested = false;
    }
err_disable:
    pci_disable_device(pdev);
err_put:
    kref_put(&pcie->refcount, rk_pcie_release_ref);
    return ret;
}

static void rk_pcie_remove(struct pci_dev *pdev)
{
    struct rk_pcie_dev *pcie = pci_get_drvdata(pdev);

    if (!pcie)
        return;

    /* Stop new opens first, then mark existing file descriptors dead. */
    rk_pcie_chrdev_del(pcie);

    mutex_lock(&pcie->lock);
    pcie->dead = true;
    rk_pcie_unmap_bars(pcie);
    if (pcie->regions_requested) {
        pci_release_regions(pdev);
        pcie->regions_requested = false;
    }
    if (pcie->enabled) {
        pci_disable_device(pdev);
        pcie->enabled = false;
    } else {
        pci_disable_device(pdev);
    }
    mutex_unlock(&pcie->lock);

    pci_set_drvdata(pdev, NULL);
    dev_info(&pdev->dev, "device removed\n");

    /* Existing file descriptors retain the object, but ioctls return ENODEV. */
    kref_put(&pcie->refcount, rk_pcie_release_ref);
}

/*
 * Empty static ID table: bind explicitly through sysfs new_id.
 * This avoids accidentally binding an unrelated PCI device.
 */
static const struct pci_device_id rk_pcie_ids[] = {
    { 0, }
};
MODULE_DEVICE_TABLE(pci, rk_pcie_ids);

static struct pci_driver rk_pcie_driver = {
    .name       = DRV_NAME,
    .id_table   = rk_pcie_ids,
    .probe      = rk_pcie_probe,
    .remove     = rk_pcie_remove,
};

static int __init rk_pcie_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&rk_pcie_devt, 0,
                              RK_PCIE_MINORS, DEV_NAME);
    if (ret)
        return ret;

    rk_pcie_class = class_create(THIS_MODULE, DEV_NAME);
    if (IS_ERR(rk_pcie_class)) {
        ret = PTR_ERR(rk_pcie_class);
        unregister_chrdev_region(rk_pcie_devt, RK_PCIE_MINORS);
        return ret;
    }

    ret = pci_register_driver(&rk_pcie_driver);
    if (ret) {
        class_destroy(rk_pcie_class);
        unregister_chrdev_region(rk_pcie_devt, RK_PCIE_MINORS);
        return ret;
    }

    pr_info(DRV_NAME ": loaded\n");
    return 0;
}

static void __exit rk_pcie_exit(void)
{
    pci_unregister_driver(&rk_pcie_driver);
    class_destroy(rk_pcie_class);
    unregister_chrdev_region(rk_pcie_devt, RK_PCIE_MINORS);
    ida_destroy(&rk_pcie_ida);
    pr_info(DRV_NAME ": unloaded\n");
}

module_init(rk_pcie_init);
module_exit(rk_pcie_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rk-pcie-sdk");
MODULE_DESCRIPTION("RK3588 FPGA PCIe access driver - Phase 1");
MODULE_VERSION("0.1.0");
