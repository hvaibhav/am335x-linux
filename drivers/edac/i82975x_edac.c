/*
 * Intel 82975X Memory Controller kernel module
 * (C) 2007 aCarLab (India) Pvt. Ltd. (http://acarlab.com)
 * (C) 2007 jetzbroadband (http://jetzbroadband.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Arvind R.
 *   Copied from i82875p_edac.c source,
 *
 * (c) 2012 Mauro Carvalho Chehab <mchehab@redhat.com>
 *	Driver re-written in order to fix lots of issues on it:
 *		- Fix Single Mode;
 *		- Add support for Asymetrical mode
 *		- Fix several issues with Interleaved mode
 *		- Fix memory label logic
 *
 * Intel datasheet: Intel(R) 975X Express Chipset Datasheet
 *	http://www.intel.com/assets/pdf/datasheet/310158.pdf
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/edac.h>
#include "edac_core.h"

#define I82975X_REVISION	" Ver: 1.0.0"
#define EDAC_MOD_STR		"i82975x_edac"

#define i82975x_printk(level, fmt, arg...) \
	edac_printk(level, "i82975x", fmt, ##arg)

#define i82975x_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "i82975x", fmt, ##arg)

#ifndef PCI_DEVICE_ID_INTEL_82975_0
#define PCI_DEVICE_ID_INTEL_82975_0	0x277c
#endif				/* PCI_DEVICE_ID_INTEL_82975_0 */

#define DIMMS_PER_CHANNEL	4
#define NUM_CHANNELS		2

/* Intel 82975X register addresses - device 0 function 0 - DRAM Controller */
#define I82975X_EAP		0x58	/* Dram Error Address Pointer (32b)
					 *
					 * 31:7  128 byte cache-line address
					 * 6:1   reserved
					 * 0     0: CH0; 1: CH1
					 */

#define I82975X_DERRSYN		0x5c	/* Dram Error SYNdrome (8b)
					 *
					 *  7:0  DRAM ECC Syndrome
					 */

#define I82975X_DES		0x5d	/* Dram ERRor DeSTination (8b)
					 * 0h:    Processor Memory Reads
					 * 1h:7h  reserved
					 * More - See Page 65 of Intel DocSheet.
					 */

#define I82975X_ERRSTS		0xc8	/* Error Status Register (16b)
					 *
					 * 15:12 reserved
					 * 11    Thermal Sensor Event
					 * 10    reserved
					 *  9    non-DRAM lock error (ndlock)
					 *  8    Refresh Timeout
					 *  7:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

/* Error Reporting is supported by 3 mechanisms:
  1. DMI SERR generation  ( ERRCMD )
  2. SMI DMI  generation  ( SMICMD )
  3. SCI DMI  generation  ( SCICMD )
NOTE: Only ONE of the three must be enabled
*/
#define I82975X_ERRCMD		0xca	/* Error Command (16b)
					 *
					 * 15:12 reserved
					 * 11    Thermal Sensor Event
					 * 10    reserved
					 *  9    non-DRAM lock error (ndlock)
					 *  8    Refresh Timeout
					 *  7:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

#define I82975X_SMICMD		0xcc	/* Error Command (16b)
					 *
					 * 15:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

#define I82975X_SCICMD		0xce	/* Error Command (16b)
					 *
					 * 15:2  reserved
					 *  1    ECC UE (multibit DRAM error)
					 *  0    ECC CE (singlebit DRAM error)
					 */

#define I82975X_XEAP	0xfc	/* Extended Dram Error Address Pointer (8b)
					 *
					 * 7:1   reserved
					 * 0     Bit32 of the Dram Error Address
					 */

#define I82975X_MCHBAR		0x44	/*
					 *
					 * 31:14 Base Addr of 16K memory-mapped
					 *	configuration space
					 * 13:1  reserverd
					 *  0    mem-mapped config space enable
					 */

/* NOTE: Following addresses have to indexed using MCHBAR offset (44h, 32b) */
/* Intel 82975x memory mapped register space */

#define I82975X_DRB_SHIFT 25	/* fixed 2^25 = 32 MiB grain */

#define I82975X_DRB		0x100	/* DRAM Row Boundary (8b x 8)
					 *
					 * 7   set to 1 in highest DRB of
					 *	channel if 4GB in ch.
					 * 6:2 upper boundary of rank in
					 *	32MB grains
					 * 1:0 set to 0
					 */
