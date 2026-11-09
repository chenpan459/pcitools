# rk-pcie-sdk v0.1.0

RK3588 host driver for the Rockchip PCIe endpoint `1d87:356a`.

## 1. Scope

Implemented, for the Rockchip endpoint `1d87:356a` (BAR0 4MB control, BAR2 64MB window, BAR4 1MB DMA):

- PCI probe/remove and the static ID `1d87:356a`
- misc device `/dev/pcie-rkep-<BDF>`
- one ioctl ABI, `include/rk_pcie_uapi.h` (the old magic-`R` diagnostic header is retired)
- device information, plus 32-bit BAR read/write
- coherent DMA buffer alloc, mmap, and blocking single-direction H2C/C2H on channel 0 or 1
- DMA completion by status poll, with a 1 second MSI wait when requested
- user interrupts by virtual id

Not in this driver:

- Xilinx XDMA
- more than two DMA channels
- a userspace library above the ioctl ABI

DMA controller interrupts stay masked until userspace selects MSI completion.
Channel 0 and channel 1 each have their own engine mutex, held across the transfer.

## 2. Requirements

- RK3588 running Linux 6.1
- Kernel build tree prepared for the exact running kernel
- `CONFIG_PCI=y`
- `CONFIG_PCI_MSI=y` (probe requests MSI vectors; DMA completion still polls unless asked)
- Rockchip endpoint `1d87:356a` enumerates successfully
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
sudo insmod kernel/pcie-rkep.ko
dmesg | tail -n 50
```

`1d87:356a` is in the driver's ID table and binds on insert. For any other ID:

```bash
sudo ./scripts/bind_device.sh 1d87 356a
```

Check:

```bash
lspci -nnk -s 03:00.0
ls -l /dev/pcie-rkep*
dmesg | tail -n 100
```

## 5. Build and run user test

```bash
cd user
make
```

The safe BAR check reads the object version register and compares it with `GET_INFO`.
On this card that register is BAR0 offset `0x4` (magic is offset `0x0`, value `0x524b4550`).

```bash
sudo ./pcie_test info /dev/pcie-rkep-0000:03:00.0
sudo ./pcie_test read /dev/pcie-rkep-0000:03:00.0 0 0x4
```

BAR0 bytes below `0x400` and BAR4 (the DMA registers) are not writable. A write there
returns `EPERM`.

```bash
sudo ./pcie_test write /dev/pcie-rkep-0000:03:00.0 2 0x0 0x1
```

Blocking DMA uses a coherent buffer. `EP_ADDR` is an endpoint address in spare memory,
not the host BAR address. Channel defaults to 0. Polling is the default; MSI is opt-in
and depends on the endpoint forwarding DMA done.

```bash
sudo ./pcie_test h2c /dev/pcie-rkep-0000:03:00.0 EP_ADDR 0x1000
sudo ./pcie_test c2h /dev/pcie-rkep-0000:03:00.0 EP_ADDR 0x1000
sudo ./pcie_test loop /dev/pcie-rkep-0000:03:00.0 EP_ADDR 0x1000
sudo ./pcie_test dma-mode /dev/pcie-rkep-0000:03:00.0 msi
sudo ./pcie_test dma-mode /dev/pcie-rkep-0000:03:00.0 poll
```

Replace `EP_ADDR` with an endpoint address from the firmware memory map. It is not the host BAR2 address (`0x3ffe0000000` on the enumerated card).

`loop` writes the endpoint buffer. Do not point it at firmware.

User interrupts keep the virtual-id wait. `irq-wait` returns when the endpoint raises
that id, or when the timeout expires.

```bash
sudo ./pcie_test irq-alloc /dev/pcie-rkep-0000:03:00.0
sudo ./pcie_test irq-wait /dev/pcie-rkep-0000:03:00.0 1 1000
sudo ./pcie_test irq-free /dev/pcie-rkep-0000:03:00.0 1
```

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
sudo rmmod pcie-rkep
```

## 8. Important engineering notes

1. The public ABI is `include/rk_pcie_uapi.h`. `include/rk_pcie_ioctl.h` only includes it.
2. `1d87:356a` matches the ID table. Confirm with `lspci -nn` before binding anything else.
3. Two drivers cannot own the same PCI function.
4. BAR reads and writes are serialized. BAR4 is not writable from userspace; the kernel
   owns the DMA doorbell.
5. DMA defaults to masked interrupts and status polling, with its own timeout. `dma-mode msi`
   unmasks the controller and waits up to one second for the endpoint to forward completion.
6. Host DMA addresses must come from `PCIE_EP_CONTINUOUS_BUFFER_ALLOC` on the same fd.
7. Do not map a BAR as normal cached memory. Coherent DMA buffers are mapped with
   `dma_mmap_coherent`.
8. Start with `pcie_test info`. It reads the BAR0 version register and fails if that read
   disagrees with `GET_INFO`.

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
