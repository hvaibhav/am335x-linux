#ifdef __ASSEMBLER__
#define IOMEM(x)		(x)
#else
#define IOMEM(x)		((void __force __iomem *)(x))
#endif

#define OMAP1_IO_OFFSET		0x01000000	/* Virtual IO = 0xfefb0000 */
#define OMAP1_IO_ADDRESS(pa)	IOMEM((pa) - OMAP1_IO_OFFSET)

/*
 * ----------------------------------------------------------------------------
 * Omap1 specific IO mapping
 * ----------------------------------------------------------------------------
 */

#define OMAP1_IO_PHYS		0xFFFB0000
#define OMAP1_IO_SIZE		0x40000
#define OMAP1_IO_VIRT		(OMAP1_IO_PHYS - OMAP1_IO_OFFSET)
