/*
 * NoirVisor Accelerator Interface
 *
 * Copyright 2022 Zero Tang
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef NOIRCV_CPUS_H
#define NOIRCV_CPUS_H

#include "sysemu/cpus.h"

#include "noircv-interface.h"

void ncv_init_vcpu(CPUState *cpu);
int ncv_exec_vcpu(CPUState *cpu);
void ncv_destroy_vcpu(CPUState *cpu);
void ncv_kick_vcpu(CPUState *cpu);

void ncv_cpu_synchronize_state(CPUState *cpu);
void ncv_cpu_synchronize_post_reset(CPUState *cpu);
void ncv_cpu_synchronize_post_init(CPUState *cpu);
void ncv_cpu_synchronize_pre_loadvm(CPUState *cpu);

noir_status ncv_create_vm(cv_handle *vm);
noir_status ncv_delete_vm(cv_handle vm);
noir_status ncv_create_vcpu(cv_handle vm,u32 vpid);
noir_status ncv_delete_vcpu(cv_handle vm,u32 vpid);
noir_status ncv_set_mapping(cv_handle vm,cv_addr_map_info *mapping_info);
noir_status ncv_inject_event(cv_handle vm,u32 vpid,bool valid,u8 vector,u8 type,u8 priority,bool error_code_valid,u32 err_code);
noir_status ncv_edit_vcpu_register(cv_handle vm,u32 vpid,cv_reg_type reg_type,void* buffer,u32 buff_size);
noir_status ncv_view_vcpu_register(cv_handle vm,u32 vpid,cv_reg_type reg_type,void* buffer,u32 buff_size);
noir_status ncv_run_vcpu(cv_handle vm,u32 vpid,cv_exit_context *exit_context);
noir_status ncv_rescind_vcpu(cv_handle vm,u32 vpid);
noir_status ncv_try_cvexit_emulation(cv_handle vm,u32 vpid,cv_emu_info_header_p info);
int ncv_win_init(void);

#define NOIRCV_SET_RUNTIME_STATE    1
#define NOIRCV_SET_RESET_STATE      2
#define NOIRCV_SET_FULL_STATE       3

#endif /* NOIRCV_CPUS_H */
