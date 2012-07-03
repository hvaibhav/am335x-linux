/*
 * Generic simple device tree based pinctrl driver
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/list.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"

#define DRIVER_NAME			"pinctrl-simple"
#define PCS_MUX_NAME			"pinctrl-simple,cells"
#define PCS_MUX_CELLS			"#pinctrl-cells"
#define PCS_REG_NAME_LEN		((sizeof(unsigned long) * 2) + 1)

/**
 * struct pcs_pingroup - pingroups for a function
 * @np:		pingroup device node pointer
 * @name:	pingroup name
 * @gpins:	array of the pins in the group
 * @ngpins:	number of pins in the group
 */
struct pcs_pingroup {
	struct device_node *np;
	const char *name;
	int *gpins;
	int ngpins;
	struct list_head node;
};

/**
 * struct pcs_func_vals - mux function register offset and value pair
 * @reg:	register virtual address
 * @defval:	default value
 */
struct pcs_func_vals {
	void __iomem *reg;
	unsigned defval;
};

/**
 * struct pcs_function - pinctrl function
 * @name:	pinctrl function name
 * @vals:	register and vals array
 * @nvals:	number of entries in vals array
 * @pgnames:	array of pingroup names the function uses
 * @npgnames:	number of pingroup names the function uses
 * @node:	list node
 */
struct pcs_function {
	const char *name;
	struct pcs_func_vals *vals;
	unsigned nvals;
	const char **pgnames;
	int npgnames;
	struct list_head node;
};

/**
 * struct pcs_data - wrapper for data needed by pinctrl framework
 * @pa:		pindesc array
 * @cur:	index to current element
 */
struct pcs_data {
	struct pinctrl_pin_desc *pa;
	int cur;
};

/**
 * struct pcs_name - register name for a pin
 * @name:	name of the pinctrl register
 */
struct pcs_name {
	char name[PCS_REG_NAME_LEN];
};

/**
 * struct pcs_device - mux device instance
 * @res:	resources
 * @base:	virtual address of the controller
 * @size:	size of the ioremapped area
 * @dev:	device entry
 * @pctl:	pin controller device
 * @mutex:	mutex protecting the lists
 * @width:	bits per mux register
 * @fmask:	function register mask
 * @fshift:	function register shift
 * @foff:	value to turn mux off
 * @cmask:	pinconf mask
 * @fmax:	max number of functions in fmask
 * @cells:	width of the mux array
 * @names:	array of register names for pins
 * @pins:	physical pins on the SoC
 * @pgtree:	pingroup index radix tree
 * @ftree:	function index radix tree
 * @pingroups:	list of pingroups
 * @functions:	list of functions
 * @ngroups:	number of pingroups
 * @nfuncs:	number of functions
 * @desc:	pin controller descriptor
 * @read:	register read function to use
 * @write:	register write function to use
 */
struct pcs_device {
	struct resource *res;
	void __iomem *base;
	unsigned size;
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct mutex mutex;

	unsigned width;
	unsigned fmask;
	unsigned fshift;
	unsigned foff;
	unsigned cmask;
	unsigned fmax;
	unsigned cells;

	struct pcs_name *names;
	struct pcs_data pins;
	struct radix_tree_root pgtree;
	struct radix_tree_root ftree;

	struct list_head pingroups;
	struct list_head functions;

	unsigned ngroups;
	unsigned nfuncs;

	struct pinctrl_desc *desc;

	unsigned (*read)(void __iomem *reg);
	void (*write)(unsigned val, void __iomem *reg);
};

static unsigned __maybe_unused pcs_readb(void __iomem *reg)
{
	return readb(reg);
}

static unsigned __maybe_unused pcs_readw(void __iomem *reg)
{
	return readw(reg);
}

static unsigned __maybe_unused pcs_readl(void __iomem *reg)
{
	return readl(reg);
}

static void __maybe_unused pcs_writeb(unsigned val, void __iomem *reg)
{
	writeb(val, reg);
}

