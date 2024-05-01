#ifndef __PLAT_ARM_H__
#define __PLAT_ARM_H__

#include <sbi_utils/psci/psci.h>
#include <sbi/riscv_locks.h>
#include <sbi_utils/psci/plat/arm/common/plat_arm.h>

#define ARM_SCMI_INSTANTIATE_LOCK       spinlock_t arm_scmi_lock

#define ARM_SCMI_LOCK_GET_INSTANCE      (&arm_scmi_lock)

extern plat_psci_ops_t plat_arm_psci_pm_ops;

const plat_psci_ops_t *plat_arm_psci_override_pm_ops(plat_psci_ops_t *ops);

void plat_arm_pwrc_setup(void);

int arm_validate_power_state(unsigned int power_state,
                            psci_power_state_t *req_state);

#endif
