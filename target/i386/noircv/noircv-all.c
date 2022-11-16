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

static bool noircv_allowed;
static cv_handle vmhandle;

#define cv_map_list_limit	32
cvmap_list cv_map_info_list[cv_map_list_limit];

int noircv_enabled(void)
{
	return noircv_allowed;
}

static void ncv_update_mapping(hwaddr start_pa,ram_addr_t size,void* host_va,bool add,bool rom)
{
	cv_addr_map_info map_info;
	map_info.GPA=start_pa;
	map_info.HVA=(u64)host_va;
	map_info.NumberOfPages=(u32)(size>>12);
	if(add)
	{
		map_info.Attributes.Present=true;
		map_info.Attributes.Write=!rom;
		map_info.Attributes.Execute=true;
		map_info.Attributes.User=true;
		map_info.Attributes.Caching=6;
		map_info.Attributes.PageSize=0;
		map_info.Attributes.Reserved=0;
	}
	else
		map_info.Attributes.Value=0;
	NOIR_STATUS st=ncv_set_mapping(vmhandle,&map_info);
	if(st!=noir_success)
	{
		fprintf(stderr,"Failed to set mapping! GPA=0x%p, HVA=0x%p, Size=%p bytes!\n",(void*)start_pa,host_va,(void*)size);
		fprintf(stderr,"Operation: %s (%s), Status=0x%08X\n",add?"Map":"Unmap",rom?"ROM":"RAM",st);
	}
}

static void ncv_process_section(MemoryRegionSection *section,bool add)
{
	MemoryRegion *mr=section->mr;
	hwaddr start_pa=section->offset_within_address_space;
	ram_addr_t size=int128_get64(section->size);
	u32 delta;
	u64 host_va;
	if(!memory_region_is_ram(mr))return;
	delta=qemu_real_host_page_size-(start_pa & ~qemu_real_host_page_mask);
	delta&=qemu_real_host_page_mask;
	if(delta>size)return;
	start_pa+=delta;
	size-=delta;
	size&=qemu_real_host_page_mask;
	if(!size || (start_pa & ~qemu_real_host_page_mask))return;
	host_va=(uintptr_t)memory_region_get_ram_ptr(mr)+section->offset_within_region+delta;
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
	.region_add=ncv_region_del,
	.log_sync=ncv_log_sync
};

static void ncv_memory_init(void)
{
	memory_listener_register(&ncv_memory_listener,&address_space_memory);
}

/*
static void ncv_set_registers(CPUState *cpu)
{
	noir_status st;
	CPUX86State *env=cpu->env_ptr;
	// X86CPU *x86_cpu=X86_CPU(cpu);
	assert(cpu_is_stopped(cpu) || qemu_cpu_is_self(cpu));
	cv_gpr_state gpr;
	// cv_cr_state cr;
	// cv_sr_state sr;
	// cv_fx_state fx;
	gpr.rax=env->regs[R_EAX];
	gpr.rcx=env->regs[R_ECX];
	gpr.rdx=env->regs[R_EDX];
	gpr.rbx=env->regs[R_EBX];
	gpr.rsp=env->regs[R_ESP];
	gpr.rbp=env->regs[R_EBP];
	gpr.rsi=env->regs[R_ESI];
	gpr.rdi=env->regs[R_EDI];
	gpr.r8=env->regs[R_R8];
	gpr.r9=env->regs[R_R9];
	gpr.r10=env->regs[R_R10];
	gpr.r11=env->regs[R_R11];
	gpr.r12=env->regs[R_R12];
	gpr.r13=env->regs[R_R13];
	gpr.r14=env->regs[R_R14];
	gpr.r15=env->regs[R_R15];
	st=ncv_edit_vcpu_register(vmhandle,cpu->cpu_index,cv_gpr,&gpr,sizeof(gpr));
	if(st!=noir_success)fprintf(stderr,"Failed to edit GPR! Status=0x%X\n",st);
}
*/

int ncv_exec_vcpu(CPUState *cpu)
{
	return 0;
}

void ncv_init_vcpu(CPUState *cpu)
{
	noir_status st=ncv_create_vcpu(vmhandle,cpu->cpu_index);
	if(st!=noir_success)
	{
		fprintf(stderr,"Failed to create vCPU %d! Status: 0x%X\n",cpu->cpu_index,st);
		return;
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
			for(int i=0;i<max_cpus;i++)
			{
				st=ncv_create_vcpu(vmhandle,i);
				if(st!=noir_success)
				{
					fprintf(stderr,"Failed to create vCPU %d! Status: 0x%X\n",i,st);
					ret=0;
					break;
				}
			}
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