static void __maybe_unused pcs_writew(unsigned val, void __iomem *reg)
{
	writew(val, reg);
}

static void __maybe_unused pcs_writel(unsigned val, void __iomem *reg)
{
	writel(val, reg);
}

static int pcs_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pcs_device *pcs;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	return pcs->ngroups;
}

static const char *pcs_get_group_name(struct pinctrl_dev *pctldev,
					unsigned gselector)
{
	struct pcs_device *pcs;
	struct pcs_pingroup *group;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	group = radix_tree_lookup(&pcs->pgtree, gselector);
	if (!group) {
		dev_err(pcs->dev, "%s could not find pingroup%i\n",
			__func__, gselector);
		return NULL;
	}

	return group->name;
}

static int pcs_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned gselector,
					const unsigned **pins,
					unsigned *npins)
{
	struct pcs_device *pcs;
	struct pcs_pingroup *group;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	group = radix_tree_lookup(&pcs->pgtree, gselector);
	if (!group) {
		dev_err(pcs->dev, "%s could not find pingroup%i\n",
			__func__, gselector);
		return -EINVAL;
	}

	*pins = group->gpins;
	*npins = group->ngpins;

	return 0;
}

static void pcs_pin_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned offset)
{
	seq_printf(s, " " DRIVER_NAME);
}

static void pcs_dt_free_map(struct pinctrl_dev *pctldev,
				struct pinctrl_map *map, unsigned num_maps)
{
	struct pcs_device *pcs;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	devm_kfree(pcs->dev, map);
}

static int pcs_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np_config,
				struct pinctrl_map **map, unsigned *num_maps);

static struct pinctrl_ops pcs_pinctrl_ops = {
	.get_groups_count = pcs_get_groups_count,
	.get_group_name = pcs_get_group_name,
	.get_group_pins = pcs_get_group_pins,
	.pin_dbg_show = pcs_pin_dbg_show,
	.dt_node_to_map = pcs_dt_node_to_map,
	.dt_free_map = pcs_dt_free_map,
};

static int pcs_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct pcs_device *pcs;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	return pcs->nfuncs;
}

static const char *pcs_get_function_name(struct pinctrl_dev *pctldev,
						unsigned fselector)
{
	struct pcs_device *pcs;
	struct pcs_function *func;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func) {
		dev_err(pcs->dev, "%s could not find function%i\n",
			__func__, fselector);
		return NULL;
	}

	return func->name;
}

static int pcs_get_function_groups(struct pinctrl_dev *pctldev,
					unsigned fselector,
					const char * const **groups,
					unsigned * const ngroups)
{
	struct pcs_device *pcs;
	struct pcs_function *func;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func) {
		dev_err(pcs->dev, "%s could not find function%i\n",
			__func__, fselector);
		return -EINVAL;
	}
	*groups = func->pgnames;
	*ngroups = func->npgnames;

	return 0;
}

static int pcs_enable(struct pinctrl_dev *pctldev, unsigned fselector,
	unsigned group)
{
	struct pcs_device *pcs;
	struct pcs_function *func;
	int i;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func)
		return -EINVAL;

	dev_dbg(pcs->dev, "enabling function%i %s\n",
		fselector, func->name);

	for (i = 0; i < func->nvals; i++) {
		struct pcs_func_vals *vals;
		unsigned val;

		vals = &func->vals[i];
		val = pcs->read(vals->reg);
		val &= ~(pcs->cmask | pcs->fmask);
		val |= vals->defval;
		pcs->write(val, vals->reg);
	}

	return 0;
}

