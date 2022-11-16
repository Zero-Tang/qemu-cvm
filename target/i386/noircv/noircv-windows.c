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

#include "qemu/osdep.h"
#include "cpu.h"
#include "noircv-accel-ops.h"
#include "noircv-windows.h"

static HANDLE nvdrv_handle;

int ncv_win_init(void)
{
	nvdrv_handle=CreateFileW(NV_DEVICE_NAME,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	return nvdrv_handle!=INVALID_HANDLE_VALUE;
}

static BOOL NoirControlDriver(IN ULONG IoControlCode,IN PVOID InputBuffer,IN ULONG InputSize,OUT PVOID OutputBuffer,IN ULONG OutputSize,OUT PULONG ReturnLength OPTIONAL)
{
	ULONG BytesReturned=0;
	BOOL bRet=DeviceIoControl(nvdrv_handle,IoControlCode,InputBuffer,InputSize,OutputBuffer,OutputSize,&BytesReturned,NULL);
	if(ReturnLength)*ReturnLength=BytesReturned;
	return bRet;
}

noir_status ncv_set_mapping(cv_handle vm,cv_addr_map_info *mapping_info)
{
	NOIR_STATUS st=noir_unsuccessful;
	ULONG64 InBuff[4];
	RtlCopyMemory(InBuff,mapping_info,sizeof(cv_addr_map_info));
	InBuff[3]=(ULONG64)vm;
	NoirControlDriver(IOCTL_CvmSetMapping,InBuff,sizeof(InBuff),&st,sizeof(st),NULL);
	return st;
}

noir_status ncv_create_vm(cv_handle *vm)
{
	ULONG_PTR OutBuff[2];
	BOOL bRet=NoirControlDriver(IOCTL_CvmCreateVm,NULL,0,OutBuff,sizeof(OutBuff),NULL);
	if(bRet)
	{
		NOIR_STATUS st=(NOIR_STATUS)OutBuff[0];
		if(st==noir_success)*vm=(CVM_HANDLE)OutBuff[1];
		return st;
	}
	return noir_unsuccessful;
}

noir_status ncv_delete_vm(cv_handle vm)
{
	NOIR_STATUS st;
	NoirControlDriver(IOCTL_CvmDeleteVm,&vm,sizeof(vm),&st,sizeof(st),NULL);
	return st;
}

noir_status ncv_inject_event(cv_handle vm,u32 vpid,bool valid,u8 vector,u8 type,u8 priority,bool error_code_valid,u32 err_code)
{
	NOIR_STATUS st=noir_unsuccessful;
	NOIR_CVM_EVENT_INJECTION Event={0};
	ULONG64 InBuff[3];
	Event.Vector=vector;
	Event.Type=type;
	Event.ErrorCodeValid=error_code_valid;
	if(type==0)Event.Priority=priority;
	Event.Reserved=0;
	Event.Valid=valid;
	Event.ErrorCode=err_code;
	InBuff[0]=vm;
	InBuff[1]=(ULONG64)vpid;
	InBuff[2]=Event.Value;
	NoirControlDriver(IOCTL_CvmInjectEvent,InBuff,sizeof(InBuff),&st,sizeof(st),NULL);
	return st;
}

noir_status ncv_run_vcpu(cv_handle vm,u32 vpid,cv_exit_context *exit_context)
{
	NOIR_STATUS st=noir_unsuccessful;
	PVOID OutBuff=alloca(sizeof(cv_exit_context)+8);
	if(OutBuff)
	{
		ULONG_PTR InBuff[2];
		InBuff[0]=vm;
		InBuff[1]=(ULONG_PTR)vpid;
		do
		{
			NoirControlDriver(IOCTL_CvmRunVcpu,InBuff,sizeof(InBuff),OutBuff,sizeof(cv_exit_context)+8,NULL);
			RtlCopyMemory(exit_context,(PVOID)((ULONG_PTR)OutBuff+8),sizeof(cv_exit_context));
			// Re-run the vCPU if the scheduler issued an exit.
		}while(exit_context->intercept_code==cv_scheduler_exit);
		st=*(PULONG32)OutBuff;
	}
	return st;
}

noir_status ncv_rescind_vcpu(cv_handle vm,u32 vpid)
{
	NOIR_STATUS st=noir_unsuccessful;
	ULONG64 InBuff[2]={vm,(ULONG64)vpid};
	NoirControlDriver(IOCTL_CvmRescindVcpu,InBuff,sizeof(InBuff),&st,sizeof(st),NULL);
	return st;
}

noir_status ncv_edit_vcpu_register(cv_handle vm,u32 vpid,cv_reg_type reg_type,void* buffer,u32 buff_size)
{
	NOIR_STATUS st=noir_insufficient_resources;
	// Allocate memory from stack to avoid inter-thread serializations by virtue of heap operations.
	PVOID InBuff=alloca(buff_size+sizeof(NOIR_VIEW_EDIT_REGISTER_CONTEXT));
	if(InBuff)
	{
		PNOIR_VIEW_EDIT_REGISTER_CONTEXT Context=(PNOIR_VIEW_EDIT_REGISTER_CONTEXT)InBuff;
		Context->VirtualMachine=vm;
		Context->VpIndex=vpid;
		Context->RegisterType=reg_type;
		RtlCopyMemory(&Context->DummyBuffer,buffer,buff_size);
		NoirControlDriver(IOCTL_CvmEditVcpuReg,InBuff,buff_size+sizeof(NOIR_VIEW_EDIT_REGISTER_CONTEXT),&st,sizeof(st),NULL);
	}
	return st;
}

noir_status ncv_view_vcpu_register(cv_handle vm,u32 vpid,cv_reg_type reg_type,void* buffer,u32 buff_size)
{
	NOIR_STATUS st=noir_insufficient_resources;
	// Allocate memory from stack to avoid inter-thread serializations by virtue of heap operations.
	PVOID OutBuff=alloca(buff_size+sizeof(NOIR_VIEW_EDIT_REGISTER_CONTEXT));
	if(OutBuff)
	{
		NOIR_VIEW_EDIT_REGISTER_CONTEXT Context;
		Context.VirtualMachine=vm;
		Context.VpIndex=vpid;
		Context.RegisterType=reg_type;
		NoirControlDriver(IOCTL_CvmViewVcpuReg,&Context,sizeof(NOIR_VIEW_EDIT_REGISTER_CONTEXT),OutBuff,buff_size+8,NULL);
		st=*(PULONG32)OutBuff;
		RtlCopyMemory(buffer,(PVOID)((ULONG_PTR)OutBuff+8),buff_size);
	}
	return st;
}

noir_status ncv_create_vcpu(cv_handle vm,u32 vpid)
{
	NOIR_STATUS st=noir_unsuccessful;
	ULONG_PTR InBuff[2]={vm,vpid};
	NoirControlDriver(IOCTL_CvmCreateVcpu,InBuff,sizeof(InBuff),&st,sizeof(st),NULL);
	return st;
}

noir_status ncv_delete_vcpu(cv_handle vm,u32 vpid)
{
	NOIR_STATUS st=noir_unsuccessful;
	ULONG_PTR InBuff[2]={vm,vpid};
	NoirControlDriver(IOCTL_CvmDeleteVcpu,InBuff,sizeof(InBuff),&st,sizeof(st),NULL);
	return st;
}