#define I82975X_DRB_CH0R0		0x100
#define I82975X_DRB_CH0R1		0x101
#define I82975X_DRB_CH0R2		0x102
#define I82975X_DRB_CH0R3		0x103
#define I82975X_DRB_CH1R0		0x180
#define I82975X_DRB_CH1R1		0x181
#define I82975X_DRB_CH1R2		0x182
#define I82975X_DRB_CH1R3		0x183


#define I82975X_DRA		0x108	/* DRAM Row Attribute (4b x 8)
					 *  defines the PAGE SIZE to be used
					 *	for the rank
					 *  7    reserved
					 *  6:4  row attr of odd rank, i.e. 1
					 *  3    reserved
					 *  2:0  row attr of even rank, i.e. 0
					 *
					 * 000 = unpopulated
					 * 001 = reserved
					 * 010 = 4KiB
					 * 011 = 8KiB
					 * 100 = 16KiB
					 * others = reserved
					 */
#define I82975X_DRA_CH0R01		0x108
#define I82975X_DRA_CH0R23		0x109
#define I82975X_DRA_CH1R01		0x188
#define I82975X_DRA_CH1R23		0x189

/* Channels 0/1 DRAM Timing Register 1 */
#define I82975X_C0DRT1			0x114
#define I82975X_C1DRT1			0x194


#define I82975X_BNKARC	0x10e /* Type of device in each rank - Bank Arch (16b)
					 *
					 * 15:8  reserved
					 * 7:6  Rank 3 architecture
					 * 5:4  Rank 2 architecture
					 * 3:2  Rank 1 architecture
					 * 1:0  Rank 0 architecture
					 *
					 * 00 => 4 banks
					 * 01 => 8 banks
					 */
#define I82975X_C0BNKARC	0x10e
#define I82975X_C1BNKARC	0x18e



#define I82975X_DRC		0x120 /* DRAM Controller Mode0 (32b)
					 *
					 * 31:30 reserved
					 * 29    init complete
					 * 28:11 reserved, according to Intel
					 *    22:21 number of channels
					 *		00=1 01=2 in 82875
					 *		seems to be ECC mode
					 *		bits in 82975 in Asus
					 *		P5W
					 *	 19:18 Data Integ Mode
					 *		00=none 01=ECC in 82875
					 * 10:8  refresh mode
					 *  7    reserved
					 *  6:4  mode select
					 *  3:2  reserved
					 *  1:0  DRAM type 10=Second Revision
					 *		DDR2 SDRAM
					 *         00, 01, 11 reserved
					 */
#define I82975X_DRC_CH0M0		0x120
#define I82975X_DRC_CH1M0		0x1A0


#define I82975X_DRC_M1	0x124 /* DRAM Controller Mode1 (32b)
					 * 31	0=Standard Address Map
					 *	1=Enhanced Address Map
					 * 30:0	reserved
					 */

#define I82975X_DRC_CH0M1		0x124
#define I82975X_DRC_CH1M1		0x1A4

enum i82975x_chips {
	I82975X = 0,
};

struct mem_range {
	u32 start, end;
};

struct i82975x_pvt {
	void __iomem		*mch_window;
	int 			num_channels;
	bool			is_symetric;
	u8			drb[DIMMS_PER_CHANNEL][NUM_CHANNELS];
	struct mem_range	page[DIMMS_PER_CHANNEL][NUM_CHANNELS];
};

struct i82975x_dev_info {
	const char *ctl_name;
};

struct i82975x_error_info {
	u16 errsts;
	u32 eap;
	u8 des;
	u8 derrsyn;
	u16 errsts2;
	u8 chan;		/* the channel is bit 0 of EAP */
	u8 xeap;		/* extended eap bit */
};

static const struct i82975x_dev_info i82975x_devs[] = {
	[I82975X] = {
		.ctl_name = "i82975x"
	},
};

static struct pci_dev *mci_pdev;	/* init dev: in case that AGP code has
					 * already registered driver
					 */

static int i82975x_registered = 1;

static void i82975x_get_error_info(struct mem_ctl_info *mci,
		struct i82975x_error_info *info)
{
	struct pci_dev *pdev;

	pdev = to_pci_dev(mci->pdev);

	/*
	 * This is a mess because there is no atomic way to read all the
	 * registers at once and the registers can transition from CE being
	 * overwritten by UE.
	 */
	pci_read_config_word(pdev, I82975X_ERRSTS, &info->errsts);
	pci_read_config_dword(pdev, I82975X_EAP, &info->eap);
	pci_read_config_byte(pdev, I82975X_XEAP, &info->xeap);
	pci_read_config_byte(pdev, I82975X_DES, &info->des);
	pci_read_config_byte(pdev, I82975X_DERRSYN, &info->derrsyn);
	pci_read_config_word(pdev, I82975X_ERRSTS, &info->errsts2);