static void pcs_disable(struct pinctrl_dev *pctldev, unsigned fselector,
					unsigned group)
{
	struct pcs_device *pcs;
	struct pcs_function *func;
	int i;

	pcs = pinctrl_dev_get_drvdata(pctldev);
	func = radix_tree_lookup(&pcs->ftree, fselector);
	if (!func) {
		dev_err(pcs->dev, "%s could not find function%i\n",
			__func__, fselector);
		return;
	}

	/*
	 * Do not touch modes if off mode is larger than supported
	 * modes. Some hardware does not have clearly defined off modes.
	 */
	if ((pcs->foff << pcs->fshift) > pcs->fshift) {
		dev_dbg(pcs->dev, "not updating mode for disable\n");
		return;
	}

	dev_dbg(pcs->dev, "disabling function%i %s\n",
		fselector, func->name);

	for (i = 0; i < func->nvals; i++) {
		struct pcs_func_vals *vals;
		unsigned val;

		vals = &func->vals[i];
		val = pcs->read(vals->reg);
		val &= ~(pcs->cmask | pcs->fmask);
		val |= pcs->foff << pcs->fshift;
		pcs->write(val, vals->reg);
	}
}

static int pcs_request_gpio(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned offset)
{
	return -ENOTSUPP;
}

static struct pinmux_ops pcs_pinmux_ops = {
	.get_functions_count = pcs_get_functions_count,
	.get_function_name = pcs_get_function_name,
	.get_function_groups = pcs_get_function_groups,
	.enable = pcs_enable,
	.disable = pcs_disable,
	.gpio_request_enable = pcs_request_gpio,
};

static int pcs_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long *config)
{
	return -ENOTSUPP;
}

static int pcs_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned pin, unsigned long config)
{
	return -ENOTSUPP;
}

static int pcs_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long *config)
{
	return -ENOTSUPP;
}

static int pcs_pinconf_group_set(struct pinctrl_dev *pctldev,
				unsigned group, unsigned long config)
{
	return -ENOTSUPP;
}

static void pcs_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned offset)
{
}

static void pcs_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned selector)
{
}

static struct pinconf_ops pcs_pinconf_ops = {
	.pin_config_get = pcs_pinconf_get,
	.pin_config_set = pcs_pinconf_set,
	.pin_config_group_get = pcs_pinconf_group_get,
	.pin_config_group_set = pcs_pinconf_group_set,
	.pin_config_dbg_show = pcs_pinconf_dbg_show,
	.pin_config_group_dbg_show = pcs_pinconf_group_dbg_show,
};

/**
 * pcs_add_pin() - add a pin to the static per controller pin array
 * @pcs: pcs driver instance
 * @offset: register offset from base
 */
static int __devinit pcs_add_pin(struct pcs_device *pcs, unsigned offset)
{
	struct pinctrl_pin_desc *pin;
	struct pcs_name *pn;
	char *name;
	int i;

	i = pcs->pins.cur;
	if (i >= pcs->desc->npins) {
		dev_err(pcs->dev, "too many pins, max %i\n",
			pcs->desc->npins);
		return -ENOMEM;
	}

	pin = &pcs->pins.pa[i];
	pn = &pcs->names[i];
	name = pn->name;
	sprintf(name, "%lx",
		(unsigned long)pcs->res->start + offset);
	pin->name = name;
	pin->number = i;
	pcs->pins.cur++;

	return i;
}

/**
 * pcs_allocate_pin_table() - adds all the pins for the pinctrl driver
 * @pcs: pcs driver instance
 *
 * In case of errors, resources are freed in pcs_free_resources.
 *
 * If your hardware needs holes in the address space, then just set
 * up multiple driver instances.
 *
 * REVISIT: Handle the address space better for multiple registers
 * per pin type hardware.
 */
static int __devinit pcs_allocate_pin_table(struct pcs_device *pcs)
{
	int mux_bytes, nr_pins, i;

	mux_bytes = pcs->width / BITS_PER_BYTE;
	nr_pins = pcs->size / mux_bytes;

	dev_dbg(pcs->dev, "allocating %i pins\n", nr_pins);
	pcs->pins.pa = devm_kzalloc(pcs->dev,
				sizeof(*pcs->pins.pa) * nr_pins,
				GFP_KERNEL);
	if (!pcs->pins.pa)
		return -ENOMEM;

	pcs->names = devm_kzalloc(pcs->dev,
				sizeof(struct pcs_name) * nr_pins,
				GFP_KERNEL);
	if (!pcs->names)
		return -ENOMEM;

	pcs->desc->pins = pcs->pins.pa;
	pcs->desc->npins = nr_pins;

	for (i = 0; i < pcs->desc->npins; i++) {
		unsigned offset;
		int res;

		offset = i * mux_bytes;
		res = pcs_add_pin(pcs, offset);
		if (res < 0) {
			dev_err(pcs->dev, "error adding pins: %i\n", res);
			return res;
		}
	}

	return 0;
}

