#ifndef __CSS_ARM_H__
#define __CSS_ARM_H__

#include <sbi/sbi_types.h>
#include <sbi_utils/psci/psci.h>
#include <sbi_utils/psci/plat/arm/common/arm_def.h>
#include <sbi_utils/psci/plat/arm/board/spacemit/include/platform_def.h>
#include <spacemit/spacemit_config.h>

#define SCMI_DOMAIN_ID_MASK             0xFFFFU
#define SCMI_CHANNEL_ID_MASK            0xFFFFU
#define SCMI_CHANNEL_ID_SHIFT           16U

#define SET_SCMI_CHANNEL_ID(n)          (((n) & SCMI_CHANNEL_ID_MASK) << \
                                         SCMI_CHANNEL_ID_SHIFT)
#define SET_SCMI_DOMAIN_ID(n)           ((n) & SCMI_DOMAIN_ID_MASK)
#define GET_SCMI_CHANNEL_ID(n)          (((n) >> SCMI_CHANNEL_ID_SHIFT) & \
                                         SCMI_CHANNEL_ID_MASK)
#define GET_SCMI_DOMAIN_ID(n)           ((n) & SCMI_DOMAIN_ID_MASK)

/* Macros to read the CSS power domain state */
#define CSS_CORE_PWR_STATE(state)       (state)->pwr_domain_state[ARM_PWR_LVL0]
#define CSS_CLUSTER_PWR_STATE(state)    (state)->pwr_domain_state[ARM_PWR_LVL1]

static inline unsigned int css_system_pwr_state(const psci_power_state_t *state)
{
#if (PLAT_MAX_PWR_LVL == CSS_SYSTEM_PWR_DMN_LVL)
        return state->pwr_domain_state[CSS_SYSTEM_PWR_DMN_LVL];
#else
        return 0;
#endif
}

extern uint32_t plat_css_core_pos_to_scmi_dmn_id_map[PLATFORM_CLUSTER_COUNT][PLATFORM_CORE_COUNT];

#endif
