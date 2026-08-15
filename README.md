# DMA-Memory-Editor

DMA-Memory-Editor is a x64dbg inspired DMA memory editor capable of inspecting a target windows machine, its processes and their complete address space. It can work over a PCIe FPGA device or if utilizing a Virtual Machine, shared memory or hugepages. It is currently cross compatible with Linux and Windows.

<img width="1920" height="1080" alt="Screenshot From 2026-08-15 15-58-10" src="https://github.com/user-attachments/assets/469211e4-9f8d-473e-b0a2-8288ab43d9b2" />

# Features
  [v1.0.0](https://github.com/Trustings/DMA-Memory-Editor/releases/tag/v1.0.0)
  
• Memory Searching                                  
• Memory Overwriting                                  
• Hardware watchpoints (currently only supported on linux)                          

# Downloading

# Linux



# Building:

# Linux

```
git clone --recursive https://github.com/Trustings/DMA_PE_Dumper.git
mkdir build
cd build
cmake ../
make
```
Download the linux project binaries at https://github.com/Trustings/DMA_PE_Dumper/releases/tag/v1.1.0 and have them extracted to your working build directory

Please note: To have this run successfully on your Linux machine you must configure your backend as such. https://github.com/ufrisk/LeechCore/wiki/Device_QEMU 

# Windows 

```
git clone --recursive https://github.com/Trustings/DMA_PE_Dumper.git
```
After cloning or downloading the repository, open the visual studio solution file (.slnx) and compile as x64 Release.

Download the Windows project binaries at https://github.com/Trustings/DMA_PE_Dumper/releases/tag/v1.1.0 and have them extracted to your working build directory

Please note: To have this run successfully on your Windows machine you must have first met these requirements at https://github.com/ufrisk/LeechCore/wiki/Device_FPGA and at https://github.com/ufrisk/MemProcFS


# Examples:

Once built, cd into the working build directory, input the name of either a system driver, a target exe, or a target exe with an associative dll.

EXAMPLE 1 -> ./DMA_PE_Dumper YourTarget.sys

EXAMPLE 2 -> ./DMA_PE_Dumper YourTarget.exe 

EXAMPLE 3 -> ./DMA_PE_Dumper YourTarget.exe YourTarget.dll