	pci_write_bits16(pdev, I82975X_ERRSTS, 0x0003, 0x0003);

	/*
	 * If the error is the same then we can for both reads then
	 * the first set of reads is valid.  If there is a change then
	 * there is a CE no info and the second set of reads is valid
	 * and should be UE info.
	 */
	if (!(info->errsts2 & 0x0003))
		return;

	if ((info->errsts ^ info->errsts2) & 0x0003) {
		pci_read_config_dword(pdev, I82975X_EAP, &info->eap);
		pci_read_config_byte(pdev, I82975X_XEAP, &info->xeap);
		pci_read_config_byte(pdev, I82975X_DES, &info->des);
		pci_read_config_byte(pdev, I82975X_DERRSYN,
				&info->derrsyn);
	}
}

static int i82975x_process_error_info(struct mem_ctl_info *mci,
		struct i82975x_error_info *info, int handle_errors)
{
	struct i82975x_pvt *pvt = mci->pvt_info;
	struct mem_range	*range;
	unsigned int		row, chan, grain;
	unsigned long		offst, page;

	if (!(info->errsts2 & 0x0003))
		return 0;

	if (!handle_errors)
		return 1;

	if ((info->errsts ^ info->errsts2) & 0x0003) {
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1, 0, 0, 0,
				     -1, -1, -1, "UE overwrote CE", "");
		info->errsts = info->errsts2;
	}

	/* Calculate page and offset of the error */

	page = (unsigned long) info->eap;
	page >>= 1;

	if (info->xeap & 1)
		page |= 0x80000000;
	page >>= (PAGE_SHIFT - 1);

	if (pvt->is_symetric)
		grain = 1 << 7;
	else
		grain = 1 << 6;

	offst = info->eap & ((1 << PAGE_SHIFT) - (1 << grain));

	/*
	 * Search for the DIMM chip that match the error page.
	 *
	 * On Symmetric mode, this will always return channel = 0, as
	 * both channel A and B ranges are identical.
	 * A latter logic will determinte the channel on symetric mode
	 *
	 * On asymetric mode or single mode, there will be just one match,
	 * that will point to the csrow with the error.
	 */
	for (chan = 0; chan < pvt->num_channels; chan++) {
		for (row = 0; row < DIMMS_PER_CHANNEL; row++) {
			range = &pvt->page[row][chan];

			if (page >= range->start && page <= range->end)
				goto found;
		}
	}
	chan = -1;
	row = -1;

found:
	if (info->errsts & 0x0002) {
		/*
		 * On uncorrected error, ECC doesn't allow do determine the
		 * channel where the error has occurred.
		 */
		edac_mc_handle_error(HW_EVENT_ERR_UNCORRECTED, mci, 1,
				     page, offst, 0,
				     row, -1, -1,
				     "i82975x UE", "");
		return 1;
	}

	if (pvt->is_symetric && row >= 0) {
		/*
		 * On Symetric mode, the memory switch happens after each
		 * cache line (64 byte boundary). Channel 0 goes first.
		 */
		if (info->eap & (1 << 6))
			chan = 1;
		else
			chan = 0;
	}
	edac_mc_handle_error(HW_EVENT_ERR_CORRECTED, mci, 1,
				page, offst, info->derrsyn,
				row, chan, -1,
				"i82975x CE", "");

	return 1;
}

static void i82975x_check(struct mem_ctl_info *mci)
{
	struct i82975x_error_info info;

	edac_dbg(4, "MC%d\n", mci->mc_idx);
	i82975x_get_error_info(mci, &info);
	i82975x_process_error_info(mci, &info, 1);
}

/**
 * detect_memory_style - Detect on what mode the memory controller is programmed
 *
 * @pvt:		pointer to the private structure
 *
 * This function detects how many channels are in use, and if the memory
 * controller is in symetric (interleaved) or asymetric mode. There's no
 * need to distinguish between asymetric and single mode, as the routines
 * that fill the csrows data and handle error are written in order to handle
 * both at the same way.
 */
