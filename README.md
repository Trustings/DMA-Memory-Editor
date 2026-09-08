# DMA-Memory-Editor

DMA-Memory-Editor is an x64dbg inspired memory editor capable of inspecting processes and their complete address space within a Windows target machine. It works over a PCIe FPGA device, or against a virtual machine through shared memory or hugepages. It runs on both Linux and Windows.

<img width="1920" height="1080" alt="Screenshot From 2026-08-15 15-58-10" src="https://github.com/user-attachments/assets/469211e4-9f8d-473e-b0a2-8288ab43d9b2" />

# Features

* Full-address-space memory scanning across every readable VAD region (modules, heaps, stacks, private allocations, TEBs), issued through the MemProcFS scatter API so a scan is a handful of DMA round-trips rather than thousands
* Scan types: byte, word, dword, qword, float, double and string
* Comparison modes: exact value, increased, decreased, unchanged, changed, increased/decreased by an amount, and increased/decreased by a percentage
* Memory writing, typed to the scanned value (writing `1.5` to a float writes 1.5)
* Hardware watchpoints — "find what accesses this address" — via the QEMU gdbstub (Linux only)
* Scans run on a background thread with live progress and a cancel button; the UI stays responsive

# Requirements

## Linux

Configure QEMU with the sockets the editor talks to:

```
-qmp unix:/tmp/qmp-win10.sock,server,nowait
-qmp unix:/tmp/qmp-win10-1.sock,server,nowait
-gdb tcp::1234
```

These paths are defaults, not requirements — see [Configuration](#configuration) if yours differ.

Set the LeechCore QEMU backend up as described at <https://github.com/ufrisk/LeechCore/wiki/Device_QEMU>.

For the debugging features you need gdb:

```
sudo apt install gdb
```

Build dependencies (Debian/Ubuntu):

```
sudo apt install build-essential cmake pkg-config \
                 libglfw3-dev libvulkan-dev libusb-1.0-0-dev liblz4-dev
```

**glibc version:** the prebuilt `vmm.so` and `leechcore.so` in `lib/` are linked against **glibc 2.38 or newer** (Ubuntu 24.04, Debian trixie). On an older distribution they fail to link with errors like `undefined reference to __isoc23_strtoull@GLIBC_2.38`. Either build on a current distribution, or build MemProcFS and LeechCore yourself and drop the resulting `.so` files into `lib/`.

**Permissions:** reading guest memory through hugepages, and mounting MemProcFS for DTB recovery, both generally require root. If you would rather not grant that, run with `--no-mount` to skip the MemProcFS mount entirely; scanning still works, but recovering a process whose DTB is wrong will not.

## Windows

Meet the requirements at <https://github.com/ufrisk/LeechCore/wiki/Device_FPGA> and <https://github.com/ufrisk/MemProcFS>.

You will also need the Vulkan SDK and a GLFW import library; prebuilt `.lib` files are in `lib/`.

# Building

## Linux

```
git clone https://github.com/Trustings/DMA-Memory-Editor.git
cd DMA-Memory-Editor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The MemProcFS/LeechCore shared objects in `lib/` are copied next to the executable and found at runtime through an `$ORIGIN` rpath.

## Windows

```
git clone https://github.com/Trustings/DMA-Memory-Editor.git
```

Open the Visual Studio solution (`.sln`) and build **x64 Release**.

# Configuration

Everything that used to be hardcoded is configurable. Run `DMA-Memory-Editor --help` for the full list, or copy `dma-editor.conf.example` to `dma-editor.conf` next to the executable.

Common cases:

```bash
# Talk to an FPGA device instead of a VM
./DMA-Memory-Editor --device fpga

# A guest whose QMP socket lives somewhere else
./DMA-Memory-Editor --qmp-socket /tmp/qmp-win11.sock

# A fully explicit device string, skipping autodetection
./DMA-Memory-Editor --device 'qemu://hugepage-pid=4242,qmp=/tmp/qmp.sock'

# Skip the MemProcFS mount (no root needed, no DTB recovery)
./DMA-Memory-Editor --no-mount

# A gdbstub on a different port
./DMA-Memory-Editor --gdb-target localhost:1235
```

Command line options override the config file, which overrides the built-in defaults.

# Notes and limitations

**Watchpoint scope.** Watchpoints are set on guest *virtual* addresses through the QEMU gdbstub, which knows nothing about guest processes. A watchpoint on `0x7ff...` will therefore also fire in any other guest process that happens to have that virtual address mapped. Read the results as "something in the guest touched this VA", not "the attached process touched it".

**Watchpoint count.** x86 has four hardware debug registers. Ask for more than that and gdb silently falls back to a software watchpoint, which single-steps the entire guest and is unusably slow. The editor warns when it detects this.

**TSC.** If you use the debugging features, be aware that some software can detect it by checking the virtual machine's TSC (Time Stamp Counter).

# Other tools

For static analysis you can use <https://github.com/Trustings/DMA-PE-Dumper>.

# License

MIT — see [LICENSE](LICENSE).
