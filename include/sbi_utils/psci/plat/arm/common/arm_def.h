#ifndef __ARM_DEF_H__
#define __ARM_DEF_H__

#define MPIDR_AFFLVL0           0ULL
#define MPIDR_AFFLVL1           1ULL
#define MPIDR_AFFLVL2           2ULL
#define MPIDR_AFFLVL3           3ULL

/*
 * Macros mapping the MPIDR Affinity levels to ARM Platform Power levels. The
 * power levels have a 1:1 mapping with the MPIDR affinity levels.
 */
#define ARM_PWR_LVL0            MPIDR_AFFLVL0
#define ARM_PWR_LVL1            MPIDR_AFFLVL1
#define ARM_PWR_LVL2            MPIDR_AFFLVL2
#define ARM_PWR_LVL3            MPIDR_AFFLVL3


#endif