static void detect_memory_style(struct i82975x_pvt *pvt)
{
	int	row;
	bool has_chan_a = false;
	bool has_chan_b = false;

	pvt->is_symetric = true;
	pvt->num_channels = 0;

	for (row = 0; row < DIMMS_PER_CHANNEL; row++) {
		pvt->drb[row][0] = readb(pvt->mch_window + I82975X_DRB + row);
		pvt->drb[row][1] = readb(pvt->mch_window + I82975X_DRB + row + 0x80);

		/* On symetric mode, both channels have the same boundaries */
		if (pvt->drb[row][0] != pvt->drb[row][1])
			pvt->is_symetric = false;

		if (pvt->drb[row][0])
			has_chan_a = true;
		if (pvt->drb[row][1])
			has_chan_b = true;
	}

	if (has_chan_a)
		pvt->num_channels++;

	if (has_chan_b)
		pvt->num_channels++;
}

static void i82975x_init_csrows(struct mem_ctl_info *mci,
				struct i82975x_pvt *pvt,
				struct pci_dev *pdev)
{
	struct dimm_info	*dimm;
	struct mem_range	*range;
	u8			boundary;
	u32			initial_page = 0, last_page;
	int			row, chan;

	/*
	 * This chipset provides 3 address modes:
	 * Single channel - either Channel A or channel B is filled
	 * Dual channel, interleaved: Memory is organized in pairs,
	 * 	where channel A gets the lower address for each pair
	 * Dual channel, asymmetric: Channel A memory goes first.
	 * In order to cover all modes, we need to start describing
	 * memories considering the dual channel, asymmetric one.
	 */

	for (chan = 0; chan < pvt->num_channels; chan++) {
		/*
		 * On symetric mode, both channels start from address 0
		 */
		if (pvt->is_symetric)
			initial_page = 0;

		for (row = 0; row < DIMMS_PER_CHANNEL; row++) {
			boundary = pvt->drb[row][chan];
			dimm = mci->csrows[row]->channels[chan]->dimm;

			last_page = boundary << (I82975X_DRB_SHIFT - PAGE_SHIFT);
			dimm->nr_pages = last_page - initial_page;
			if (!dimm->nr_pages)
				continue;

			range = &pvt->page[row][chan];
			range->start = initial_page;
			range->end = range->start + dimm->nr_pages - 1;

			/*
			 * Grain is one cache-line:
			 * On dual symetric mode, it is 128 Bytes;
			 * On single mode or asymetric, it is 64 bytes.
			 */
			if (pvt->is_symetric) {
				dimm->grain = 1 << 7;

				/*
				 * In dual interleaved mode, the addresses
				 * need to be multiplied by 2, as both
				 * channels are interlaced, and the boundary
				 * limit there actually match each DIMM size
				 */
				range->start <<= 1;
				range->end <<= 1;
			} else {
				dimm->grain = 1 << 6;
			}

			snprintf(dimm->label,
				 EDAC_MC_LABEL_LEN, "DIMM %c%d",
				 (chan == 0) ? 'A' : 'B', row);
			dimm->mtype = MEM_DDR2; /* I82975x supports only DDR2 */
			dimm->edac_mode = EDAC_SECDED; /* only supported */

			/*
			 * This chipset supports both x8 and x16 memories,
			 * but datasheet doesn't describe how to distinguish
			 * between them.
			 *
			 * Also, the "Rank" comment at initial_page 17 says that
			 * ECC is only available with x8 memories. As this
			 * driver doesn't even initialize without ECC, better
			 * to assume that everything is x8. This is not
			 * actually true, on a mixed ECC/non-ECC scenario.
			 */
			dimm->dtype = DEV_X8;

			edac_dbg(1,
				 "%s: from page 0x%08x to 0x%08x (size: 0x%08x pages)\n",
				 dimm->label,
				 range->start, range->end,
				 dimm->nr_pages);
			initial_page = last_page;
		}
	}
}

