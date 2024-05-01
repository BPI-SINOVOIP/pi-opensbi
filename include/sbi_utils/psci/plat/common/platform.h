#ifndef __PSCI_PLAT_COMMON_H__
#define __PSCI_PLAT_COMMON_H__

#include <sbi/sbi_types.h>
#include <sbi_utils/psci/psci.h>

unsigned char *plat_get_power_domain_tree_desc(void);

int plat_setup_psci_ops(uintptr_t sec_entrypoint,
                        const struct plat_psci_ops **psci_ops);
int plat_core_pos_by_mpidr(u_register_t mpidr);

#endif
