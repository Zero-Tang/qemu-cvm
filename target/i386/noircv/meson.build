i386_softmmu_ss.add(when: 'CONFIG_NOIRCV', if_true: files(
    'noircv-all.c',
    'noircv-accel-ops.c',
))
i386_softmmu_ss.add(when: ['CONFIG_NOIRCV', 'CONFIG_WIN32'], if_true: files('noircv-windows.c'))