static void i82975x_print_dram_config(struct i82975x_pvt *pvt,
				      u32 mchbar, u32 *drc)
{
#ifdef CONFIG_EDAC_DEBUG
	/*
	 * The register meanings are from Intel specs;
	 * (shows 13-5-5-5 for 800-DDR2)
	 * Asus P5W Bios reports 15-5-4-4
	 * What's your religion?
	 */
	static const int	caslats[4] = { 5, 4, 3, 6 };
	u32			dtreg[2];
	int			row;

	/* Show memory config if debug level is 1 or upper */
	if (!edac_debug_level)
		return;

	i82975x_printk(KERN_INFO, "MCHBAR real = %0x, remapped = %p\n",
		       mchbar, pvt->mch_window);

	for (row = 0; row < DIMMS_PER_CHANNEL; row++) {
		if (row)
			/* Only show if at least one bank is filled */
			if ((pvt->drb[row][0] == pvt->drb[row-1][0]) &&
			    (pvt->drb[row][1] == pvt->drb[row-1][1]))
				continue;

		i82975x_printk(KERN_INFO,
			       "DRAM%i Rank Boundary Address: Channel A: 0x%08x; Channel B: 0x%08x\n",
			       row,
			       pvt->drb[row][0],
			       pvt->drb[row][1]);
	}

	i82975x_printk(KERN_INFO, "DRAM Controller mode Channel A: = 0x%08x (%s); Channel B: 0x%08x (%s)\n",
		       drc[0],
		       ((drc[0] >> 21) & 3) == 1 ?
		       "ECC enabled" : "ECC disabled",
		       drc[1],
		       ((drc[1] >> 21) & 3) == 1 ?
		       "ECC enabled" : "ECC disabled");

	i82975x_printk(KERN_INFO, "Bank Architecture Channel A: 0x%08x, Channel B: 0x%08x\n",
		       readw(pvt->mch_window + I82975X_C0BNKARC),
		       readw(pvt->mch_window + I82975X_C1BNKARC));

	dtreg[0] = readl(pvt->mch_window + I82975X_C0DRT1);
	dtreg[1] = readl(pvt->mch_window + I82975X_C1DRT1);
	i82975x_printk(KERN_INFO, "DRAM Timings :      ChA    ChB\n");
	i82975x_printk(KERN_INFO, "  RAS Active Min =  %2d      %2d\n",
		       (dtreg[0] >> 19 ) & 0x0f,(dtreg[1] >> 19) & 0x0f);
	i82975x_printk(KERN_INFO, "  CAS latency    =  %2d      %2d\n",
		       caslats[(dtreg[0] >> 8) & 0x03],
		       caslats[(dtreg[1] >> 8) & 0x03]);
	i82975x_printk(KERN_INFO, "  RAS to CAS     =  %2d      %2d\n",
		       ((dtreg[0] >> 4) & 0x07) + 2,
		       ((dtreg[1] >> 4) & 0x07) + 2);
	i82975x_printk(KERN_INFO, "  RAS precharge  =  %2d      %2d\n",
		(dtreg[0] & 0x07) + 2,
		(dtreg[1] & 0x07) + 2);
#endif
}

static int i82975x_probe1(struct pci_dev *pdev, int dev_idx)
{
	int rc = -ENODEV;
	struct mem_ctl_info *mci;
	struct edac_mc_layer layers[2];
	struct i82975x_pvt tmp_pvt, *pvt;
	u32 mchbar;
	u32 drc[2];
	struct i82975x_error_info discard;

	edac_dbg(0, "\n");

	pci_read_config_dword(pdev, I82975X_MCHBAR, &mchbar);
	if (!(mchbar & 1)) {
		edac_dbg(3, "failed, MCHBAR disabled!\n");
		goto fail0;
	}
	mchbar &= 0xffffc000;	/* bits 31:14 used for 16K window */
	tmp_pvt.mch_window = ioremap_nocache(mchbar, 0x1000);
	if (!tmp_pvt.mch_window) {
		i82975x_printk(KERN_ERR, "Couldn't map MCHBAR registers.\n");
		rc = -ENOMEM;
		goto fail0;
	}

	drc[0] = readl(tmp_pvt.mch_window + I82975X_DRC_CH0M0);
	drc[1] = readl(tmp_pvt.mch_window + I82975X_DRC_CH1M0);

	detect_memory_style(&tmp_pvt);
	if (!tmp_pvt.num_channels) {
		edac_dbg(3, "No memories installed? This shouldn't be running!\n");
		goto fail0;
	}

	i82975x_print_dram_config(&tmp_pvt, mchbar, drc);

	if (!(((drc[0] >> 21) & 3) == 1 || ((drc[1] >> 21) & 3) == 1)) {
		i82975x_printk(KERN_INFO, "ECC disabled on both channels.\n");
		goto fail1;
	}

	/* assuming only one controller, index thus is 0 */
	layers[0].type = EDAC_MC_LAYER_CHIP_SELECT;
	layers[0].size = DIMMS_PER_CHANNEL;
	layers[0].is_virt_csrow = true;
	layers[1].type = EDAC_MC_LAYER_CHANNEL;
	layers[1].size = tmp_pvt.num_channels;
	layers[1].is_virt_csrow = false;
	mci = edac_mc_alloc(0, ARRAY_SIZE(layers), layers, sizeof(*pvt));
	if (!mci) {
		rc = -ENOMEM;
		goto fail1;
	}

	edac_dbg(3, "init mci\n");
	mci->pdev = &pdev->dev;
	mci->mtype_cap = MEM_FLAG_DDR2;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->mod_name = EDAC_MOD_STR;
	mci->mod_ver = I82975X_REVISION;
	mci->ctl_name = i82975x_devs[dev_idx].ctl_name;
	mci->dev_name = pci_name(pdev);
	mci->edac_check = i82975x_check;
	mci->ctl_page_to_phys = NULL;

	edac_dbg(3, "init pvt\n");
	pvt = (struct i82975x_pvt *) mci->pvt_info;
	*pvt = tmp_pvt;

	i82975x_init_csrows(mci, pvt, pdev);
	mci->scrub_mode = SCRUB_HW_SRC;
	i82975x_get_error_info(mci, &discard);  /* clear counters */

	/* finalize this instance of memory controller with edac core */
	if (edac_mc_add_mc(mci)) {
		edac_dbg(3, "failed edac_mc_add_mc()\n");
		goto fail2;
	}

	/* get this far and it's successful */
	edac_dbg(3, "success\n");
	return 0;

fail2:
	edac_mc_free(mci);

fail1:
	iounmap(tmp_pvt.mch_window);
fail0:
	return rc;
}

