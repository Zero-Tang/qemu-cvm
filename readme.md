# QEMU-CVM Fork
This repository serves as a subproject of Project NoirVisor. The purpose of this fork is to implement an accelerator for QEMU based on NoirVisor CVM and NoirVisor NSV.

## Original Readme
Please read the [README_repo.rst](./README_repo.rst) for the original repository's readme.

## Contribution
The codes of NoirVisor CVM accelerator is located in the [noircv](./target/i386/noircv/readme.md) directory.

## Compilation
Make sure you added `--enable-noircv` option while configuring the build system in order to build NoirVisor CVM accelerator. \
For further documentation about building QEMU, please read [QEMU's wiki](https://wiki.qemu.org/Hosts/W32#Native_builds_with_MSYS2).

I recommend to configure QEMU with the following options:
```
../configure --target-list=x86_64-softmmu --enable-whpx --enable-hax --enable-noircv --enable-tools --enable-lzo --enable-bzip2 --enable-sdl --enable-gtk --enable-vdi --enable-qcow1 --disable-capstone --disable-qga-vss --enable-debug
```
Make sure you are entering this command from MinGW64 console shell from `build` directory.

## Debug
In order to debug QEMU, you must configure the `--enable-debug` option.

## Run
You may copy dynamic-link library files from msys2 to the QEMU build directory in order to resolve QEMU's library dependencies.
```
cp /mingw64/bin/lib*.dll ./
```
Make sure you are entering this command from MinGW64 console shell from `build` directory.

## License
The NoirVisor CVM accelerator is licensed under the GPLv2 License. For other components of QEMU, consult [LICENSE](./LICENSE) file for further details.