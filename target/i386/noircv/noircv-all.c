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

static bool noircv_allowed;

int noircv_enabled(void)
{
	return noircv_allowed;
}

static int ncv_init(ram_addr_t ram_size,int max_cpus)
{
	return 0;
}

static int ncv_accel_init(MachineState *ms)
{
	int ret=ncv_init(ms->ram_size,(int)ms->smp.max_cpus);
	fprintf(stderr,"NoirVisor is %s in the system!\n",ret?"present":"absent");
	return ret;
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