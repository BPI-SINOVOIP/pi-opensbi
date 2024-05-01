#include <sbi_utils/psci/psci.h>

static unsigned char plat_power_domain_tree_desc[] = {
	/* No of root nodes */
	1,
	/* Num of children for the root node */
	0,
	/* Num of children for the first cluster node */
	0,
	/* Num of children for the second cluster node */
	0,
};

int plat_core_pos_by_mpidr(u_register_t mpidr)
{
	unsigned int cluster = MPIDR_AFFLVL1_VAL(mpidr);
	unsigned int core = MPIDR_AFFLVL0_VAL(mpidr);

	return (cluster == 0) ? core : 
		(plat_power_domain_tree_desc[2] + core);
}

unsigned char *plat_get_power_domain_tree_desc(void)
{
        return plat_power_domain_tree_desc;
}
