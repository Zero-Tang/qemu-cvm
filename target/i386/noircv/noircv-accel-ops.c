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
#include "sysemu/kvm_int.h"
#include "qemu/main-loop.h"
#include "sysemu/cpus.h"
#include "qemu/guest-random.h"

#include "sysemu/noircv.h"
#include "noircv-accel-ops.h"

static void ncv_kick_vcpu_thread(CPUState *cpu)
{
    if(!qemu_cpu_is_self(cpu))ncv_kick_vcpu(cpu);
}

static void* ncv_cpu_thread_rt(void* arg)
{
    CPUState *cpu=arg;
    rcu_register_thread();
    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id=qemu_get_thread_id();
    current_cpu=cpu;
    ncv_init_vcpu(cpu);
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);
    do
    {
        if(cpu_can_run(cpu))
            if(ncv_exec_vcpu(cpu)==EXCP_DEBUG)
                cpu_handle_guest_debug(cpu);
        while(cpu_thread_is_idle(cpu))
            qemu_cond_wait_iothread(cpu->halt_cond);
        qemu_wait_io_event_common(cpu);
    }while(!cpu->unplug || cpu_can_run(cpu));
    ncv_destroy_vcpu(cpu);
    cpu_thread_signal_destroyed(cpu);
    qemu_mutex_unlock_iothread();
    rcu_unregister_thread();
    return NULL;
}

static void ncv_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];
    cpu->thread=g_new0(QemuThread,1);
    cpu->halt_cond=g_new0(QemuCond,1);
    qemu_cond_init(cpu->halt_cond);
    snprintf(thread_name,VCPU_THREAD_NAME_SIZE,"NoirCV vCPU %d",cpu->cpu_index);
    qemu_thread_create(cpu->thread,thread_name,ncv_cpu_thread_rt,cpu,QEMU_THREAD_JOINABLE);
#ifdef _WIN32
    cpu->hThread=qemu_thread_get_handle(cpu->thread);
#endif
}

static void noircv_accel_ops_class_init(ObjectClass *oc,void *data)
{
    AccelOpsClass *ops=ACCEL_OPS_CLASS(oc);
    ops->create_vcpu_thread=ncv_start_vcpu_thread;
    ops->kick_vcpu_thread=ncv_kick_vcpu_thread;
    ops->synchronize_post_init=ncv_cpu_synchronize_post_init;
    ops->synchronize_post_reset=ncv_cpu_synchronize_post_reset;
    ops->synchronize_pre_loadvm=ncv_cpu_synchronize_pre_loadvm;
    ops->synchronize_state=ncv_cpu_synchronize_state;
}

static const TypeInfo noircv_accel_ops_type=
{
    .name=ACCEL_OPS_NAME("noircv"),
    .parent=TYPE_ACCEL_OPS,
    .class_init=noircv_accel_ops_class_init,
    .abstract=true
};

static void noircv_accel_ops_register_types(void)
{
    type_register_static(&noircv_accel_ops_type);
}

type_init(noircv_accel_ops_register_types);