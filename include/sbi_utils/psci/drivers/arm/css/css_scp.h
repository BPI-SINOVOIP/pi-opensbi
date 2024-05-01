#ifndef __CSS_SCP_H__
#define __CSS_SCP_H__

#include <sbi_utils/psci/psci.h>

void css_scp_off(const struct psci_power_state *target_state);
void css_scp_on(u_register_t mpidr);
void css_scp_suspend(const struct psci_power_state *target_state);

#endif
