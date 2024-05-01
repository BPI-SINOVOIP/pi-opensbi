/*
 * Copyright (c) 2014-2020, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <sbi_utils/psci/drivers/arm/css/scmi.h>
#include <sbi_utils/psci/drivers/arm/css/scmi_private.h>
#include "mhu.h"

void mhu_ring_doorbell(struct scmi_channel_plat_info *plat_info)
{
	unsigned int msg;
	mbox_reg_desc_t *regs = (mbox_reg_desc_t *)plat_info->db_reg_addr;

	/* clear the fifo */
        while (regs->msg_status[MAILBOX_SECURE_PSCI_CHANNEL + 2].bits.num_msg) {
                msg = regs->mbox_msg[MAILBOX_SECURE_PSCI_CHANNEL + 2].val;
        }

	/* clear pending */
        msg = regs->mbox_irq[0].irq_status_clr.val;
        msg |= (1 << ((MAILBOX_SECURE_PSCI_CHANNEL + 2) * 2));
        regs->mbox_irq[0].irq_status_clr.val = msg;

	/* door bell the esos */
	regs->mbox_msg[MAILBOX_SECURE_PSCI_CHANNEL].val = 'c';
}
