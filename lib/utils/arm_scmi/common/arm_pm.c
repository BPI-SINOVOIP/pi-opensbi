/*
 * Copyright (c) 2015-2020, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sbi_utils/psci/psci.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_hart.h>
#include <sbi_utils/psci/plat/arm/common/plat_arm.h>
#include <sbi_utils/psci/plat/arm/common/arm_def.h>

/*******************************************************************************
 * ARM standard platform handler called to check the validity of the power state
 * parameter.
 ******************************************************************************/
int arm_validate_power_state(unsigned int power_state,
                            psci_power_state_t *req_state)
{
        unsigned int pstate = psci_get_pstate_type(power_state);
        unsigned int pwr_lvl = psci_get_pstate_pwrlvl(power_state);
        unsigned int i;

        if (req_state == NULL) {
		sbi_printf("%s:%d\n", __func__, __LINE__);
		sbi_hart_hang();
	}

        if (pwr_lvl > PLAT_MAX_PWR_LVL)
                return PSCI_E_INVALID_PARAMS;

        /* Sanity check the requested state */
        if (pstate == PSTATE_TYPE_STANDBY) {
                /*
                 * It's possible to enter standby only on power level 0
                 * Ignore any other power level.
                 */
                if (pwr_lvl != ARM_PWR_LVL0)
                        return PSCI_E_INVALID_PARAMS;

                req_state->pwr_domain_state[ARM_PWR_LVL0] =
                                        ARM_LOCAL_STATE_RET;
        } else {
                for (i = ARM_PWR_LVL0; i <= pwr_lvl; i++)
                        req_state->pwr_domain_state[i] =
                                        ARM_LOCAL_STATE_OFF;
        }

        /*
         * We expect the 'state id' to be zero.
         */
        if (psci_get_pstate_id(power_state) != 0U)
                return PSCI_E_INVALID_PARAMS;

        return PSCI_E_SUCCESS;
}

/*******************************************************************************
 * The ARM Standard platform definition of platform porting API
 * `plat_setup_psci_ops`.
 ******************************************************************************/
int plat_setup_psci_ops(uintptr_t sec_entrypoint,
				const plat_psci_ops_t **psci_ops)
{
	*psci_ops = plat_arm_psci_override_pm_ops(&plat_arm_psci_pm_ops);

	return 0;
}