/**
 * pcs_add_function() - adds a new function to the function list
 * @pcs: pcs driver instance
 * @np: device node of the mux entry
 * @name: name of the function
 * @vals: array of mux register value pairs used by the function
 * @nvals: number of mux register value pairs
 * @pgnames: array of pingroup names for the function
 * @npgnames: number of pingroup names
 */
static struct pcs_function *pcs_add_function(struct pcs_device *pcs,
					struct device_node *np,
					const char *name,
					struct pcs_func_vals *vals,
					unsigned nvals,
					const char **pgnames,
					unsigned npgnames)
{
	struct pcs_function *function;

	function = devm_kzalloc(pcs->dev, sizeof(*function), GFP_KERNEL);
	if (!function)
		return NULL;

	function->name = name;
	function->vals = vals;
	function->nvals = nvals;
	function->pgnames = pgnames;
	function->npgnames = npgnames;

	mutex_lock(&pcs->mutex);
	list_add_tail(&function->node, &pcs->functions);
	radix_tree_insert(&pcs->ftree, pcs->nfuncs, function);
	pcs->nfuncs++;
	mutex_unlock(&pcs->mutex);

	return function;
}

static void pcs_remove_function(struct pcs_device *pcs,
				struct pcs_function *function)
{
	int i;

	mutex_lock(&pcs->mutex);
	for (i = 0; i < pcs->nfuncs; i++) {
		struct pcs_function *found;

		found = radix_tree_lookup(&pcs->ftree, i);
		if (found == function)
			radix_tree_delete(&pcs->ftree, i);
	}
	list_del(&function->node);
	mutex_unlock(&pcs->mutex);
}

/**
 * pcs_add_pingroup() - add a pingroup to the pingroup list
 * @pcs: pcs driver instance
 * @np: device node of the mux entry
 * @name: name of the pingroup
 * @gpins: array of the pins that belong to the group
 * @ngpins: number of pins in the group
 */
static int pcs_add_pingroup(struct pcs_device *pcs,
					struct device_node *np,
					const char *name,
					int *gpins,
					int ngpins)
{
	struct pcs_pingroup *pingroup;

	pingroup = devm_kzalloc(pcs->dev, sizeof(*pingroup), GFP_KERNEL);
	if (!pingroup)
		return -ENOMEM;

	pingroup->name = name;
	pingroup->np = np;
	pingroup->gpins = gpins;
	pingroup->ngpins = ngpins;

	mutex_lock(&pcs->mutex);
	list_add_tail(&pingroup->node, &pcs->pingroups);
	radix_tree_insert(&pcs->pgtree, pcs->ngroups, pingroup);
	pcs->ngroups++;
	mutex_unlock(&pcs->mutex);

	return 0;
}

/**
 * pcs_get_pin_by_offset() - get a pin index based on the register offset
 * @pcs: pcs driver instance
 * @offset: register offset from the base
 *
 * Note that this is OK as long as the pins are in a static array.
 */
static int pcs_get_pin_by_offset(struct pcs_device *pcs, unsigned offset)
{
	unsigned index;

	if (offset >= pcs->size) {
		dev_err(pcs->dev, "mux offset out of range: 0x%x (0x%x)\n",
			offset, pcs->size);
		return -EINVAL;
	}

	index = offset / (pcs->width / BITS_PER_BYTE);

	return index;
}

