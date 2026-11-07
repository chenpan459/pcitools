# rk-pcie-sdk v0.1.0

RK3588 / Linux 6.1 FPGA PCIe access driver, Phase 1.

## 1. Phase 1 scope

Implemented:

- PCI driver probe/remove
- Explicit dynamic PCI ID binding through `new_id`
- PCI memory BAR resource request and mapping
- `/dev/rk-pcieN` character device
- `ioctl`: device information, BAR 32-bit read/write
- Basic user-space test utility

Not implemented yet:

- XDMA H2C/C2H engine programming
- DMA buffer allocation and mmap
- MSI/MSI-X event delivery
- Async transfer queues
- Performance benchmark

Do not treat this phase as a working XDMA DMA driver. XDMA register layout, channel count,
interrupt mode, descriptor format and IP version must be confirmed from the FPGA build.

## 2. Requirements

- RK3588 running Linux 6.1
- Kernel build tree prepared for the exact running kernel
- `CONFIG_PCI=y`
- `CONFIG_PCI_MSI` is recommended for later phases
- PCIe FPGA endpoint enumerates successfully
- Root privileges for driver binding and BAR writes

## 3. Build kernel module

On the RK3588 target, if matching kernel headers/build tree are installed:

```bash
cd kernel
make KDIR=/lib/modules/$(uname -r)/build ARCH=arm64
```

Cross compile on host:

```bash
make -C kernel \
  KDIR=/path/to/rk3588/linux-6.1 \
  ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu-
```

`KDIR` must be the prepared output/build directory for the exact kernel configuration and
version running on the board. If the kernel uses a separate `O=` output directory, point
`KDIR` to that output directory, not merely the source tree.

## 4. Load driver

```bash
sudo insmod kernel/rk_pcie_drv.ko
dmesg | tail -n 50
```

The driver intentionally uses an empty static PCI ID table to avoid binding unrelated
devices. Add the actual FPGA IDs dynamically.

Example only (replace with the IDs reported by `lspci -nn`):

```bash
sudo sh -c 'echo 10ee 903f > /sys/bus/pci/drivers/rk-pcie-sdk/new_id'
```

If the device is already bound to another driver, unbind it first. Do not unbind a production
device without understanding the impact.

Check:

```bash
lspci -nnk
ls -l /dev/rk-pcie*
dmesg | tail -n 100
```

You can also use:

```bash
sudo ./scripts/bind_device.sh 10ee 903f
```

The script assumes the device is not already bound to another driver.

## 5. Build and run user test

```bash
cd user
make
```

Device information:

```bash
sudo ./pcie_test info /dev/rk-pcie0
```

Read BAR0 register at offset 0:

```bash
sudo ./pcie_test read /dev/rk-pcie0 0 0x0
```

Write BAR0 register:

```bash
sudo ./pcie_test write /dev/rk-pcie0 0 0x8 0x1
```

WARNING: BAR writes can change FPGA state. Only write offsets documented by the FPGA
hardware design. Do not write arbitrary offsets.

## 6. Link status

`link_speed` in the ioctl result is the PCIe Link Status speed encoding:

- 1: 2.5 GT/s
- 2: 5.0 GT/s
- 3: 8.0 GT/s
- 4: 16.0 GT/s
- 5: 32.0 GT/s
- 6: 64.0 GT/s

The user tool currently prints the raw encoding. For a human-readable check, use:

```bash
lspci -s <BDF> -vv | grep -E 'LnkCap|LnkSta'
```

## 7. Unload

```bash
sudo rmmod rk_pcie_drv
```

## 8. Important engineering notes

1. This driver is a generic PCIe BAR access skeleton, not the vendor XDMA Linux driver.
2. The actual Xilinx XDMA PCI Vendor/Device IDs vary with the FPGA PCIe IP configuration.
   Do not assume `10ee:903f`; obtain the real values from `lspci -nn`.
3. Before integrating this driver with XDMA, inspect whether the vendor XDMA driver is already
   bound. Two drivers cannot own the same PCI function simultaneously.
4. BAR register reads/writes are serialized by a mutex. This is appropriate for Phase 1
   diagnostics, not high-throughput data paths.
5. No DMA bus mastering is enabled in this phase.
6. Do not map a BAR as normal cached memory. Use proper I/O accessors.
7. Before Phase 2, confirm the XDMA IP version, BAR map, H2C/C2H channel count, descriptor
   mode, interrupt mode, FPGA DMA address width, and RK3588 IOMMU/SMMU behavior.
8. The kernel module should be tested first with a harmless read-only FPGA ID/version
   register. Avoid testing writes until the register map is confirmed.

## 9. Troubleshooting

```bash
lspci -nn
lspci -nnk
lspci -s <BDF> -vv
cat /sys/bus/pci/devices/<BDF>/resource
dmesg | grep -iE 'pcie|pci|iommu|smmu|rk-pcie'
```

If `new_id` is missing, check that the module loaded and the driver registered successfully.
If `new_id` succeeds but probe fails, inspect the kernel log for BAR resource conflicts,
mapping failures or endpoint link issues.