/* returns count (>= 0), or negative on error */
static int __devinit i82975x_init_one(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	int rc;

	edac_dbg(0, "\n");

	if (pci_enable_device(pdev) < 0)
		return -EIO;

	rc = i82975x_probe1(pdev, ent->driver_data);

	if (mci_pdev == NULL)
		mci_pdev = pci_dev_get(pdev);

	return rc;
}

static void __devexit i82975x_remove_one(struct pci_dev *pdev)
{
	struct mem_ctl_info *mci;
	struct i82975x_pvt *pvt;

	edac_dbg(0, "\n");

	mci = edac_mc_del_mc(&pdev->dev);
	if (mci  == NULL)
		return;

	pvt = mci->pvt_info;
	if (pvt->mch_window)
		iounmap( pvt->mch_window );

	edac_mc_free(mci);
}

static DEFINE_PCI_DEVICE_TABLE(i82975x_pci_tbl) = {
	{
		PCI_VEND_DEV(INTEL, 82975_0), PCI_ANY_ID, PCI_ANY_ID, 0, 0,
		I82975X
	},
	{
		0,
	}	/* 0 terminated list. */
};

MODULE_DEVICE_TABLE(pci, i82975x_pci_tbl);

static struct pci_driver i82975x_driver = {
	.name = EDAC_MOD_STR,
	.probe = i82975x_init_one,
	.remove = __devexit_p(i82975x_remove_one),
	.id_table = i82975x_pci_tbl,
};

static int __init i82975x_init(void)
{
	int pci_rc;

	edac_dbg(3, "\n");

       /* Ensure that the OPSTATE is set correctly for POLL or NMI */
       opstate_init();

	pci_rc = pci_register_driver(&i82975x_driver);
	if (pci_rc < 0)
		goto fail0;

	if (mci_pdev == NULL) {
		mci_pdev = pci_get_device(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_82975_0, NULL);

		if (!mci_pdev) {
			edac_dbg(0, "i82975x pci_get_device fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}

		pci_rc = i82975x_init_one(mci_pdev, i82975x_pci_tbl);

		if (pci_rc < 0) {
			edac_dbg(0, "i82975x init fail\n");
			pci_rc = -ENODEV;
			goto fail1;
		}
	}

	return 0;

fail1:
	pci_unregister_driver(&i82975x_driver);

fail0:
	if (mci_pdev != NULL)
		pci_dev_put(mci_pdev);

	return pci_rc;
}

static void __exit i82975x_exit(void)
{
	edac_dbg(3, "\n");

	pci_unregister_driver(&i82975x_driver);

	if (!i82975x_registered) {
		i82975x_remove_one(mci_pdev);
		pci_dev_put(mci_pdev);
	}
}

module_init(i82975x_init);
module_exit(i82975x_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Arvind R. <arvino55@gmail.com>");
MODULE_DESCRIPTION("MC support for Intel 82975 memory hub controllers");

module_param(edac_op_state, int, 0444);
MODULE_PARM_DESC(edac_op_state, "EDAC Error Reporting state: 0=Poll,1=NMI");
