/* Minimal host-test replacement for <machine/machine_routines.h>. */
#ifndef GTA4L_SIM_MACHINE_ROUTINES_H
#define GTA4L_SIM_MACHINE_ROUTINES_H
#include <sys/types.h>
vm_offset_t ml_io_map(vm_offset_t phys, vm_size_t size);
#endif
