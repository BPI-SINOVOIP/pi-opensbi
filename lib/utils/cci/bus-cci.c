/*
 * Copyright (c) 2015-2020, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>


/* Slave interface offsets from PERIPHBASE */
#define SLAVE_IFACE6_OFFSET         (0x7000UL)
#define SLAVE_IFACE5_OFFSET         (0x6000UL)
#define SLAVE_IFACE4_OFFSET         (0x5000UL)
#define SLAVE_IFACE3_OFFSET         (0x4000UL)
#define SLAVE_IFACE2_OFFSET         (0x3000UL)
#define SLAVE_IFACE1_OFFSET         (0x2000UL)
#define SLAVE_IFACE0_OFFSET         (0x1000UL)
#define SLAVE_IFACE_OFFSET(index)   (SLAVE_IFACE0_OFFSET + \
                                    ((0x1000UL) * (index)))

/* Slave interface event and count register offsets from PERIPHBASE */
#define EVENT_SELECT7_OFFSET        (0x80000UL)
#define EVENT_SELECT6_OFFSET        (0x70000UL)
#define EVENT_SELECT5_OFFSET        (0x60000UL)
#define EVENT_SELECT4_OFFSET        (0x50000UL)
#define EVENT_SELECT3_OFFSET        (0x40000UL)
#define EVENT_SELECT2_OFFSET        (0x30000UL)
#define EVENT_SELECT1_OFFSET        (0x20000UL)
#define EVENT_SELECT0_OFFSET        (0x10000UL)
#define EVENT_OFFSET(index)         (EVENT_SELECT0_OFFSET + \
                                    ((0x10000UL) * (index)))

/* Control and ID register offsets */
#define CTRL_OVERRIDE_REG           (0x0U)
#define SECURE_ACCESS_REG           (0x8U)
#define STATUS_REG                  (0xcU)
#define IMPRECISE_ERR_REG           (0x10U)
#define PERFMON_CTRL_REG            (0x100U)
#define IFACE_MON_CTRL_REG          (0x104U)

/* Component and peripheral ID registers */
#define PERIPHERAL_ID0              (0xFE0U)
#define PERIPHERAL_ID1              (0xFE4U)
#define PERIPHERAL_ID2              (0xFE8U)
#define PERIPHERAL_ID3              (0xFECU)
#define PERIPHERAL_ID4              (0xFD0U)
#define PERIPHERAL_ID5              (0xFD4U)
#define PERIPHERAL_ID6              (0xFD8U)
#define PERIPHERAL_ID7              (0xFDCU)

#define COMPONENT_ID0               (0xFF0U)
#define COMPONENT_ID1               (0xFF4U)
#define COMPONENT_ID2               (0xFF8U)
#define COMPONENT_ID3               (0xFFCU)
#define COMPONENT_ID4               (0x1000U)
#define COMPONENT_ID5               (0x1004U)
#define COMPONENT_ID6               (0x1008U)
#define COMPONENT_ID7               (0x100CU)

/* Slave interface register offsets */
#define SNOOP_CTRL_REG                  (0x0U)
#define SH_OVERRIDE_REG                 (0x4U)
#define READ_CHNL_QOS_VAL_OVERRIDE_REG  (0x100U)
#define WRITE_CHNL_QOS_VAL_OVERRIDE_REG (0x104U)
#define MAX_OT_REG                      (0x110U)

/* Snoop Control register bit definitions */
#define DVM_EN_BIT          (1U<<1)
#define SNOOP_EN_BIT        (1U<<0)
#define SUPPORT_SNOOPS      (1U<<30)
#define SUPPORT_DVM         (1U<<31)

/* Status register bit definitions */
#define CHANGE_PENDING_BIT  (1U<<0)

/* Event and count register offsets */
#define EVENT_SELECT_REG    (0x0U)
#define EVENT_COUNT_REG     (0x4U)
#define COUNT_CNTRL_REG     (0x8U)
#define COUNT_OVERFLOW_REG  (0xCU)

/* Slave interface monitor registers */
#define INT_MON_REG_SI0         (0x90000U)
#define INT_MON_REG_SI1         (0x90004U)
#define INT_MON_REG_SI2         (0x90008U)
#define INT_MON_REG_SI3         (0x9000CU)
#define INT_MON_REG_SI4         (0x90010U)
#define INT_MON_REG_SI5         (0x90014U)
#define INT_MON_REG_SI6         (0x90018U)

/* Master interface monitor registers */
#define INT_MON_REG_MI0         (0x90100U)
#define INT_MON_REG_MI1         (0x90104U)
#define INT_MON_REG_MI2         (0x90108U)
#define INT_MON_REG_MI3         (0x9010cU)
#define INT_MON_REG_MI4         (0x90110U)
#define INT_MON_REG_MI5         (0x90114U)

#define SLAVE_IF_UNUSED         (-1)

#define MAKE_CCI_PART_NUMBER(hi, lo)    (((hi) << 8) | (lo))
#define CCI_PART_LO_MASK        (0xffU)
#define CCI_PART_HI_MASK        (0xfU)

/* CCI part number codes read from Peripheral ID registers 0 and 1 */
#define CCI400_PART_NUM         (0x420)
#define CCI500_PART_NUM         (0x422)
#define CCI550_PART_NUM         (0x423)

#define CCI400_SLAVE_PORTS      (5)
#define CCI500_SLAVE_PORTS      (7)
#define CCI550_SLAVE_PORTS      (7)

static void *cci_base;
static const int *cci_slave_if_map;


void cci_init(u32 base, const int *map, unsigned int num_cci_masters)
{
    cci_base = (void *)(u64)base;
    cci_slave_if_map = map;
}

void cci_enable_snoop_dvm_reqs(unsigned int master_id)
{
    int slave_if_id = cci_slave_if_map[master_id];

    /*
     * Enable Snoops and DVM messages, no need for Read/Modify/Write as
     * rest of bits are write ignore
     */
    writel(DVM_EN_BIT | SNOOP_EN_BIT, cci_base +
            SLAVE_IFACE_OFFSET(slave_if_id) + SNOOP_CTRL_REG);

    /*
     * Wait for the completion of the write to the Snoop Control Register
     * before testing the change_pending bit
     */
    mb();

    /* Wait for the dust to settle down */
    while ((readl(cci_base + STATUS_REG) & CHANGE_PENDING_BIT) != 0U)
        ;
}

void cci_disable_snoop_dvm_reqs(unsigned int master_id)
{
    int slave_if_id = cci_slave_if_map[master_id];

    /*
     * Disable Snoops and DVM messages, no need for Read/Modify/Write as
     * rest of bits are write ignore.
     */
    writel(~(DVM_EN_BIT | SNOOP_EN_BIT), cci_base +
            SLAVE_IFACE_OFFSET(slave_if_id) + SNOOP_CTRL_REG);

    /*
     * Wait for the completion of the write to the Snoop Control Register
     * before testing the change_pending bit
     */
    mb();

    /* Wait for the dust to settle down */
    while ((readl(cci_base + STATUS_REG) & CHANGE_PENDING_BIT) != 0U)
        ;
}