/**
 * smux_parse_one_pinctrl_entry() - parses a device tree mux entry
 * @pcs: pinctrl driver instance
 * @np: device node of the mux entry
 * @map: map entry
 * @pgnames: pingroup names
 *
 * Note that this currently supports only #pinctrl-cells = 2.
 * This could be improved to parse controllers that have additional
 * auxilary registers per mux.
 */
static int __init pcs_parse_one_pinctrl_entry(struct pcs_device *pcs,
						struct device_node *np,
						struct pinctrl_map **map,
						const char **pgnames)
{
	struct pcs_func_vals *vals;
	const __be32 *mux;
	int size, rows, *pins, index = 0, found = 0, res = -ENOMEM;
	struct pcs_function *function;

	if (pcs->cells != 2) {
		dev_err(pcs->dev, "unhandled %s: %i\n", PCS_MUX_CELLS,
			pcs->cells);
		return -EINVAL;
	}

	mux = of_get_property(np, PCS_MUX_NAME, &size);
	if ((!mux) || (size < sizeof(*mux) * pcs->cells)) {
		dev_err(pcs->dev, "bad data for mux %s\n",
			np->name);
		return -EINVAL;
	}

	size /= sizeof(*mux);	/* Number of elements in array */
	rows = size / pcs->cells;

	vals = devm_kzalloc(pcs->dev, sizeof(*vals) * rows, GFP_KERNEL);
	if (!vals)
		return -ENOMEM;

	pins = devm_kzalloc(pcs->dev, sizeof(*pins) * rows, GFP_KERNEL);
	if (!pins)
		goto free_vals;

	while (index < size) {
		unsigned offset, defval;
		int pin;

		offset = be32_to_cpup(mux + index++);
		defval = be32_to_cpup(mux + index++);
		vals[found].reg = pcs->base + offset;
		vals[found].defval = defval;

		pin = pcs_get_pin_by_offset(pcs, offset);
		if (pin < 0) {
			dev_err(pcs->dev,
				"could not add functions for %s %ux\n",
				np->name, offset);
			break;
		}
		pins[found++] = pin;
	}

	pgnames[0] = np->name;
	function = pcs_add_function(pcs, np, np->name, vals, found, pgnames, 1);
	if (!function)
		goto free_pins;

	res = pcs_add_pingroup(pcs, np, np->name, pins, found);
	if (res < 0)
		goto free_function;

	(*map)->type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)->data.mux.group = np->name;
	(*map)->data.mux.function = np->name;

	return 0;

free_function:
	pcs_remove_function(pcs, function);

free_pins:
	devm_kfree(pcs->dev, pins);

free_vals:
	devm_kfree(pcs->dev, vals);

	return res;
}
/**
 * pcs_dt_node_to_map() - allocates and parses pinctrl maps
 * @pctldev: pinctrl instance
 * @np_config: device tree pinmux entry
 * @map: array of map entries
 * @num_maps: number of maps
 */
static int pcs_dt_node_to_map(struct pinctrl_dev *pctldev,
				struct device_node *np_config,
				struct pinctrl_map **map, unsigned *num_maps)
{
	struct pcs_device *pcs;
	struct device_node *np;
	struct pinctrl_map *cur_map;
	const char **pgnames, **cur_name;
	int ret, found_maps = 0, added_maps = 0;

	pcs = pinctrl_dev_get_drvdata(pctldev);

	for_each_child_of_node(np_config, np)
		found_maps++;

