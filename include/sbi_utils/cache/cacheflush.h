#ifndef __CACHE_FLUSH__H__
#define __CACHE_FLUSH__H__

#include <sbi/sbi_types.h>
#include <sbi/riscv_io.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi_utils/psci/psci.h>
#include <spacemit/spacemit_config.h>

#define __ALWAYS_STATIC_INLINE  __attribute__((always_inline)) static inline

/**
  \brief   Clear Dcache by addr
  \details Clear Dcache by addr.
  \param [in] addr  operate addr
 */
__ALWAYS_STATIC_INLINE void __DCACHE_CPA(uintptr_t addr)
{
	uintptr_t __v = addr;
	asm volatile ("cbo.clean" " 0(%0)" : : "rK"(__v) : "memory");
}

/**
  \brief   Invalid Dcache by addr
  \details Invalid Dcache by addr.
  \param [in] addr  operate addr
 */
__ALWAYS_STATIC_INLINE void __DCACHE_IPA(uintptr_t addr)
{
	uintptr_t __v = addr;
	asm volatile ("cbo.inval" " 0(%0)" : : "rK"(__v) : "memory");
}

/**
  \brief   Clear & Invalid Dcache by addr
  \details Clear & Invalid Dcache by addr.
  \param [in] addr  operate addr
 */
__ALWAYS_STATIC_INLINE void __DCACHE_CIPA(uintptr_t addr)
{
	uintptr_t __v = addr;
	asm volatile ("cbo.flush" " 0(%0)" : : "rK"(__v) : "memory");
}

/**
  \brief   Get MSTATUS
  \details Returns the content of the MSTATUS Register.
  \return               MSTATUS Register value
 */
__ALWAYS_STATIC_INLINE uintptr_t  __get_CurrentSP(void)
{
    uintptr_t result;

    asm volatile("move %0, sp" : "=r"(result));

    return (result);
}

__ALWAYS_STATIC_INLINE uintptr_t  __get_Supervisor_isr(void)
{
    uintptr_t result;

    asm volatile("csrr %0, mip" : "=r"(result));

    return (result & 0x222);
}
/**
  \brief   D-Cache Clean by address
  \details Cleans D-Cache for the given address
  \param[in]   addr    address (aligned to 32-byte boundary)
  \param[in]   dsize   size of memory block (in number of bytes)
*/
static inline void csi_dcache_clean_range (uintptr_t addr, unsigned int dsize)
{
    int op_size = dsize + addr % CACHE_LINE_SIZE;
    uintptr_t op_addr = addr & CACHE_INV_ADDR_Msk;

    asm volatile("fence rw, rw");

    while (op_size > 0) {
        __DCACHE_CPA(op_addr);
        op_addr += CACHE_LINE_SIZE;
        op_size -= CACHE_LINE_SIZE;
    }

    asm volatile("fence rw, rw");
    asm volatile("fence.i");
}

/**
  \brief   D-Cache Clean and Invalidate by address
  \details Cleans and invalidates D_Cache for the given address
  \param[in]   addr    address (aligned to 32-byte boundary)
  \param[in]   dsize   size of memory block (aligned to 16-byte boundary)
*/
static inline void csi_dcache_clean_invalid_range (uintptr_t addr, unsigned int dsize)
{
    int op_size = dsize + addr % CACHE_LINE_SIZE;
    uintptr_t op_addr = addr & CACHE_INV_ADDR_Msk;

    asm volatile("fence rw, rw");

    while (op_size > 0) {
        __DCACHE_CIPA(op_addr);
        op_addr += CACHE_LINE_SIZE;
        op_size -= CACHE_LINE_SIZE;
    }

    asm volatile("fence rw, rw");
    asm volatile("fence.i");
}

/**
  \brief   D-Cache Invalidate by address
  \details Invalidates D-Cache for the given address
  \param[in]   addr    address (aligned to 32-byte boundary)
  \param[in]   dsize   size of memory block (in number of bytes)
*/
static inline void csi_dcache_invalid_range (uintptr_t addr, unsigned int dsize)
{
    int op_size = dsize + addr % CACHE_LINE_SIZE;
    uintptr_t op_addr = addr & CACHE_INV_ADDR_Msk;

    asm volatile("fence rw, rw");

    while (op_size > 0) {
        __DCACHE_IPA(op_addr);
        op_addr += CACHE_LINE_SIZE;
        op_size -= CACHE_LINE_SIZE;
    }

    asm volatile("fence rw, rw");
    asm volatile("fence.i");
}

static inline void csi_enable_dcache(void)
{
	csr_set(CSR_MSETUP, 0x10073);
}

static inline void csi_disable_data_preftch(void)
{
	csr_clear(CSR_MSETUP, 32);
}

static inline void csi_disable_dcache(void)
{
	csr_clear(CSR_MSETUP, 1);
}

static inline void csi_flush_dcache_all(void)
{
	asm volatile ("csrwi 0x7c2, 0x3");
}

static inline void csi_invalidate_dcache_all(void)
{
	asm volatile ("csrwi 0x7c2, 0x2");
}

static inline void __mdelay(void)
{
	unsigned long long i;

	for (i = 0; i < 0xffffffff; ++i)
		cpu_relax();
}

static inline void csi_flush_l2_cache(bool hw)
{
	unsigned int hartid = current_hartid();

	uintptr_t *cr =(MPIDR_AFFLVL1_VAL(hartid) == 0) ? (uintptr_t *)CLUSTER0_L2_CACHE_FLUSH_REG_BASE :
			(uintptr_t *)CLUSTER1_L2_CACHE_FLUSH_REG_BASE;

	if (!hw) {
		writel(0x0, cr);
		/* flush l2 cache */
		writel(readl(cr) | (1 << L2_CACHE_FLUSH_REQUEST_BIT_OFFSET), cr);
		/* k1pro */
		if (L2_CACHE_FLUSH_REQUEST_BIT_OFFSET == L2_CACHE_FLUSH_DONE_BIT_OFFSET)
			while (readl(cr) & (1 << L2_CACHE_FLUSH_DONE_BIT_OFFSET));
		else /* k1x */ {
			/* clear the request */
			while (1) {
				if ((readl(cr) & (1 << L2_CACHE_FLUSH_DONE_BIT_OFFSET)) == 0)
					break;
				__mdelay();
			}
			writel(readl(cr) & ~(1 << L2_CACHE_FLUSH_REQUEST_BIT_OFFSET), cr);
		}
	} else {
		/* k1pro */
		if (L2_CACHE_FLUSH_REQUEST_BIT_OFFSET == L2_CACHE_FLUSH_DONE_BIT_OFFSET)
			return /* do nothing */;
		writel((1 << L2_CACHE_FLUSH_HW_TYPE_BIT_OFFSET) | (1 << L2_CACHE_FLUSH_HW_EN_BIT_OFFSET), cr);
	}
}
#endif
