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
#define noir_emu_dual_memory_operands	0x43000000
#define noir_unsuccessful				0xC0000000
#define noir_insufficient_resources		0xC0000001
#define noir_not_implemented			0xC0000002
#define noir_unknown_processor			0xC0000003
#define noir_invalid_parameter			0xC0000004
#define noir_hypervision_absent			0xC0000005
#define noir_vcpu_already_created		0xC0000006
#define noir_buffer_too_small			0xC0000007
#define noir_vcpu_not_exist				0xC0000008
#define noir_emu_not_emulatable			0xC3000000
#define noir_emu_unknown_instruction	0xC3000001

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
	cv_vcpu_options,
	cv_exception_bitmap
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

typedef struct _cv_dr_state
{
	u64 dr0;
	u64 dr1;
	u64 dr2;
	u64 dr3;
}cv_dr_state,*cv_dr_state_p;

typedef struct _cv_dr67_state
{
	u64 dr6;
	u64 dr7;
}cv_dr67_state,*cv_dr67_state_p;

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
typedef struct __attribute__((aligned(8))) _cv_fx_state
{
	uint16_t fcw;
	uint16_t fsw;
	uint8_t ftw;
	uint8_t res1;
	uint16_t fop;
	union
	{
		struct
		{
			uint32_t fip;
			uint16_t fcs;
			uint16_t res2;
		};
		uint64_t fpu_ip;
	};
	union
	{
		struct
		{
			uint32_t fdp;
			uint16_t fds;
			uint16_t res3;
		};
		uint64_t fpu_dp;
	};
	uint32_t mxcsr;
	uint32_t mxcsr_mask;
	uint8_t st_mm[8][16];
	uint8_t mmx_1[8][16];
	uint8_t mmx_2[8][16];
	uint8_t pad[96];
}cv_fx_state,*cv_fx_state_p;

typedef struct _cv_seg_reg
{
	u16 selector;
	u16 attrib;
	u32 limit;
	u64 base;
}cv_seg_reg,*cv_seg_reg_p;

typedef struct _cv_sr_state
{
	cv_seg_reg es;
	cv_seg_reg cs;
	cv_seg_reg ss;
	cv_seg_reg ds;
}cv_sr_state,*cv_sr_state_p;

typedef struct _cv_fg_state
{
	cv_seg_reg fs;
	cv_seg_reg gs;
	u64 gsswap;
}cv_fg_state,*cv_fg_state_p;

typedef struct _cv_lt_state
{
	cv_seg_reg tr;
	cv_seg_reg ldtr;
}cv_lt_state,*cv_lt_state_p;

typedef struct _cv_dt_state
{
	cv_seg_reg gdtr;
	cv_seg_reg idtr;
}cv_dt_state,*cv_dt_state_p;

typedef struct _cv_sysenter_state
{
	u64 sysenter_cs;
	u64 sysenter_esp;
	u64 sysenter_eip;
}cv_sysenter_state,*cv_sysenter_state_p;

typedef struct _cv_syscall_state
{
	u64 star;
	u64 lstar;
	u64 cstar;
	u64 sfmask;
}cv_syscall_state,*cv_syscall_state_p;

#define cv_memtype_uc		0
#define cv_memtype_wc		1
#define cv_memtype_wt		4
#define cv_memtype_wp		5
#define cv_memtype_wb		6
#define cv_memtype_ucm		7

typedef struct _cv_addr_map_info
{
	u64 gpa;
	u64 hva;
	u32 page_total;
	union
	{
		struct
		{
			u32 present:1;
			u32 write:1;
			u32 execute:1;
			u32 user:1;
			u32 caching:3;
			u32 page_size:2;
			u32 reserved:23;
		};
		u32 value;
	}attrib;
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
	cv_lbr,
	cv_tsc,
	cv_maximum
}cv_reg_type,*cv_reg_type_p;

typedef enum _cv_intercept_code
{
	cv_invalid_state=0,
	cv_shutdown_condition=1,
	cv_memory_access=2,
	cv_rsm_instruction=3,
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
		u32 vector:5;
		u32 ec_valid:1;
		u32 reserved:26;
	};
	u32 error_code;
	u64 pf_addr;
	u8 fetched_bytes;
	u8 instruction_bytes[15];
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
		u8 present:1;
		u8 write:1;
		u8 execute:1;
		u8 user:1;
		u8 fetched_bytes:4;
	}access;
	u8 instruction_bytes[15];
	u64 gpa;
	u64 gva;
	struct
	{
		u64 operand_size:16;
		u64 reserved:47;
		u64 decoded:1;
	}flags;
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
		u64 cpl:2;
		u64 pe:1;
		u64 lm:1;
		u64 interrupt_shadow:1;
		u64 instruction_length:4;
		u64 int_pending:1;
		u64 pg:1;
		u64 pae:1;
		u64 reserved:52;
	}vp_state;
}cv_exit_context,*cv_exit_context_p;

typedef struct _cv_emu_info_header
{
	u32 length;
	u32 type;
}cv_emu_info_header,*cv_emu_info_header_p;

typedef struct _cv_emu_mmio_info
{
	cv_emu_info_header header;
	union
	{
		struct
		{
			u64 direction:1;
			u64 advancement_length:4;
			u64 reserved:27;
			u64 access_size:32;
		};
		u64 value;
	}emulation_property;
	u64 address;
	u64 data_size;
	u8 data[1];
}cv_emu_mmio_info,*cv_emu_mmio_info_p;

typedef NOIR_STATUS noir_status;
typedef CVM_HANDLE cv_handle;
#endif