	*map = devm_kzalloc(pcs->dev, sizeof(**map) * found_maps,
				GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	cur_map = *map;
	*num_maps = 0;

	pgnames = devm_kzalloc(pcs->dev, sizeof(*pgnames) * found_maps,
				GFP_KERNEL);
	if (!pgnames) {
		devm_kfree(pcs->dev, *map);
		return -ENOMEM;
	}

	cur_name = pgnames;

	for_each_child_of_node(np_config, np) {
		ret = pcs_parse_one_pinctrl_entry(pcs, np, &cur_map, cur_name);
		if (ret < 0) {
			dev_err(pcs->dev, "added only %i/%i entries for %s\n",
				added_maps, found_maps, np_config->name);
			break;
		}
		cur_map++;
		cur_name++;
		added_maps++;
	}

	*num_maps = added_maps;

	return 0;
}

/**
 * pcs_free_funcs() - free memory used by functions
 * @pcs: pcs driver instance
 */
static void pcs_free_funcs(struct pcs_device *pcs)
{
	struct list_head *pos, *tmp;
	int i;

	mutex_lock(&pcs->mutex);
	for (i = 0; i < pcs->nfuncs; i++) {
		struct pcs_function *func;

		func = radix_tree_lookup(&pcs->ftree, i);
		if (!func)
			continue;
		radix_tree_delete(&pcs->ftree, i);
	}
	list_for_each_safe(pos, tmp, &pcs->functions) {
		struct pcs_function *function;

		function = list_entry(pos, struct pcs_function, node);
		list_del(&function->node);
	}
	mutex_unlock(&pcs->mutex);
}

/**
 * pcs_free_pingroups() - free memory used by pingroups
 * @pcs: pcs driver instance
 */
static void pcs_free_pingroups(struct pcs_device *pcs)
{
	struct list_head *pos, *tmp;
	int i;

	mutex_lock(&pcs->mutex);
	for (i = 0; i < pcs->ngroups; i++) {
		struct pcs_pingroup *pingroup;

		pingroup = radix_tree_lookup(&pcs->pgtree, i);
		if (!pingroup)
			continue;
		radix_tree_delete(&pcs->pgtree, i);
	}
	list_for_each_safe(pos, tmp, &pcs->pingroups) {
		struct pcs_pingroup *pingroup;

		pingroup = list_entry(pos, struct pcs_pingroup, node);
		list_del(&pingroup->node);
	}
	mutex_unlock(&pcs->mutex);
}

/**
 * pcs_free_resources() - free memory used by this driver
 * @pcs: pcs driver instance
 */
static void pcs_free_resources(struct pcs_device *pcs)
{
	if (pcs->pctl)
		pinctrl_unregister(pcs->pctl);

	pcs_free_funcs(pcs);
	pcs_free_pingroups(pcs);
}

/**
 * pcs_register() - initializes and registers with pinctrl framework
 * @pcs: pcs driver instance
 */
static int __devinit pcs_register(struct pcs_device *pcs)
{
	int ret;

	if (!pcs->dev->of_node)
		return -ENODEV;

	pcs->desc = devm_kzalloc(pcs->dev, sizeof(*pcs->desc), GFP_KERNEL);
	if (!pcs->desc)
		return -ENOMEM;
	pcs->desc->name = DRIVER_NAME;
	pcs->desc->pctlops = &pcs_pinctrl_ops;
	pcs->desc->pmxops = &pcs_pinmux_ops;
	pcs->desc->confops = &pcs_pinconf_ops;
	pcs->desc->owner = THIS_MODULE;

	ret = pcs_allocate_pin_table(pcs);
	if (ret < 0)
		goto free;

	pcs->pctl = pinctrl_register(pcs->desc, pcs->dev, pcs);
	if (!pcs->pctl) {
		dev_err(pcs->dev, "could not register simple pinctrl driver\n");
		ret = -EINVAL;
		goto free;
	}

	dev_info(pcs->dev, "%i pins at pa %p size %u\n",
		 pcs->desc->npins, pcs->base, pcs->size);

	return 0;

free:
	pcs_free_resources(pcs);

	return ret;
}

#define PCS_GET_PROP_U32(name, reg, err)				\
	do {								\
		ret = of_property_read_u32(np, name, reg);		\
		if (ret) {						\
			dev_err(pcs->dev, err);				\
			goto out;					\
		}							\
	} while (0);

static struct of_device_id pcs_of_match[];

/*
 * REVISIT: Handle hardware with multiple registers per pin
 * better by adding other masks.
 */
static int __devinit pcs_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct resource res;
	struct pcs_device *pcs;
	int ret;

