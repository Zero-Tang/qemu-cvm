/*
 * QEMU NoirVisor Customizable Virtual Machine accelerator (NoirCV)
 *
 * Copyright Zero Tang. 2022
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "exec/ioport.h"
#include "qemu-common.h"
#include "qemu/accel.h"
#include "sysemu/noircv.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"
#include "qemu/main-loop.h"
#include "hw/boards.h"
#include "hw/i386/ioapic.h"
#include "hw/i386/apic_internal.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qapi/qapi-types-common.h"
#include "qapi/qapi-visit-common.h"
#include "migration/blocker.h"

#include "noircv-accel-ops.h"

typedef struct _cvmap_list
{
	hwaddr start_pa;
	ram_addr_t size;
	void* host_va;
}cvmap_list,*cvmap_list_p;

typedef struct _noircv_vcpu
{
	int cpu_index;
	cv_exit_context exit_context;
	bool ready_for_pic_interrupt;
	bool interruptible;
	bool window_registered;
	bool interrupt_pending;
}noircv_vcpu,*noircv_vcpu_p;

static bool noircv_allowed;
static cv_handle vmhandle;

#define cv_map_list_limit	32
cvmap_list cv_map_info_list[cv_map_list_limit];

int noircv_enabled(void)
{
	return noircv_allowed;
}

static noircv_vcpu_p get_noircv_vcpu(CPUState *cpu)
{
	return (noircv_vcpu_p)cpu->hax_vcpu;
}

static bool ncv_copy_physical(void* buffer,u64 gpa,u64 size,bool read)
{
	u64 cur_gpa=gpa;
	u64 rem_size=size;
	void* cur_buff=buffer;
	bool fully_copied=false;
	for(u32 i=0;i<cv_map_list_limit;i++)
	{
		cvmap_list_p info=&cv_map_info_list[i];
		if(cur_gpa>=info->start_pa && cur_gpa<info->start_pa+info->size)
		{
			u64 region_offset=cur_gpa-info->start_pa;
			u64 region_remainder=info->size-region_offset;
			u64 copy_size=rem_size>region_remainder?region_remainder:rem_size;
			if(read)
				memcpy(cur_buff,(void*)((uintptr_t)info->host_va+region_offset),copy_size);
			else
				memcpy((void*)((uintptr_t)info->host_va+region_offset),cur_buff,copy_size);
			cur_gpa+=copy_size;
			cur_buff=(void*)((uintptr_t)cur_buff+copy_size);
			rem_size-=copy_size;
			if(rem_size==0)
			{
				fully_copied=true;
				break;
			}
		}
	}
	return fully_copied;
}

static void ncv_update_mapping(hwaddr start_pa,ram_addr_t size,void* host_va,bool add,bool rom)
{
	cv_addr_map_info map_info;
	map_info.gpa=start_pa;
	map_info.hva=(u64)host_va;
	map_info.page_total=(u32)(size>>12);
	map_info.attrib.value=0;
	if(add)
	{
		map_info.attrib.present=true;
		map_info.attrib.write=!rom;
		map_info.attrib.execute=true;
		map_info.attrib.user=true;
		map_info.attrib.caching=cv_memtype_wb;
		map_info.attrib.page_size=0;
	}
	printf("NoirVisor CVM: %s mapping %s for GPA=0x%llX with HVA=0x%llX for 0x%X pages!\n",add?"Adding":"Removing",rom?"ROM":"RAM",map_info.gpa,map_info.hva,map_info.page_total);
	noir_status st=ncv_set_mapping(vmhandle,&map_info);
	if(st!=noir_success)
	{
		fprintf(stderr,"Failed to set mapping! GPA=0x%p, HVA=0x%p, Size=%p bytes!\n",(void*)start_pa,host_va,(void*)size);
		fprintf(stderr,"Operation: %s (%s), Status=0x%08X\n",add?"Map":"Unmap",rom?"ROM":"RAM",st);
	}
}

static void ncv_process_section(MemoryRegionSection *section,bool add)
{
	bool list_mapped=false;
	MemoryRegion *mr=section->mr;
	hwaddr start_pa=section->offset_within_address_space;
	ram_addr_t size=int128_get64(section->size);
	u32 delta;
	u64 host_va;
	if(!memory_region_is_ram(mr))return;
	delta=qemu_real_host_page_size-(start_pa & ~qemu_real_host_page_mask);
	delta&=~qemu_real_host_page_mask;
	if(delta>size)return;
	start_pa+=delta;
	size-=delta;
	size&=qemu_real_host_page_mask;
	if(!size || (start_pa & ~qemu_real_host_page_mask))return;
	host_va=(uintptr_t)memory_region_get_ram_ptr(mr)+section->offset_within_region+delta;
	// Add updates...
	for(u32 i=0;i<cv_map_list_limit;i++)
	{
		if(add && cv_map_info_list[i].host_va==NULL)
		{
			cv_map_info_list[i].host_va=(void*)host_va;
			cv_map_info_list[i].start_pa=start_pa;
			cv_map_info_list[i].size=size;
			list_mapped=true;
			break;
		}
		else if((!add) && (cv_map_info_list[i].start_pa==start_pa) && (cv_map_info_list[i].size==size))
		{
			memset(&cv_map_info_list[i],0,sizeof(cvmap_list));
			list_mapped=true;
			break;
		}
	}
	if(!list_mapped)fprintf(stderr,"Failed to process section!\n");
	// Update NPT.
	ncv_update_mapping(start_pa,size,(void*)host_va,add,memory_region_is_rom(mr));
}

static void ncv_region_add(MemoryListener *listener,MemoryRegionSection *section)
{
	memory_region_ref(section->mr);
	ncv_process_section(section,true);
}

static void ncv_region_del(MemoryListener *listener,MemoryRegionSection *section)
{
	ncv_process_section(section,false);
	memory_region_unref(section->mr);
}

static void ncv_transaction_begin(MemoryListener *listener)
{
	;	// NOTHING
}

static void ncv_transaction_commit(MemoryListener *listener)
{
	;	// NOTHING
}

static void ncv_log_sync(MemoryListener *listener,MemoryRegionSection *section)
{
	MemoryRegion *mr=section->mr;
	if(!memory_region_is_ram(mr))return;
	memory_region_set_dirty(mr,0,int128_get64(section->size));
}

static MemoryListener ncv_memory_listener=
{
	.name="noircv",
	.begin=ncv_transaction_begin,
	.commit=ncv_transaction_commit,
	.region_add=ncv_region_add,
	.region_del=ncv_region_del,
	.log_sync=ncv_log_sync,
	.priority=10
};

static void ncv_memory_init(void)
{
	memory_listener_register(&ncv_memory_listener,&address_space_memory);
}

static void ncv_handle_portio(CPUState *cpu,const cv_io_context_p io_ctxt)
{
	noircv_vcpu_p vcpu=get_noircv_vcpu(cpu);
	cv_exit_context_p exit_ctxt=&vcpu->exit_context;
	MemTxAttrs attrs={0};
	if(io_ctxt->access.string)
	{
		u64 size=io_ctxt->access.operand_size;
		u64 mask=0;
		// Calculate the GVA
		u64 gva=io_ctxt->segment.base,gpa;
		if(io_ctxt->access.io_type)
			gva+=io_ctxt->rdi;
		else
			gva+=io_ctxt->rsi;
		// Mask with address width.
		memset(&mask,0xFF,io_ctxt->access.address_width);
		gva&=mask;
		if(io_ctxt->access.repeat)size*=io_ctxt->rcx;
		if(exit_ctxt->vp_state.pg)
		{
			// FIXME: Do permission checks, translations...
			gpa=cpu_get_phys_page_debug(cpu,gva);
			fprintf(stderr,"NoirVisor CVM: String Port I/O, while paging is on, is unsupported!\n");
			fprintf(stderr,"Port=0x%04X. GVA=0x%llX, GPA=0x%llX, Size=%llu\n",io_ctxt->port,gva,gpa,size);
			system("pause");
			qemu_system_guest_panicked(cpu_get_crash_info(cpu));
		}
		else
		{
			// If paging is turned off, then gva is gpa.
			u8 *io_buff=malloc(size);
			if(io_buff)
			{
				bool result;
				// Make sure to split in I/O address space, or otherwise it will be overflowing to other ports.
				if(io_ctxt->access.io_type)
				{
					for(u64 i=0;i<size;i+=io_ctxt->access.operand_size)
						address_space_read(&address_space_io,io_ctxt->port,attrs,&io_buff[i],io_ctxt->access.operand_size);
					result=ncv_copy_physical(io_buff,gva,size,false);
				}
				else
				{
					result=ncv_copy_physical(io_buff,gva,size,true);
					for(u64 i=0;i<size;i+=io_ctxt->access.operand_size)
						address_space_write(&address_space_io,io_ctxt->port,attrs,&io_buff[i],io_ctxt->access.operand_size);
				}
				if(!result)
				{
					fprintf(stderr,"Failed to copy guest physical memory!\n");
					qemu_system_guest_panicked(cpu_get_crash_info(cpu));
					system("pause");
				}
				free(io_buff);
			}
			else
			{
				fprintf(stderr,"Failed to allocate buffer for String I/O!");
				qemu_system_guest_panicked(cpu_get_crash_info(cpu));
			}
		}
		ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_ip,&exit_ctxt->next_rip,sizeof(exit_ctxt->next_rip));
	}
	else
	{
		address_space_rw(&address_space_io,io_ctxt->port,attrs,&io_ctxt->rax,io_ctxt->access.operand_size,!io_ctxt->access.io_type);
		if(io_ctxt->access.io_type)
		{
			cv_gpr_state gpr;
			ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_gpr,&gpr,sizeof(gpr));
			gpr.rax=io_ctxt->rax;
			ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_gpr,&gpr,sizeof(gpr));
		}
		ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_ip,&exit_ctxt->next_rip,sizeof(exit_ctxt->next_rip));
	}
}

static void ncv_handle_mmio(CPUState *cpu,const cv_memory_access_context_p memory_access)
{
	noircv_vcpu_p vcpu=get_noircv_vcpu(cpu);
	cv_exit_context_p exit_ctxt=&vcpu->exit_context;
	if(unlikely(memory_access->access.execute))
	{
		fprintf(stderr,"NoirVisor CVM: Incorrect mapping for execution is detected! Execution GPA=0x%llX\n",memory_access->gpa);
		qemu_system_guest_panicked(cpu_get_crash_info(cpu));
		system("pause");
	}
	else
	{
		if(likely(memory_access->flags.decoded))
		{
			cv_emu_mmio_info_p emu_mmio=alloca(sizeof(emu_mmio)+memory_access->flags.operand_size);
			// If the MMIO operation is a read instruction, pre-fill the buffer.
			if(!memory_access->access.write)
				cpu_physical_memory_read(memory_access->gpa,emu_mmio->data,memory_access->flags.operand_size);
			printf("Handing MMIO (%s): GPA=0x%llX, rip=0x%llX\n",memory_access->access.write?"Write":"Read",memory_access->gpa,exit_ctxt->rip);
			noir_status st=ncv_try_cvexit_emulation(vmhandle,cpu->cpu_index,&emu_mmio->header);
			switch(st)
			{
				case noir_success:
				{
					if(memory_access->access.write)
						cpu_physical_memory_write(memory_access->gpa,emu_mmio->data,memory_access->flags.operand_size);
					break;
				}
				case noir_emu_dual_memory_operands:
				{
					// This is not a emulation failure.
					// It requires more processing regarding guest virtual address because there are two memory operands.
					fprintf(stderr,"MMIO emulation failed because the MMIO instruction has two memory operands! GPA=0x%llX\n",memory_access->gpa);
					qemu_system_guest_panicked(cpu_get_crash_info(cpu));
					system("pause");
					break;
				}
				case noir_emu_unknown_instruction:
				{
					fprintf(stderr,"MMIO emulation failed due to unknown instruction! See Kernel Debugger Output. GPA=0x%llX\n",memory_access->gpa);
					qemu_system_guest_panicked(cpu_get_crash_info(cpu));
					system("pause");
					break;
				}
				default:
				{
					fprintf(stderr,"Unknown status (0x%X) of MMIO Emulation! GPA=0x%llX\n",st,memory_access->gpa);
					qemu_system_guest_panicked(cpu_get_crash_info(cpu));
					system("pause");
					break;
				}
			}
		}
		else
		{
			fprintf(stderr,"NoirVisor CVM: Failed to decode instruction for MMIO! GPA=0x%llX\n",memory_access->gpa);
			qemu_system_guest_panicked(cpu_get_crash_info(cpu));
			system("pause");
		}
	}
}

static void ncv_handle_halt(CPUState *cpu)
{
	noircv_vcpu_p vcpu=get_noircv_vcpu(cpu);
	cv_exit_context_p exit_ctxt=&vcpu->exit_context;
	CPUX86State *env=cpu->env_ptr;
	printf("[NoirVisor CVM] vCPU %d is halting...\n",cpu->cpu_index);
	qemu_mutex_lock_iothread();
	if(!((cpu->interrupt_request & CPU_INTERRUPT_HARD) && (env->eflags & IF_MASK)) && !(cpu->interrupt_request & CPU_INTERRUPT_NMI))
	{
		cpu->exception_index=EXCP_HLT;
		cpu->halted=true;
	}
	qemu_mutex_unlock_iothread();
	ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_ip,&exit_ctxt->next_rip,sizeof(exit_ctxt->next_rip));
}

static cv_seg_reg ncv_seg_q2v(const SegmentCache *qs)
{
	cv_seg_reg seg;
	seg.selector=qs->selector;
	seg.attrib=(u16)(qs->flags>>DESC_TYPE_SHIFT);
	seg.limit=qs->limit;
	seg.base=qs->base;
	return seg;
}

static SegmentCache ncv_seg_v2q(const cv_seg_reg *seg)
{
	SegmentCache qs;
	qs.selector=seg->selector;
	qs.flags=(u32)seg->attrib<<DESC_TYPE_SHIFT;
	qs.limit=seg->limit;
	qs.base=seg->base;
	return qs;
}

static void ncv_get_registers(CPUState *cpu)
{
	noir_status st;
	CPUX86State *env=cpu->env_ptr;
	cv_cr_state cr;
	cv_sr_state sr;
	cv_fg_state fg;
	cv_lt_state lt;
	cv_dt_state dt;
	cv_fx_state fx;
	cv_sysenter_state se;
	cv_syscall_state sc;
	assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));
	// Get Time Stamp Counter...
	if(!env->tsc_valid)
	{
		st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_tsc,&env->tsc,sizeof(env->tsc));
		if(st!=noir_success)fprintf(stderr,"Failed to view Time Stamp Counter! Status=0x%X\n",st);
	}
	// Get General-Purpose Registers...
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_gpr,env->regs,sizeof(env->regs));
	if(st!=noir_success)fprintf(stderr,"Failed to view GPR! Status=0x%X\n",st);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_flags,&env->eflags,sizeof(env->eflags));
	if(st!=noir_success)fprintf(stderr,"Failed to view rflags! Status=0x%X\n",st);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_ip,&env->eip,sizeof(env->eflags));
	if(st!=noir_success)fprintf(stderr,"Failed to view rip! Status=0x%X\n",st);
	// Get Control Registers...
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_cr,&cr,sizeof(cr));
	if(st!=noir_success)fprintf(stderr,"Failed to view Control Registers! Status=0x%X\n",st);
	env->cr[0]=cr.cr0;
	env->cr[3]=cr.cr3;
	env->cr[4]=cr.cr4;
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_cr2,&env->cr[2],sizeof(env->cr[2]));
	if(st!=noir_success)fprintf(stderr,"Failed to view CR2 Register! Status=0x%X\n",st);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_xcr0,&env->xcr0,sizeof(env->xcr0));
	if(st!=noir_success)fprintf(stderr,"Failed to view XCR0 Register! Status=0x%X\n",st);
	// Get Debug Registers...
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_dr,&env->dr[0],sizeof(cv_dr_state));
	if(st!=noir_success)fprintf(stderr,"Failed to view Debug Registers! Status=0x%X\n",st);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_dr67,&env->dr[6],sizeof(cv_dr67_state));
	if(st!=noir_success)fprintf(stderr,"Failed to view DR6/DR7 Registers! Status=0x%X\n",st);
	// Get Segment Registers...
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_sr,&sr,sizeof(sr));
	if(st!=noir_success)fprintf(stderr,"Failed to view Segment Registers! Status=0x%X\n",st);
	env->segs[R_CS]=ncv_seg_v2q(&sr.cs);
	env->segs[R_DS]=ncv_seg_v2q(&sr.ds);
	env->segs[R_ES]=ncv_seg_v2q(&sr.es);
	env->segs[R_SS]=ncv_seg_v2q(&sr.ss);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_fg,&fg,sizeof(fg));
	if(st!=noir_success)fprintf(stderr,"Failed to view fs/gs segments! Status=0x%X\n",st);
	env->segs[R_FS]=ncv_seg_v2q(&fg.fs);
	env->segs[R_GS]=ncv_seg_v2q(&fg.gs);
	env->kernelgsbase=fg.gsswap;
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_lt,&lt,sizeof(lt));
	if(st!=noir_success)fprintf(stderr,"Failed to view tr/ldtr descriptors! Status=0x%X\n",st);
	env->tr=ncv_seg_v2q(&lt.tr);
	env->ldt=ncv_seg_v2q(&lt.ldtr);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_dt,&dt,sizeof(dt));
	if(st!=noir_success)fprintf(stderr,"Failed to view gdt/idt descriptors! Status=0x%X\n",st);
	env->gdt=ncv_seg_v2q(&dt.gdtr);
	env->idt=ncv_seg_v2q(&dt.idtr);
	// Get FX State...
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_fx,&fx,sizeof(fx));
	if(st!=noir_success)fprintf(stderr,"Failed to view FPU state! Status=0x%X\n",st);
	env->fpstt = (fx.fsw >> 11) & 7;
	env->fpus = fx.fsw;
	env->fpuc = fx.fcw;
	for(int i=0;i<8;i++)
		env->fptags[i]=!((fx.ftw>>i)&1);
	memcpy(env->fpregs,fx.st_mm,sizeof(env->fpregs));
	for(int i=0;i<8;i++)
	{
		env->xmm_regs[i].ZMM_Q(0)=ldq_p(&fx.mmx_1[i][0]);
		env->xmm_regs[i].ZMM_Q(1)=ldq_p(&fx.mmx_1[i][8]);
		if(CPU_NB_REGS>8)
		{
			env->xmm_regs[i+8].ZMM_Q(0)=ldq_p(&fx.mmx_2[i][0]);
			env->xmm_regs[i+8].ZMM_Q(1)=ldq_p(&fx.mmx_2[i][8]);
		}
	}
	env->mxcsr=fx.mxcsr;
	// Get MSRs...
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_efer,&env->efer,sizeof(env->efer));
	if(st!=noir_success)fprintf(stderr,"Failed to view EFER register! Status=0x%X\n",st);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_pat,&env->pat,sizeof(env->pat));
	if(st!=noir_success)fprintf(stderr,"Failed to view PAT register! Status=0x%X\n",st);
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_sysenter_msr,&se,sizeof(se));
	if(st!=noir_success)fprintf(stderr,"Failed to view SysEnter MSRs! Status=0x%X\n",st);
	env->sysenter_cs=se.sysenter_cs;
	env->sysenter_esp=se.sysenter_esp;
	env->sysenter_eip=se.sysenter_eip;
	st=ncv_view_vcpu_register(vmhandle,cpu->cpu_index,cv_syscall_msr,&sc,sizeof(sc));
	if(st!=noir_success)fprintf(stderr,"Failed to view SysCall MSRs! Status=0x%X\n",st);
	env->star=sc.star;
	env->lstar=sc.lstar;
	env->cstar=sc.cstar;
	env->fmask=sc.sfmask;
}

static void ncv_set_registers(CPUState *cpu,int level)
{
	noir_status st;
	CPUX86State *env=cpu->env_ptr;
	// X86CPU *x86_cpu=X86_CPU(cpu);
	assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));
	cv_cr_state cr;
	cv_sr_state sr;
	cv_fg_state fg;
	cv_lt_state lt;
	cv_dt_state dt;
	cv_fx_state fx;
	cv_sysenter_state se;
	cv_syscall_state sc;
	// Set General-Purpose Registers...
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_gpr,env->regs,sizeof(env->regs));
	if(st!=noir_success)fprintf(stderr,"Failed to edit GPR! Status=0x%X\n",st);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_flags,&env->eflags,sizeof(env->eflags));
	if(st!=noir_success)fprintf(stderr,"Failed to edit rflags! Status=0x%X\n",st);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_ip,&env->eip,sizeof(env->eip));
	if(st!=noir_success)fprintf(stderr,"Failed to edit rip! Status=0x%X\n",st);
	// Set Control Registers...
	cr.cr0=env->cr[0];
	cr.cr3=env->cr[3];
	cr.cr4=env->cr[4];
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_cr,&cr,sizeof(cr));
	if(st!=noir_success)fprintf(stderr,"Failed to edit Control Registers! Status=0x%X\n",st);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_cr2,&env->cr[2],sizeof(env->cr[2]));
	if(st!=noir_success)fprintf(stderr,"Failed to edit CR2 register! Status=0x%X\n",st);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_xcr0,&env->xcr0,sizeof(env->xcr0));
	if(st!=noir_success)fprintf(stderr,"Failed to edit XCR0 register! Status=0x%X\n",st);
	// Set Debug Registers...
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_dr,&env->dr[0],sizeof(cv_dr_state));
	if(st!=noir_success)fprintf(stderr,"Failed to edit Debug Registers! Status=0x%X\n",st);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_dr67,&env->dr[6],sizeof(cv_dr67_state));
	if(st!=noir_success)fprintf(stderr,"Failed to edit DR6/DR7! Status=0x%X\n",st);
	// Set Segment Registers...
	sr.cs=ncv_seg_q2v(&env->segs[R_CS]);
	sr.ds=ncv_seg_q2v(&env->segs[R_DS]);
	sr.es=ncv_seg_q2v(&env->segs[R_ES]);
	sr.ss=ncv_seg_q2v(&env->segs[R_SS]);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_sr,&sr,sizeof(sr));
	if(st!=noir_success)fprintf(stderr,"Failed to edit Segment Registers! Status=0x%X\n",st);
	fg.fs=ncv_seg_q2v(&env->segs[R_FS]);
	fg.gs=ncv_seg_q2v(&env->segs[R_GS]);
	fg.gsswap=env->kernelgsbase;
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_fg,&fg,sizeof(fg));
	if(st!=noir_success)fprintf(stderr,"Failed to edit Segment Registers! Status=0x%X\n",st);
	lt.tr=ncv_seg_q2v(&env->tr);
	lt.ldtr=ncv_seg_q2v(&env->ldt);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_lt,&lt,sizeof(lt));
	if(st!=noir_success)fprintf(stderr,"Failed to edit TR/LDTR registers! Status=0x%X\n",st);
	dt.gdtr=ncv_seg_q2v(&env->gdt);
	dt.idtr=ncv_seg_q2v(&env->idt);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_dt,&dt,sizeof(dt));
	if(st!=noir_success)fprintf(stderr,"Failed to edit GDT/IDT registers! Status=0x%X\n",st);
	// Set FX State...
	memset(&fx,0,sizeof(fx));
	fx.fsw=env->fpus&~(7<<11);
	fx.fsw|=(env->fpstt&7)<<11;
	fx.fcw=env->fpuc;
	for(int i=0;i<8;i++)fx.ftw|=(!env->fptags[i])<<i;
	memcpy(fx.st_mm,env->fpregs,sizeof(env->fpregs));
	for(int i=0;i<8;i++)
	{
		stq_p(&fx.mmx_1[i][0],env->xmm_regs[i].ZMM_Q(0));
		stq_p(&fx.mmx_1[i][8],env->xmm_regs[i].ZMM_Q(1));
		if(CPU_NB_REGS>8)
		{
			stq_p(&fx.mmx_2[i][0],env->xmm_regs[i + 8].ZMM_Q(0));
			stq_p(&fx.mmx_2[i][8],env->xmm_regs[i + 8].ZMM_Q(1));
		}
	}
	fx.mxcsr=env->mxcsr;
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_fx,&fx,sizeof(fx));
	// Set MSR State...
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_efer,&env->efer,sizeof(env->efer));
	if(st!=noir_success)fprintf(stderr,"Failed to edit EFER register! Status=0x%X\n",st);
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_pat,&env->pat,sizeof(env->pat));
	if(st!=noir_success)fprintf(stderr,"Failed to edit PAT register! Status=0x%X\n",st);
	se.sysenter_cs=env->sysenter_cs;
	se.sysenter_esp=env->sysenter_esp;
	se.sysenter_eip=env->sysenter_eip;
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_sysenter_msr,&se,sizeof(se));
	if(st!=noir_success)fprintf(stderr,"Failed to edit SysEnter MSRs! Status=0x%X\n",st);
	sc.star=env->star;
	sc.lstar=env->lstar;
	sc.cstar=env->cstar;
	sc.sfmask=env->fmask;
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_syscall_msr,&sc,sizeof(sc));
	if(st!=noir_success)fprintf(stderr,"Failed to edit SysCall MSRs! Status=0x%X\n",st);
	if(level)
	{
		st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_tsc,&env->tsc,sizeof(env->tsc));
		if(st!=noir_success)fprintf(stderr,"Failed to edit Time Stamp Counter! Status=0x%X\n",st);
	}
}

static void ncv_vcpu_process_async_events(CPUState *cpu)
{
	CPUX86State *env=cpu->env_ptr;
	X86CPU *x86_cpu=X86_CPU(cpu);
	noircv_vcpu_p vcpu=get_noircv_vcpu(cpu);
	if((cpu->interrupt_request & CPU_INTERRUPT_INIT) && !(env->hflags & HF_SMM_MASK))
	{
		ncv_cpu_synchronize_state(cpu);
		do_cpu_init(x86_cpu);
		vcpu->interruptible=true;
	}
	if(((cpu->interrupt_request & CPU_INTERRUPT_HARD) && (env->eflags & IF_MASK)) || (cpu->interrupt_request & CPU_INTERRUPT_NMI))
		cpu->halted=false;
	if(cpu->interrupt_request & CPU_INTERRUPT_SIPI)
	{
		ncv_cpu_synchronize_state(cpu);
		do_cpu_sipi(x86_cpu);
	}
}

static void ncv_vcpu_pre_run(CPUState *cpu)
{
	noircv_vcpu_p vcpu=get_noircv_vcpu(cpu);
	CPUX86State *env=cpu->env_ptr;
	noir_status st=noir_success;
	qemu_mutex_lock_iothread();
	// Inject NMI
	if(!vcpu->interrupt_pending)
	{
		if(cpu->interrupt_request & CPU_INTERRUPT_NMI)
		{
			cpu->interrupt_request&=~CPU_INTERRUPT_NMI;
			vcpu->interruptible=false;
			st=ncv_inject_event(vmhandle,cpu->cpu_index,true,EXCP02_NMI,ncv_event_nmi,0,false,0);
			fprintf(stderr,"[NoirVisor CVM] Injecting NMI... Status=0x%X\n",st);
		}
		if(cpu->interrupt_request & CPU_INTERRUPT_SMI)
		{
			// NoirVisor can't take SMIs right now.
			cpu->interrupt_request&=~CPU_INTERRUPT_SMI;
			fprintf(stderr,"[NoirVisor CVM] NoirVisor does not support SMI!\n");
		}
		// Force the vCPU out of its inner loop, in order to process INIT or TPR accesses.
		if((cpu->interrupt_request & CPU_INTERRUPT_INIT) && !(env->hflags & HF_SMM_MASK))
			cpu->exit_request=1;
		if(cpu->interrupt_request & CPU_INTERRUPT_TPR)cpu->exit_request=1;
	}
	if(vcpu->ready_for_pic_interrupt && (cpu->interrupt_request & CPU_INTERRUPT_HARD))
	{
		cpu->interrupt_request&=~CPU_INTERRUPT_HARD;
		int irq=cpu_get_pic_interrupt(env);
		if(irq>=0)st=ncv_inject_event(vmhandle,cpu->cpu_index,true,irq,ncv_event_extint,0,false,0);
			fprintf(stderr,"[NoirVisor CVM] Injecting External Interrupt... Status=0x%X\n",st);
	}
	qemu_mutex_unlock_iothread();
	vcpu->ready_for_pic_interrupt=false;
}

static void ncv_vcpu_post_run(CPUState *cpu)
{
	CPUX86State *env=cpu->env_ptr;
	noircv_vcpu_p vcpu=(noircv_vcpu_p)cpu->hax_vcpu;
	cv_exit_context_p exit_ctxt=&vcpu->exit_context;
	env->eflags=exit_ctxt->rflags;
	vcpu->interrupt_pending=exit_ctxt->vp_state.int_pending;
	vcpu->interruptible=exit_ctxt->vp_state.interrupt_shadow;
}

static int ncv_vcpu_run(CPUState *cpu)
{
	noircv_vcpu_p vcpu=get_noircv_vcpu(cpu);
	noir_status st;
	int ret=0;
	ncv_vcpu_process_async_events(cpu);
	if(cpu->halted)
	{
		cpu->exception_index=EXCP_HLT;
		qatomic_set(&cpu->exit_request,false);
		return 0;
	}
	qemu_mutex_unlock_iothread();
	cpu_exec_start(cpu);
	do
	{
		cv_exit_context_p exit_ctxt=&vcpu->exit_context;
		if(cpu->vcpu_dirty)
		{
			ncv_set_registers(cpu,NOIRCV_SET_RUNTIME_STATE);
			cpu->vcpu_dirty=false;
		}
		ncv_vcpu_pre_run(cpu);
		if(qatomic_read(&cpu->exit_request))ncv_kick_vcpu(cpu);
		st=ncv_run_vcpu(vmhandle,cpu->cpu_index,exit_ctxt);
		if(st!=noir_success)
		{
			fprintf(stderr,"NoirVisor CVM: Failed to run vCPU! Status=0x%X\n",st);
			ret=-1;
			break;
		}
		ncv_vcpu_post_run(cpu);
		switch(exit_ctxt->intercept_code)
		{
			case cv_invalid_state:
			{
				fprintf(stderr,"NoirVisor CVM: Failed to launch vCPU due to invalid state!\n");
				ret=-1;
				break;
			}
			case cv_shutdown_condition:
			{
				fprintf(stderr,"NoirVisor CVM: vCPU encounters a shutdown condition!\n");
				break;
			}
			case cv_memory_access:
			{
				ncv_handle_mmio(cpu,&exit_ctxt->memory_access);
				ret=1;
				break;
			}
			case cv_hlt_instruction:
			{
				ncv_handle_halt(cpu);
				ret=1;
				break;
			}
			case cv_io_instruction:
			{
				ncv_handle_portio(cpu,&exit_ctxt->io);
				ret=1;
				break;
			}
			case cv_rescission:
			{
				cpu->exception_index=EXCP_INTERRUPT;
				ret=1;
				break;
			}
			default:
			{
				fprintf(stderr,"NoirVisor CVM: Unexpected intercept code: 0x%X!\n",exit_ctxt->intercept_code);
				ncv_get_registers(cpu);
				qemu_mutex_lock_iothread();
				qemu_system_guest_panicked(cpu_get_crash_info(cpu));
				qemu_mutex_unlock_iothread();
				break;
			}
		}
	}while(!ret);
	cpu_exec_end(cpu);
	qemu_mutex_lock_iothread();
	current_cpu=cpu;
	qatomic_set(&cpu->exit_request,false);
	return ret<0;
}

static void do_noircv_cpu_synchronize_state(CPUState *cpu,run_on_cpu_data arg)
{
	if(!cpu->vcpu_dirty)
	{
		ncv_get_registers(cpu);
		cpu->vcpu_dirty=true;
	}
}

static void do_noircv_cpu_synchronize_post_reset(CPUState *cpu,run_on_cpu_data arg)
{
	ncv_set_registers(cpu,NOIRCV_SET_RESET_STATE);
	cpu->vcpu_dirty=false;
}

static void do_noircv_cpu_synchronize_post_init(CPUState *cpu,run_on_cpu_data arg)
{
	ncv_set_registers(cpu,NOIRCV_SET_FULL_STATE);
	cpu->vcpu_dirty=false;
}

static void do_noircv_cpu_synchronize_pre_loadvm(CPUState *cpu,run_on_cpu_data arg)
{
	cpu->vcpu_dirty=true;
}

void ncv_cpu_synchronize_state(CPUState *cpu)
{
	if(!cpu->vcpu_dirty)run_on_cpu(cpu,do_noircv_cpu_synchronize_state,RUN_ON_CPU_NULL);
}

void ncv_cpu_synchronize_post_reset(CPUState *cpu)
{
	run_on_cpu(cpu,do_noircv_cpu_synchronize_post_reset,RUN_ON_CPU_NULL);
}

void ncv_cpu_synchronize_post_init(CPUState *cpu)
{
	run_on_cpu(cpu,do_noircv_cpu_synchronize_post_init,RUN_ON_CPU_NULL);
}

void ncv_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
	run_on_cpu(cpu,do_noircv_cpu_synchronize_pre_loadvm,RUN_ON_CPU_NULL);
}

int ncv_exec_vcpu(CPUState *cpu)
{
	int ret=0,fatal=0;
	while(1)
	{
		if(cpu->exception_index>=EXCP_INTERRUPT)
		{
			ret=cpu->exception_index;
			cpu->exception_index=-1;
			break;
		}
		fatal=ncv_vcpu_run(cpu);
		if(fatal)
		{
			fprintf(stderr,"NoirVisor CVM: Failed to run vCPU %d!\n",cpu->cpu_index);
			abort();
		}
	}
	return ret;
}

static void ncv_cpu_update_state(void* opaque,bool running,RunState state)
{
	CPUX86State *env=opaque;
	if(running)env->tsc_valid=false;
}

void ncv_init_vcpu(CPUState *cpu)
{
	noircv_vcpu_p vcpu=g_new0(noircv_vcpu,1);
	if(vcpu)
	{
		noir_status st=ncv_create_vcpu(vmhandle,cpu->cpu_index);
		if(st!=noir_success)
		{
			fprintf(stderr,"Failed to create vCPU %d! Status: 0x%X\n",cpu->cpu_index,st);
			g_free(vcpu);
			return;
		}
		cpu->vcpu_dirty=true;
		cpu->hax_vcpu=(struct hax_vcpu_state*)vcpu;
		qemu_add_vm_change_state_handler(ncv_cpu_update_state,cpu->env_ptr);
	}
}

void ncv_destroy_vcpu(CPUState *cpu)
{
	ncv_delete_vcpu(vmhandle,cpu->cpu_index);
}

void ncv_kick_vcpu(CPUState *cpu)
{
	ncv_rescind_vcpu(vmhandle,cpu->cpu_index);
}

static int ncv_init(ram_addr_t ram_size,int max_cpus)
{
	int ret;
#ifdef CONFIG_WIN32
	ret=ncv_win_init();
#endif
	if(ret==0)
		fprintf(stderr,"NoirVisor is absent in the system!\n");
	else
	{
		noir_status st=ncv_create_vm(&vmhandle);
		if(st==noir_success)
		{
			ncv_memory_init();
			printf("NoirVisor CVM accelerator is operational!\n");
		}
		else
		{
			if(st==noir_hypervision_absent)
				fprintf(stderr,"NoirVisor did not subvert the system!\n");
			else
				fprintf(stderr,"Failed to create VM! Status: 0x%X\n",st);
			ret=0;
		}
	}
	return ret;
}

static int ncv_accel_init(MachineState *ms)
{
	return ncv_init(ms->ram_size,(int)ms->smp.max_cpus);
}

static void ncv_accel_class_init(ObjectClass *oc,void *data)
{
	AccelClass *ac=ACCEL_CLASS(oc);
	ac->name="NOIRCV";
	ac->init_machine=ncv_accel_init;
	ac->allowed=&noircv_allowed;
}

static const TypeInfo ncv_accel_type=
{
	.name=ACCEL_CLASS_NAME("noircv"),
	.parent=TYPE_ACCEL,
	.class_init=ncv_accel_class_init
};

static void ncv_type_init(void)
{
	type_register_static(&ncv_accel_type);
}

type_init(ncv_type_init);