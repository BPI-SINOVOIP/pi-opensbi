/*
 * Copyright (c) 2015-2018, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __CCI_H__
#define __CCI_H__

/* Function declarations */

/*
 * The ARM CCI driver needs the following:
 * 1. Base address of the CCI product
 * 2. An array  of map between AMBA 4 master ids and ACE/ACE lite slave
 *    interfaces.
 * 3. Size of the array.
 *
 * SLAVE_IF_UNUSED should be used in the map to represent no AMBA 4 master exists
 * for that interface.
 */
void cci_init(uintptr_t base, const int *map, unsigned int num_cci_masters);

void cci_enable_snoop_dvm_reqs(unsigned int master_id);
void cci_disable_snoop_dvm_reqs(unsigned int master_id);

#endif /* CCI_H */
