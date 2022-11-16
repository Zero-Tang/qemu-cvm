/*
 * QEMU NoirVisor Customizable Virtual Machine (NoirCV) support
 *
 * Copyright Zero Tang. 2022
 *
 * Authors:
 *
 * Zero Tang
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_NOIRCV_H
#define QEMU_NOIRCV_H

#ifdef NEED_CPU_H

#ifdef CONFIG_NOIRCV

int noircv_enabled(void);

#else /* CONFIG_NOIRCV */

#define noircv_enabled() (0)

#endif /* CONFIG_WHPX */

#endif /* NEED_CPU_H */

#endif /* QEMU_NOIRCV_H */
