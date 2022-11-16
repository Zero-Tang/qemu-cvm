/*
 * QEMU NoirVisor CVM support
 *
 * Copyright Zero Tang, 2022
 *
 * Authors:
 *  Zero Tang   <zero.tangptr@gmail.com>
 *
 * Copyright (c) 2022 Zero Tang
 *  Written by:
 *  Zero Tang <zero.tangptr@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include <winioctl.h>
#include <windef.h>

#define NV_DEVICE_NAME			L"\\\\.\\NoirVisor"

// Definitions of I/O Control Codes of NoirVisor Driver CVM Interface.
#define CTL_CODE_GEN(i)		CTL_CODE(FILE_DEVICE_UNKNOWN,i,METHOD_BUFFERED,FILE_ANY_ACCESS)

#define IOCTL_CvmCreateVm		CTL_CODE_GEN(0x880)
#define IOCTL_CvmDeleteVm		CTL_CODE_GEN(0x881)
#define IOCTL_CvmSetMapping		CTL_CODE_GEN(0x882)
#define IOCTL_CvmQueryGpaAdMap  CTL_CODE_GEN(0x883)
#define IOCTL_CvmClearGpaAdMap  CTL_CODE_GEN(0x884)
#define IOCTL_CvmCreateVmEx     CTL_CODE_GEN(0x885)
#define IOCTL_CvmQueryHvStatus	CTL_CODE_GEN(0x88F)
#define IOCTL_CvmCreateVcpu		CTL_CODE_GEN(0x890)
#define IOCTL_CvmDeleteVcpu		CTL_CODE_GEN(0x891)
#define IOCTL_CvmRunVcpu		CTL_CODE_GEN(0x892)
#define IOCTL_CvmViewVcpuReg	CTL_CODE_GEN(0x893)
#define IOCTL_CvmEditVcpuReg	CTL_CODE_GEN(0x894)
#define IOCTL_CvmRescindVcpu	CTL_CODE_GEN(0x895)
#define IOCTL_CvmInjectEvent	CTL_CODE_GEN(0x896)
#define IOCTL_CvmSetVcpuOptions	CTL_CODE_GEN(0x897)