	match = of_match_device(pcs_of_match, &pdev->dev);
	if (!match)
		return -EINVAL;

	pcs = devm_kzalloc(&pdev->dev, sizeof(*pcs), GFP_KERNEL);
	if (!pcs) {
		dev_err(&pdev->dev, "could not allocate\n");
		return -ENOMEM;
	}
	pcs->dev = &pdev->dev;
	mutex_init(&pcs->mutex);
	INIT_LIST_HEAD(&pcs->pingroups);
	INIT_LIST_HEAD(&pcs->functions);

	PCS_GET_PROP_U32("pinctrl-simple,register-width", &pcs->width,
			 "register width not specified\n");

	PCS_GET_PROP_U32("pinctrl-simple,function-mask", &pcs->fmask,
			 "function register mask not specified\n");
	pcs->fshift = ffs(pcs->fmask) - 1;
	pcs->fmax = pcs->fmask >> pcs->fshift;

	PCS_GET_PROP_U32("pinctrl-simple,function-off", &pcs->foff,
			 "function off mode not specified\n");

	PCS_GET_PROP_U32("pinctrl-simple,pinconf-mask", &pcs->cmask,
			 "pinconf mask not specified\n");

	PCS_GET_PROP_U32("#pinctrl-cells", &pcs->cells,
			 "#pinctrl-cells not specified\n");

	ret = of_address_to_resource(pdev->dev.of_node, 0, &res);
	if (ret) {
		dev_err(pcs->dev, "could not get resource\n");
		goto out;
	}

	pcs->res = devm_request_mem_region(pcs->dev, res.start,
			resource_size(&res), DRIVER_NAME);
	if (!pcs->res) {
		dev_err(pcs->dev, "could not get mem_region\n");
		ret = -EBUSY;
		goto out;
	}

	pcs->size = resource_size(pcs->res);
	pcs->base = devm_ioremap(pcs->dev, pcs->res->start, pcs->size);
	if (!pcs->base) {
		dev_err(pcs->dev, "could not ioremap\n");
		ret = -ENODEV;
		goto out;
	}

	INIT_RADIX_TREE(&pcs->pgtree, GFP_KERNEL);
	INIT_RADIX_TREE(&pcs->ftree, GFP_KERNEL);
	platform_set_drvdata(pdev, pcs);

	switch (pcs->width) {
	case 8:
		pcs->read = pcs_readb;
		pcs->write = pcs_writeb;
		break;
	case 16:
		pcs->read = pcs_readw;
		pcs->write = pcs_writew;
		break;
	case 32:
		pcs->read = pcs_readl;
		pcs->write = pcs_writel;
		break;
	default:
		break;
	}

	ret = pcs_register(pcs);
	if (ret < 0) {
		dev_err(pcs->dev, "could not add mux registers: %i\n", ret);
		goto out;
	}

	return 0;

out:
	return ret;
}

static int __devexit pcs_remove(struct platform_device *pdev)
{
	struct pcs_device *pcs = platform_get_drvdata(pdev);

	if (!pcs)
		return 0;

	pcs_free_resources(pcs);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct of_device_id pcs_of_match[] __devinitdata = {
	{ .compatible = DRIVER_NAME, },
	{ .compatible = "ti,omap2420-padconf", },
	{ .compatible = "ti,omap2430-padconf", },
	{ .compatible = "ti,omap3-padconf", },
	{ .compatible = "ti,omap4-padconf", },
	{ },
};
MODULE_DEVICE_TABLE(of, pcs_of_match);

static struct platform_driver pcs_driver = {
	.probe		= pcs_probe,
	.remove		= __devexit_p(pcs_remove),
	.driver = {
		.owner		= THIS_MODULE,
		.name		= DRIVER_NAME,
		.of_match_table	= pcs_of_match,
	},
};

module_platform_driver(pcs_driver);

MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("Simple device tree pinctrl driver");
MODULE_LICENSE("GPL");
