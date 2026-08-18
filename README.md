# DMA-Memory-Editor

DMA-Memory-Editor is a x64dbg inspired DMA memory editor capable of inspecting processes and their complete address space within a windows target machine. It can work over a PCIe FPGA device or if utilizing a Virtual Machine, shared memory or hugepages. It is currently cross compatible with Linux and Windows.

<img width="1920" height="1080" alt="Screenshot From 2026-08-15 15-58-10" src="https://github.com/user-attachments/assets/469211e4-9f8d-473e-b0a2-8288ab43d9b2" />

# Features:
  [v1.0.0](https://github.com/Trustings/DMA-Memory-Editor/releases/tag/v1.0.0)
  
• Memory Searching                       

• Memory Overwriting                              

• Hardware Watchpoints (currently only supported on linux)                          

# Requirements:

# Linux

You must have these socket connections configured in your qemu configuration.

`-qmp unix:/tmp/qmp-win10.sock,server,nowait`     

`-qmp unix:/tmp/qmp-win10-1.sock,server,nowait`       

`-gdb tcp::1234`   

To use the debugging features you must have gdb installed       

Debian/Ubuntu
```
sudo apt install gdb 
```

Please note: To have this run successfully on your Linux machine you must configure your backend as such. https://github.com/ufrisk/LeechCore/wiki/Device_QEMU 

# Windows

Please note: To have this run successfully on your Windows machine you must have first met these requirements at https://github.com/ufrisk/LeechCore/wiki/Device_FPGA and at https://github.com/ufrisk/MemProcFS

# Downloading:

You can download the compiled versions at https://github.com/Trustings/DMA-Memory-Editor/releases/tag/v1.0.0



# Building:

# Linux

```
git clone --recursive https://github.com/Trustings/DMA-Memory-Editor.git
mkdir build
cd build
cmake ../
make
```
Download the linux project binaries at https://github.com/Trustings/DMA-Memory-Editor/releases/tag/v1.0.0 and have them extracted to your working build directory  


# Windows 

```
git clone --recursive https://github.com/Trustings/DMA-Memory-Editor.git
```
After cloning or downloading the repository, open the visual studio solution file (.sln) and compile as x64 Release.

Download the Windows project binaries at https://github.com/Trustings/DMA-Memory-Editor/releases/tag/v1.0.0 and have them extracted to your working build directory

# Important Information: 
If you plan on using the debugging features know that some software is able to detect this by checking your Virtual Machine's TSC (Time Stamp Counter)

# Other Tools:

For static analysis you can use https://github.com/Trustings/DMA-PE-Dumper 
