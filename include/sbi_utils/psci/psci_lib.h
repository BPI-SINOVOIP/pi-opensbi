#ifndef __PSCI_LIB_H__
#define __PSCI_LIB_H__

int psci_setup(void);
void psci_print_power_domain_map(void);
void psci_warmboot_entrypoint(void);

#endif
