/*
 * QEMU NoirVisor CVM support
 *
 * Copyright (c) 2022 Zero Tang
 *  Written by:
 *  Zero Tang <zero.tangptr@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef NOIRCV_INTERFACE_H
#define NOIRCV_INTERFACE_H
 
#include "cpu.h"
#include "sysemu/noircv.h"

// Definitions of Status Codes of NoirVisor.
#define noir_success					0
#define noir_unsuccessful				0xC0000000
#define noir_insufficient_resources		0xC0000001
#define noir_not_implemented			0xC0000002
#define noir_unknown_processor			0xC0000003
#define noir_invalid_parameter			0xC0000004
#define noir_hypervision_absent			0xC0000005
#define noir_vcpu_already_created		0xC0000006
#define noir_buffer_too_small			0xC0000007
#define noir_vcpu_not_exist				0xC0000008

#ifdef CONFIG_WIN32
typedef ULONG32 NOIR_STATUS;
typedef ULONG64 CVM_HANDLE;
typedef ULONG64 PCVM_HANDLE;

typedef BYTE	u8;
typedef USHORT	u16;
typedef ULONG32	u32;
typedef ULONG64	u64;
#endif

typedef enum _cv_vcpu_option_type
{
	NoirCvmGuestVpOptions,
	NoirCvmExceptionBitmap,
	NoirCvmSchedulingPriority
}cv_vcpu_option_type,*cv_vcpu_option_type_p;

typedef struct _cv_gpr_state
{
	u64 rax;
	u64 rcx;
	u64 rdx;
	u64 rbx;
	u64 rsp;
	u64 rbp;
	u64 rsi;
	u64 rdi;
	u64 r8;
	u64 r9;
	u64 r10;
	u64 r11;
	u64 r12;
	u64 r13;
	u64 r14;
	u64 r15;
}cv_gpr_state,*cv_gpr_state_p;

typedef struct _cv_cr_state
{
	u64 cr0;
	u64 cr3;
	u64 cr4;
}cv_cr_state,*cv_cr_state_p;

typedef union _r128
{
	float f[4];
	double d[2];
}r128;

typedef struct _cv_xmm_state
{
	r128 xmm0;
	r128 xmm1;
	r128 xmm2;
	r128 xmm3;
	r128 xmm4;
	r128 xmm5;
	r128 xmm6;
	r128 xmm7;
	r128 xmm8;
	r128 xmm9;
	r128 xmm10;
	r128 xmm11;
	r128 xmm12;
	r128 xmm13;
	r128 xmm14;
	r128 xmm15;
}cv_xmm_state,*cv_xmm_state_p;

// 512-byte region of fxsave instruction.
// Including fPU, x87, XMM.
typedef struct _cv_fx_state
{
	struct
	{
		u16 fcw;
		u16 fsw;
		u8 ftw;
		u8 reserved;
		u16 fop;
		u32 fip;
		u16 fcs;
		u16 reserved1;
#if defined(_WIN64)
		u64 fdp;
#else
		u32 fdp;
		u16 fds;
		u16 reserved2;
#endif
		u32 fxcsr;
		u32 fxcsrfask;
	}fpu;
	struct
	{
		u64 fm0;		// St0
		u64 reserved0;
		u64 fm1;		// St1
		u64 reserved1;
		u64 fm2;		// St2
		u64 reserved2;
		u64 fm3;		// St3
		u64 reserved3;
		u64 fm4;		// St4
		u64 reserved4;
		u64 fm5;		// St5
		u64 reserved5;
		u64 fm6;		// St6
		u64 reserved6;
		u64 fm7;		// St7
		u64 reserved7;
	}x87;
	cv_xmm_state sse;
#if defined(_WIN64)
	u64 reserved[6];
#else
	u64 reserved[22];
#endif
	u64 available[6];
}cv_fx_state,*cv_fx_state_p;

typedef struct _cv_seg_reg
{
	u16 Selector;
	u16 Attributes;
	u32 Limit;
	u64 Base;
}cv_seg_reg,*cv_seg_reg_p;

typedef struct _cv_sr_state
{
	cv_seg_reg Es;
	cv_seg_reg Cs;
	cv_seg_reg Ss;
	cv_seg_reg Ds;
}cv_sr_state,*cv_sr_state_p;

#define cv_memtype_uc		0
#define cv_memtype_wc		1
#define cv_memtype_wt		4
#define cv_memtype_wp		5
#define cv_memtype_wb		6
#define cv_memtype_ucm		7

typedef struct _cv_addr_map_info
{
	u64 GPA;
	u64 HVA;
	u32 NumberOfPages;
	union
	{
		struct
		{
			u32 Present:1;
			u32 Write:1;
			u32 Execute:1;
			u32 User:1;
			u32 Caching:3;
			u32 PageSize:2;
			u32 Reserved:23;
		};
		u32 Value;
	}Attributes;
}cv_addr_map_info,*cv_addr_map_info_p;

typedef enum _cv_reg_type
{
	cv_gpr,
	cv_flags,
	cv_ip,
	cv_cr,
	cv_cr2,
	cv_dr,
	cv_dr67,
	cv_sr,
	cv_fg,
	cv_dt,
	cv_lt,
	cv_syscall_msr,
	cv_sysenter_msr,
	cv_cr8,
	cv_fx,
	cv_xsave,
	cv_xcr0,
	cv_efer,
	cv_pat,
	cv_maximum
}cv_reg_type,*cv_reg_type_p;

typedef enum _cv_intercept_code
{
	cv_invalid_state=0,
	cv_shutdown_condition=1,
	cv_memory_access=2,
	cv_init_signal=3,
	cv_hlt_instruction=4,
	cv_io_instruction=5,
	cv_cpuid_instruction=6,
	cv_rdmsr_instruction=7,
	cv_wrmsr_instruction=8,
	cv_cr_access=9,
	cv_dr_access=10,
	cv_hypercall=11,
	cv_exception=12,
	cv_rescission=13,
	cv_interruptWindow=14,
	cv_scheduler_exit=0x80000000,
	cv_scheduler_pause=0x80000001
}cv_intercept_code,*cv_intercept_code_p;

typedef struct _cv_cr_access_context
{
	struct
	{
		u32 cr_num:4;
		u32 gpr_num:4;
		u32 mov:1;
		u32 write:1;
		u32 reserved:22;
	};
}cv_cr_access_context,*cv_cr_access_context_p;

typedef struct _cv_dr_access_context
{
	struct
	{
		u32 dr_num:4;
		u32 gpr_num:4;
		u32 write:1;
		u32 reserved:23;
	};
}cv_dr_access_context,*cv_dr_access_context_p;

typedef struct _cv_exception_context
{
	struct
	{
		u32 Vector:5;
		u32 EvValid:1;
		u32 Reserved:26;
	};
	u32 ErrorCode;
	u64 PageFaultAddress;
	u8 FetchedBytes;
	u8 InstructionBytes[15];
}cv_exception_context,*cv_exception_context_p;

typedef struct _cv_io_context
{
	struct
	{
		u16 io_type:1;
		u16 string:1;
		u16 repeat:1;
		u16 operand_size:3;
		u16 address_width:4;
		u16 reserved:6;
	}access;
	u16 port;
	u64 rax;
	u64 rcx;
	u64 rsi;
	u64 rdi;
	cv_seg_reg segment;
}cv_io_context,*cv_io_context_p;

typedef struct _cv_msr_context
{
	u32 eax;
	u32 edx;
	u32 ecx;
}cv_msr_context,*cv_msr_context_p;

typedef struct _cv_memory_access_context
{
	struct
	{
		u8 read:1;
		u8 write:1;
		u8 execute:1;
		u8 user:1;
		u8 fetched_bytes:4;
	}access;
	u8 instruction_bytes[15];
	u64 gpa;
}cv_memory_access_context,*cv_memory_access_context_p;

typedef struct _cv_cpuid_context
{
	struct
	{
		u32 eax;
		u32 ecx;
	}leaf;
}cv_cpuid_context,*cv_cpuid_context_p;

typedef struct _cv_exit_context
{
	cv_intercept_code intercept_code;
	union
	{
		cv_cr_access_context cr_access;
		cv_dr_access_context dr_access;
		cv_exception_context exception;
		cv_io_context io;
		cv_msr_context msr;
		cv_memory_access_context memory_access;
		cv_cpuid_context cpuid;
	};
	cv_seg_reg cs;
	u64 rip;
	u64 rflags;
	u64 next_rip;
	struct
	{
		u32 cpl:2;
		u32 pe:1;
		u32 lm:1;
		u32 interrupt_shadow:1;
		u32 instruction_length:4;
		u32 reserved:23;
	}vp_state;
}cv_exit_context,*cv_exit_context_p;

typedef NOIR_STATUS noir_status;
typedef CVM_HANDLE cv_handle;
#endif