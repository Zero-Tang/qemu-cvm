# QEMU-CVM Fork
This repository serves as a subproject of Project NoirVisor. The purpose of this fork is to implement an accelerator for QEMU based on NoirVisor CVM and NoirVisor NSV.

## Original Readme
Please read the [README_repo.rst](./README_repo.rst) for the original repository's readme.

## Compilation
Make sure you added `--enable-noircv` option while configuring the build system in order to build NoirVisor CVM accelerator. \
For further documentation about building QEMU, please read [QEMU's wiki](https://wiki.qemu.org/Hosts/W32#Native_builds_with_MSYS2).

## Run
Because this is a fork from QEMU v7.0.0, please download the [installer for QEMU v7.0.0 on 64-bit Windows](https://qemu.weilnetz.de/w64/2022/qemu-w64-setup-20220419.exe). Replace the `qemu-system-x86_64.exe` file.

## License
The NoirVisor CVM accelerator is licensed under the GPLv2 License. For other components of QEMU, consult [LICENSE](./LICENSE) file for further details.