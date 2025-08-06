// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/firmware.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/iommu.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/soc/qcom/mdt_loader.h>

#include "iris_core.h"
#include "iris_firmware.h"

#define IRIS_PAS_ID				9

#define MAX_FIRMWARE_NAME_SIZE	128

/* Detect Gen2 firmware by scanning the blob for:
 *   QC_IMAGE_VERSION_STRING=<version>
 * and then checking:
 *   - version starts with "vfw", OR
 *   - version matches "video-firmware.N.M" with N >= 2
 */

static bool iris_detect_gen2_from_fwdata(const u8 *data, size_t size)
{
	const char *marker = "QC_IMAGE_VERSION_STRING=";
	const size_t mlen = strlen(marker);
	int major = 0, minor = 0;
	char version_buf[64];
	size_t max;

	max = (size > mlen) ? size - mlen : 0;
	for (size_t i = 0; i < max; i++) {
		if (!memcmp(data + i, marker, mlen)) {
			const char *found = (const char *)(data + i + mlen);

			strscpy(version_buf, found, sizeof(version_buf));
			if (!strncmp(version_buf, "vfw", 3))
				return true;
			if (sscanf(version_buf, "video-firmware.%d.%d", &major, &minor) == 2 &&
			    major >= 2)
				return true;
			break;
		}
	}

	return false;
}

static const struct firmware *iris_detect_firmware(struct iris_core *core,
						   const char **fw_name)
{
	const struct firmware *firmware;
	bool has_both_gens;
	int ret;

	*fw_name = NULL;
	if (core->iris_platform_data->firmware_desc_gen2)
		core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen2;
	else if (core->iris_platform_data->firmware_desc_gen1)
		core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen1;
	else
		return ERR_PTR(-EINVAL);

	has_both_gens = core->iris_platform_data->firmware_desc_gen2 &&
		core->iris_platform_data->firmware_desc_gen1;

	ret = of_property_read_string_index(dev_of_node(core->dev), "firmware-name", 0, fw_name);
	if (ret) {
		*fw_name = core->iris_firmware_desc->fwname;
		ret = request_firmware(&firmware, *fw_name, core->dev);
		if (ret && has_both_gens) {
			core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen1;
			*fw_name = core->iris_firmware_desc->fwname;
			ret = request_firmware(&firmware, *fw_name, core->dev);
		}

		return ret ? ERR_PTR(ret) : firmware;
	}

	ret = request_firmware(&firmware, *fw_name, core->dev);
	if (ret)
		return ERR_PTR(ret);

	if (has_both_gens &&
	    !iris_detect_gen2_from_fwdata((const u8 *)firmware->data, firmware->size)) {
		dev_info(core->dev, "Gen1 FW detected in %s\n", *fw_name);
		core->iris_firmware_desc = core->iris_platform_data->firmware_desc_gen1;
	}

	return firmware;
}

#define WRAPPER_TZ_BASE_OFFS		0x000C0000

#define WRAPPER_TZ_XTSS_SW_RESET	(WRAPPER_TZ_BASE_OFFS + 0x1000)
#define WRAPPER_XTSS_SW_RESET_BIT	BIT(0)

#define WRAPPER_CPA_START_ADDR		(WRAPPER_TZ_BASE_OFFS + 0x1020)
#define WRAPPER_CPA_END_ADDR		(WRAPPER_TZ_BASE_OFFS + 0x1024)
#define WRAPPER_FW_START_ADDR		(WRAPPER_TZ_BASE_OFFS + 0x1028)
#define WRAPPER_FW_END_ADDR		(WRAPPER_TZ_BASE_OFFS + 0x102C)
#define WRAPPER_NONPIX_START_ADDR	(WRAPPER_TZ_BASE_OFFS + 0x1030)
#define WRAPPER_NONPIX_END_ADDR		(WRAPPER_TZ_BASE_OFFS + 0x1034)

#define IRIS_FW_START_ADDR		0x0

static void iris_reset_cpu_no_tz(struct iris_core *core)
{
	u32 fw_size = core->fw.mapped_mem_size;

	writel(0, core->reg_base + WRAPPER_FW_START_ADDR);
	writel(fw_size, core->reg_base + WRAPPER_FW_END_ADDR);
	writel(0, core->reg_base + WRAPPER_CPA_START_ADDR);
	writel(fw_size, core->reg_base + WRAPPER_CPA_END_ADDR);
	writel(fw_size, core->reg_base + WRAPPER_NONPIX_START_ADDR);
	writel(fw_size, core->reg_base + WRAPPER_NONPIX_END_ADDR);

	/* Bring XTSS out of reset */
	writel(0, core->reg_base + WRAPPER_TZ_XTSS_SW_RESET);
}

static int iris_set_hw_state_no_tz(struct iris_core *core, bool resume)
{
	if (resume)
		iris_reset_cpu_no_tz(core);
	else
		writel(WRAPPER_XTSS_SW_RESET_BIT, core->reg_base + WRAPPER_TZ_XTSS_SW_RESET);

	return 0;
}

static int iris_load_fw_to_memory(struct iris_core *core)
{
	const struct firmware *firmware = NULL;
	struct device *dev = core->dev;
	struct resource res;
	phys_addr_t mem_phys;
	const char *fw_name;
	size_t res_size;
	ssize_t fw_size;
	void *mem_virt;
	int ret;

	ret = of_reserved_mem_region_to_resource(dev->of_node, 0, &res);
	if (ret)
		return ret;

	mem_phys = res.start;
	res_size = resource_size(&res);

	firmware = iris_detect_firmware(core, &fw_name);
	if (IS_ERR(firmware))
		return PTR_ERR(firmware);

	core->iris_firmware_data = core->iris_firmware_desc->firmware_data;

	fw_size = qcom_mdt_get_size(firmware);
	if (fw_size < 0 || res_size < (size_t)fw_size) {
		ret = -EINVAL;
		goto err_release_fw;
	}

	mem_virt = memremap(mem_phys, res_size, MEMREMAP_WC);
	if (!mem_virt) {
		ret = -ENOMEM;
		goto err_release_fw;
	}

	if (core->use_tz)
		ret = qcom_mdt_load(dev, firmware, fw_name,
				    IRIS_PAS_ID, mem_virt, mem_phys, res_size, NULL);
	else
		ret = qcom_mdt_load_no_init(dev, firmware, fw_name,
					    mem_virt, mem_phys, res_size, NULL);

	/* Unmap memory on our side before we give firmware access to it */
	memunmap(mem_virt);
	if (ret)
		goto err_release_fw;

	if (core->fw.iommu_domain) {
		ret = iommu_map(core->fw.iommu_domain, IRIS_FW_START_ADDR, mem_phys, res_size,
				IOMMU_READ | IOMMU_WRITE | IOMMU_PRIV, GFP_KERNEL);
		if (ret) {
			dev_err(core->dev, "failed to map firmware in IOMMU: %d\n", ret);
			goto err_release_fw;
		}

		core->fw.mapped_mem_size = res_size;
	}

err_release_fw:
	release_firmware(firmware);

	return ret;
}

int iris_fw_load(struct iris_core *core)
{
	const struct tz_cp_config *cp_config;
	int i, ret;

	ret = iris_load_fw_to_memory(core);
	if (ret) {
		dev_err(core->dev, "firmware download failed %d\n", ret);
		return ret;
	}

	if (core->use_tz) {
		ret = qcom_scm_pas_auth_and_reset(IRIS_PAS_ID);
		if (ret)  {
			dev_err(core->dev, "auth and reset failed: %d\n", ret);
			return ret;
		}

		for (i = 0; i < core->iris_platform_data->tz_cp_config_data_size; i++) {
			cp_config = &core->iris_platform_data->tz_cp_config_data[i];
			ret = qcom_scm_mem_protect_video_var(cp_config->cp_start,
							     cp_config->cp_size,
							     cp_config->cp_nonpixel_start,
							     cp_config->cp_nonpixel_size);
			if (ret) {
				dev_err(core->dev,
					"qcom_scm_mem_protect_video_var failed: %d\n", ret);
				qcom_scm_pas_shutdown(IRIS_PAS_ID);
				return ret;
			}
		}
	} else {
		iris_reset_cpu_no_tz(core);
	}

	return 0;
}

int iris_fw_unload(struct iris_core *core)
{
	int ret;

	if (core->use_tz)
		ret = qcom_scm_pas_shutdown(IRIS_PAS_ID);
	else
		ret = iris_set_hw_state_no_tz(core, false);

	if (core->fw.mapped_mem_size) {
		size_t unmapped = iommu_unmap(core->fw.iommu_domain, IRIS_FW_START_ADDR,
					      core->fw.mapped_mem_size);

		if (unmapped != core->fw.mapped_mem_size)
			dev_err(core->dev, "failed to unmap firmware in IOMMU\n");

		core->fw.mapped_mem_size = 0;
	}

	return ret;
}

int iris_set_hw_state(struct iris_core *core, bool resume)
{
	if (core->use_tz)
		return qcom_scm_set_remote_state(resume, 0);
	else
		return iris_set_hw_state_no_tz(core, resume);
}

int iris_fw_init(struct iris_core *core)
{
	struct iommu_domain *iommu_domain;
	struct platform_device_info info;
	struct platform_device *pdev;
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(core->dev->of_node, "video-firmware");
	if (!np) {
		core->use_tz = true;
		return 0;
	}

	memset(&info, 0, sizeof(info));
	info.parent = core->dev;
	info.fwnode = &np->fwnode;
	info.name = np->name;
	info.dma_mask = DMA_BIT_MASK(32);

	pdev = platform_device_register_full(&info);
	if (IS_ERR(pdev)) {
		of_node_put(np);
		return PTR_ERR(pdev);
	}

	ret = of_dma_configure(&pdev->dev, np, true);
	of_node_put(np);
	if (ret) {
		dev_err(&pdev->dev, "failed to configure dma: %d\n", ret);
		goto err_unregister;
	}

	iommu_domain = iommu_paging_domain_alloc(&pdev->dev);
	if (IS_ERR(iommu_domain)) {
		dev_err(&pdev->dev, "failed to allocate iommu domain: %pe\n", iommu_domain);
		ret = PTR_ERR(iommu_domain);
		goto err_unregister;
	}

	ret = iommu_attach_device(iommu_domain, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "could not attach device to IOMMU: %d\n", ret);
		goto err_iommu_free;
	}

	core->fw.pdev = pdev;
	core->fw.iommu_domain = iommu_domain;

	return 0;

err_iommu_free:
	iommu_domain_free(iommu_domain);
err_unregister:
	platform_device_unregister(pdev);
	return ret;
}

void iris_fw_deinit(struct iris_core *core)
{
	if (!core->fw.iommu_domain)
		return;

	iommu_detach_device(core->fw.iommu_domain, &core->fw.pdev->dev);
	iommu_domain_free(core->fw.iommu_domain);
	platform_device_unregister(core->fw.pdev);
}
