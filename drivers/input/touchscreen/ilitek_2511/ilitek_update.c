// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#include "ilitek_update.h"

#ifndef __KERNEL__
static int hex_to_bin(uint8_t ch)
{
	uint8_t cu = ch & 0xdf;
	return -1 +
		((ch - '0' +  1) & (unsigned)((ch - '9' - 1) &
		('0' - 1 - ch)) >> 8) +
		((cu - 'A' + 11) & (unsigned)((cu - 'F' - 1) &
		('A' - 1 - cu)) >> 8);
}

static int hex2bin(uint8_t *dst, const uint8_t *src, size_t count)
{
	int hi = 0, lo = 0;

	while (count--) {
		if ((hi = hex_to_bin(*src++)) < 0 ||
		    (lo = hex_to_bin(*src++)) < 0) {
			TP_ERR(NULL, "hex_to_bin failed, hi: %d, lo: %d\n",
				hi, lo);
			return -EINVAL;
		}

		*dst++ = (hi << 4) | lo;
	}
	return 0;
}
#endif

static uint32_t get_tag_addr(uint32_t start, uint32_t end,
			     const uint8_t *buf, unsigned int buf_size,
			     const uint8_t *tag, unsigned int tag_size)
{
	unsigned int i;

	for (i = start; i <= end - tag_size && i < buf_size - tag_size; i++) {
		if (!memcmp(buf + i, tag, tag_size))
			return i + tag_size + 1;
	}

	return end;
}

static uint32_t get_endaddr(uint32_t start, uint32_t end, const uint8_t *buf,
			    unsigned int buf_size, bool is_AP)
{
	uint32_t addr;
	uint8_t tag[32];
	const uint8_t ap_tag[] = "ILITek AP CRC   ";
	const uint8_t blk_tag[] = "ILITek END TAG  ";

	memset(tag, 0xFF, sizeof(tag));
	memcpy(tag + 16, (is_AP) ? ap_tag : blk_tag, 16);

	addr = get_tag_addr(start, end, buf, buf_size, tag, sizeof(tag));
	TP_DBG(NULL, "find tag in start/end: 0x%x/0x%x, tag addr: 0x%x\n",
		start, end, addr);

	return addr;
}

static int decode_mm(struct ilitek_fw_handle *fw, uint32_t addr,
		      uint8_t *buf, uint32_t buf_size)
{
	uint8_t i;
	union mapping_info *mapping;

	PACKED(struct ap_block_info {
		uint8_t year;
		uint8_t month;
		uint8_t day;
		uint8_t del;
		char name[10];
	}) *info;
	uint8_t tmp[14];

	memset(tmp, 0xFF, sizeof(tmp));
	if (sizeof(tmp) != sizeof(*info)) {
		TP_ERR(NULL, "unexpected packed size %d vs. %d\n",
			(int)sizeof(tmp), (int)sizeof(*info));
		return -EINVAL;
	}

	TP_INFO(NULL, "------------Memory Mapping information------------\n");
	TP_INFO(NULL, "memory-mapping-info addr: %#x\n", addr);

	mapping = (union mapping_info *)(buf + addr);
	memset(fw->file.ic_name, 0, sizeof(fw->file.ic_name));

	_sprintf(fw->file.ic_name, 0, "%02x%02x",
		mapping->ic_name[1], mapping->ic_name[0]);

	if (!strcmp(fw->file.ic_name, "2133"))
		_sprintf(fw->file.ic_name, 0, "2132S");

	if (fw->dev && strcmp(fw->dev->mcu_info.ic_name, fw->file.ic_name)) {
		TP_ERR(fw->dev->id, "IC: ILI%s, Firmware File: ILI%s not matched\n",
			fw->dev->mcu_info.ic_name, fw->file.ic_name);
		return -EINVAL;
	}

	TP_MSG(NULL, "Hex Mapping Ver.: 0x%x\n",
		le32(mapping->mapping_ver, 3));
	TP_MSG(NULL, "Hex Protocol: 0x%x\n",
		le32(mapping->protocol_ver, 3));
	TP_MSG(NULL, "Hex MCU Ver.: %s\n", fw->file.ic_name);

	memset(fw->file.fw_ver, 0, sizeof(fw->file.fw_ver));

	fw->file.mm_addr = addr;
	switch (addr) {
	case 0x4020:
		fw->file.mm_size = 128;
		fw->file.fw_ver[0] = mapping->_lego.fw_ver[3];
		fw->file.fw_ver[1] = mapping->_lego.fw_ver[2];
		fw->file.fw_ver[2] = mapping->_lego.fw_ver[1];
		fw->file.fw_ver[3] = mapping->_lego.fw_ver[0];
		fw->file.fw_ver[4] = buf[0x2C007];
		fw->file.fw_ver[5] = buf[0x2C006];
		fw->file.fw_ver[6] = buf[0x2C005];
		fw->file.fw_ver[7] = buf[0x2C004];
		break;
	case 0x3020:
		fw->file.mm_size = 128;
		fw->file.fw_ver[0] = mapping->_lego.fw_ver[3];
		fw->file.fw_ver[1] = mapping->_lego.fw_ver[2];
		fw->file.fw_ver[2] = mapping->_lego.fw_ver[1];
		fw->file.fw_ver[3] = mapping->_lego.fw_ver[0];
		fw->file.fw_ver[4] = buf[0x3C007];
		fw->file.fw_ver[5] = buf[0x3C006];
		fw->file.fw_ver[6] = buf[0x3C005];
		fw->file.fw_ver[7] = buf[0x3C004];
		break;
	case 0x2020:
		fw->file.mm_size = 132;
		fw->file.fw_ver[0] = buf[0x2033];
		fw->file.fw_ver[1] = buf[0x2032];
		fw->file.fw_ver[2] = buf[0x2031];
		fw->file.fw_ver[3] = buf[0x2030];
		fw->file.fw_ver[4] = buf[0xF004];
		fw->file.fw_ver[5] = buf[0xF005];
		fw->file.fw_ver[6] = buf[0xF006];
		fw->file.fw_ver[7] = buf[0xF007];

		/* for V3 251x IC, get AP crc and DF checksum */
		fw->file.blocks[0].check = get_crc(fw->file.blocks[0].start,
			fw->file.blocks[0].end - 1, buf, buf_size);

		if (fw->file.blocks[1].end > fw->file.blocks[1].start) {
			fw->file.blocks[1].check = get_checksum(
				fw->file.blocks[1].start,
				fw->file.blocks[1].end + 1, buf, buf_size);
		}
		break;

	case 0x500:
		fw->file.mm_size = 132;
		fw->file.fw_ver[0] = buf[0x52D];
		fw->file.fw_ver[1] = buf[0x52C];
		fw->file.fw_ver[2] = buf[0x52B];
		fw->file.fw_ver[3] = buf[0x52A];
		fw->file.fw_ver[4] = buf[0x1F404];
		fw->file.fw_ver[5] = buf[0x1F405];
		fw->file.fw_ver[6] = buf[0x1F406];
		fw->file.fw_ver[7] = buf[0x1F407];

		/* for V3 231x IC, get AP checksum and DF checksum */
		fw->file.blocks[0].check = get_checksum(
			fw->file.blocks[0].start,
			fw->file.blocks[0].end + 1, buf, buf_size);
		if (fw->file.blocks[1].end > fw->file.blocks[1].start) {
			fw->file.blocks[1].check = get_checksum(
				fw->file.blocks[1].start,
				fw->file.blocks[1].end + 1, buf, buf_size);
		}
		break;
	default:
		fw->file.mm_size = 0;
		break;
	}

	TP_INFO(NULL, "File FW Version: %02x-%02x-%02x-%02x\n",
		fw->file.fw_ver[0], fw->file.fw_ver[1],
		fw->file.fw_ver[2], fw->file.fw_ver[3]);
	TP_INFO(NULL, "File Customer Version: %02x-%02x-%02x-%02x\n",
		fw->file.fw_ver[4], fw->file.fw_ver[5],
		fw->file.fw_ver[6], fw->file.fw_ver[7]);

	if (le32(mapping->mapping_ver, 3) < 0x10000)
		goto memory_mapping_end;

	TP_INFO(NULL, "File Tuning Version: %02x-%02x-%02x-%02x\n",
		mapping->_lego.tuning_ver[3], mapping->_lego.tuning_ver[2],
		mapping->_lego.tuning_ver[1], mapping->_lego.tuning_ver[0]);

	if (mapping->_lego.block_num > ARRAY_SIZE(fw->file.blocks)) {
		TP_ERR(NULL, "Unexpected block num: %hhu > %u\n",
			mapping->_lego.block_num,
			(unsigned int)ARRAY_SIZE(fw->file.blocks));
		goto memory_mapping_end;
	}

	fw->file.block_num = mapping->_lego.block_num;

	TP_MSG(NULL, "Total %hhu blocks\n", fw->file.block_num);
	for (i = 0; i < fw->file.block_num; i++) {
		fw->file.blocks[i].start =
			le32(mapping->_lego.blocks[i].addr, 3);
		fw->file.blocks[i].end = (i == fw->file.block_num - 1) ?
			le32(mapping->_lego.end_addr, 3) :
			le32(mapping->_lego.blocks[i + 1].addr, 3);

		/*
		 * get end addr. of block,
		 * i.e. address of block's final byte of crc.
		 */
		fw->file.blocks[i].end = get_endaddr(
			fw->file.blocks[i].start, fw->file.blocks[i].end,
			buf, buf_size, i == 0);

		fw->file.blocks[i].check = get_crc(fw->file.blocks[i].start,
			fw->file.blocks[i].end - 1,
			buf, buf_size);

		/* add end add. to include info tag. */
		if (i == 0) {
			info = (struct ap_block_info *)
				(buf + fw->file.blocks[0].end + 1);
			fw->file.blocks[0].end += sizeof(*info);

			if (memcmp(tmp, (uint8_t *)info, sizeof(tmp)))
				TP_MSG(NULL, "ap-tag: %d/%hhu/%hhu by %s\n",
					2000 + info->year, info->month,
					info->day, info->name);
		}

		TP_MSG(NULL, "Block[%u], start:%#x end:%#x, crc:%#x\n",
			i, fw->file.blocks[i].start, fw->file.blocks[i].end,
			fw->file.blocks[i].check);
	}

memory_mapping_end:
	TP_INFO(NULL, "--------------------------------------------------\n");

	return 0;
}

static int decode_hex(struct ilitek_fw_handle *fw, uint8_t *hex,
		      uint32_t start, uint32_t end,
		      uint8_t *buf, uint32_t buf_size)
{
	int error;
	uint8_t info[4], data[16];
	unsigned int i, len, addr, type, exaddr = 0;
	uint32_t mapping_info_addr = 0;

	/* m2v hex has another block at the end of hex file */
	uint8_t j = (fw->m2v) ? fw->file.block_num : 0;

	fw->file.blocks[j].start = (~0U);
	fw->file.blocks[j].end = 0x0;
	fw->file.blocks[j].check = 0x0;
	fw->file.blocks[j + 1].start = (~0U);
	fw->file.blocks[j + 1].end = 0x0;
	fw->file.blocks[j + 1].check = 0x0;

	for (i = start; i < end; i++) {
		/* filter out non-hexadecimal characters */
		if (hex_to_bin(hex[i]) < 0)
			continue;

		if ((error = hex2bin(info, hex + i, sizeof(info))) < 0)
			return error;

		len = info[0];
		addr = be32(info + 1, 2);
		type = info[3];

		if ((error = hex2bin(data, hex + i + 8, len)) < 0)
			return error;

		switch (type) {
		case 0xAC:
			mapping_info_addr = be32(data, len);
			break;

		case 0xAD:
			fw->file.blocks[1].start = be32(data, len);
			memset(buf + fw->file.blocks[1].start, 0, 0x1000);
			break;

		case 0xBA:
			if (be32(data, len) != 2U)
				break;

			TP_MSG(NULL, "start to decode M2V part of hex file\n");
			fw->m2v = true;
			////Reed Add : 20230721（解析完2326部分先跑Decode_mm,再继续解析M2V部分。）
			if (mapping_info_addr)
				decode_mm(fw, mapping_info_addr, buf, buf_size);
			return decode_hex(fw, hex, i + 10 + len * 2 + 1, end,
					  fw->m2v_buf, ILITEK_FW_BUF_SIZE);

		case 0x01:
			goto success_return;

		case 0x02:
			exaddr = be32(data, len) << 4;
			break;

		case 0x04:
			exaddr = be32(data, len) << 16;
			break;

		case 0x05:
			TP_MSG(NULL, "hex data type: %#x, start linear address: %#x\n",
				type, be32(data, len));
			break;

		case 0x00:
			addr += exaddr;

			if (addr + len > buf_size) {
				TP_ERR(NULL, "hex addr: %#x, buf size: %#x OOB\n",
					addr + len, buf_size);
				return -ENOBUFS;
			}
			memcpy(buf + addr, data, len);

			fw->file.blocks[j].start =
				MIN(fw->file.blocks[j].start, addr);

			if (addr + len < fw->file.blocks[j + 1].start) {
				fw->file.blocks[j].end =
					MAX(fw->file.blocks[j].end,
					    addr + len - 1);
				fw->file.blocks[j].check += get_checksum(
					0, len, data, sizeof(data));
			} else {
				fw->file.blocks[j + 1].end =
					MAX(fw->file.blocks[j + 1].end,
					    addr + len - 1);
				fw->file.blocks[j + 1].check += get_checksum(
					0, len, data, sizeof(data));
			}

			break;
		default:
			TP_ERR(NULL, "unexpected type:%#x in hex, len:%u, addr:%#x\n",
				type, len, addr);
			return -EINVAL;
		}

		i = i + 10 + len * 2;
	}

success_return:
	if (fw->m2v)
		fw->m2v_checksum = fw->file.blocks[fw->file.block_num].check;
	if (mapping_info_addr)
		return decode_mm(fw, mapping_info_addr, fw->file.buf, buf_size);

	return 0;
}

static int decode_bin(struct ilitek_fw_handle *fw,
		      uint8_t *bin, uint32_t bin_size,
		      uint8_t *buf, uint32_t buf_size)
{
	int error;
	struct ilitek_ts_device *dev = fw->dev;
	uint32_t mapping_info_addr;

	if (!dev) {
		TP_ERR(NULL, "offline decode bin file is not supported\n");
		return -EINVAL;
	}

	if (bin_size > buf_size) {
		TP_ERR(dev->id, "bin file size: %#x, buf size: %#x OOB\n",
			bin_size, buf_size);
		return -ENOBUFS;
	}
	memcpy(buf, bin, bin_size);

	if ((error = api_protocol_set_cmd(dev, GET_PTL_VER, NULL)) < 0 ||
	    (error = api_protocol_set_cmd(dev, GET_MCU_VER, NULL)) < 0)
		return error;

	switch (dev->protocol.flag) {
	case PTL_V6:
		mapping_info_addr = dev->mcu_info.mm_addr;
		break;

	case PTL_V3:
		/*
		 * For 231x: AP checksum and DF checksum, DF start addr: 0x1f000
		 * For 251x: AP crc and DF checksum, DF start addr: 0xf000
		 */
		if (is_231x(dev)) {
			mapping_info_addr = 0x500;

			fw->file.blocks[1].start = 0x1f000;
			fw->file.blocks[1].end = bin_size - 1;
			fw->file.blocks[0].start = 0x0;
			fw->file.blocks[0].end =
				get_endaddr(fw->file.blocks[0].start,
					    fw->file.blocks[1].start,
					    bin, bin_size, true) + 2;
		} else {
			mapping_info_addr = 0x2020;
			fw->file.blocks[1].start = 0xf000;
			fw->file.blocks[1].end = bin_size - 1;
			fw->file.blocks[0].start = 0x2000;
			fw->file.blocks[0].end =
				get_endaddr(fw->file.blocks[0].start,
					    fw->file.blocks[1].start,
					    bin, bin_size, true);
		}

		break;

	default:
		return -EINVAL;
	}

	/*
	 * take the whole "buf" into decode_mm, "buf" should be
	 * properly initialized, and the size should be
	 * larger than "bin", which reduce OOB issue.
	 */
	return decode_mm(fw, mapping_info_addr, buf, buf_size);
}

#ifdef ILITEK_BOOT_UPDATE
#include "ilitek_fw.h"

static int decode_ili(struct ilitek_fw_handle *fw,
	uint8_t *buf, uint32_t buf_size)
{
	struct ilitek_ts_device *dev = fw->dev;
	uint8_t *ili_buf = CTPM_FW;
	int size = sizeof(CTPM_FW);
	uint8_t id;

#if defined(ILITEK_BOOT_UPDATE_ILI_VER)
	switch (__ili_select_type__) {
	case ili_by_sensor_id:
		if (!support_sensor_id(dev)) {
			TP_WARN(dev->id, "protocol: 0x%x, mode: 0x%hhx, "
				"sensor-id not supported, "
				"take default fw(id: 0x%x)\n",
				dev->protocol.ver, dev->ic[0].mode,
				ILITEK_BOOT_UPDATE_DEF_ID);
			fw->file.id = ILITEK_BOOT_UPDATE_DEF_ID;
			break;
		}

		id = dev->sensor.id & dev->setting.sensor_id_mask;
		if (id >= ARRAY_SIZE(ili_arr)) {
			TP_ERR(dev->id, "invalid sensor id: %hhu >= %d\n",
				dev->sensor.id, (int)ARRAY_SIZE(ili_arr));
			return -EINVAL;
		}

		fw->file.id = id;
		ili_buf = ili_arr[id].buf;
		size = ili_arr[id].size;
		break;
	}
#endif

	if (!ili_buf || size < 32)
		return -EINVAL;

	fw->setting.fw_ver_check = true;
	memcpy(fw->setting.fw_ver, ili_buf + 18, 8);

	TP_MSG_ARR(dev->id, "IC  fw ver:", TYPE_U8, 8, dev->fw_ver);
	TP_MSG_ARR(dev->id, "Hex fw ver:", TYPE_U8, 8, fw->setting.fw_ver);

	return decode_bin(fw, ili_buf + 32, size - 32, buf, buf_size);
}
#endif

static bool need_retry(struct ilitek_fw_handle *fw)
{
	struct ilitek_ts_device *dev = fw->dev;
	uint32_t id;

#if defined(ILITEK_BOOT_UPDATE) && defined(ILITEK_BOOT_UPDATE_ILI_VER)
	if (fw->file.type != fw_ili)
		return false;

	switch (__ili_select_type__) {
	case ili_by_sensor_id:
		id = dev->sensor.id & dev->setting.sensor_id_mask;
		if (id == fw->file.id)
			break;

		/* reload correct fw if sensor-id not matched with default fw */
		TP_MSG(dev->id, "sensor id: %#x, file: %#x not matched, "
			"reload fw again\n",
			id, fw->file.id);

		if (ilitek_update_load_fw(fw, "ilitek.ili") < 0) {
			TP_ERR(dev->id, "reload ilitek.ili failed\n");
			return false;
		}

		/* disable fw ver check for the second try */
		fw->setting.fw_ver_check = false;

		return true;
	}
#endif

	UNUSED(dev);
	UNUSED(id);

	return false;
}

static int decode_firmware(struct ilitek_fw_handle *fw, char *file_name)
{
	int error;
	char *file_ext;
	int file_size = 0;
	uint8_t *file_buf;

	/* initialization */
	memset(fw->file.buf, 0xFF, fw->file.buf_size);
	memset(fw->m2v_buf, 0xFF, ILITEK_FW_BUF_SIZE);
	fw->m2v = false;  ////Reed Add : 20230721

	/*
	 * set block num 2 for V3 AP and Data Flash as default,
	 * for V6, block num would be updated after decoding memory mapping.
	 */
	fw->file.block_num = 2;
	fw->file.blocks[0].start = (~0U);
	fw->file.blocks[0].end = 0x0;
	fw->file.blocks[0].check = 0x0;
	fw->file.blocks[1].start = (~0U);
	fw->file.blocks[1].end = 0x0;
	fw->file.blocks[1].check = 0x0;

	if (!(file_ext = strrchr(file_name, '.')))
		return -ENOENT;

	file_buf = (uint8_t *)CALLOC(ILITEK_FW_FILE_SIZE, 1);
	if (!file_buf)
		return -ENOMEM;

	/* no need to read .ili file */
	if (_strcasecmp(file_ext, ".ili")) {
		TP_MSG(NULL, "start to load file: %s\n", file_name);
		file_size = fw->cb.read_fw(file_name, file_buf,
					   ILITEK_FW_FILE_SIZE, fw->_private);

		if ((error = file_size) < 0) {
			TP_ERR(NULL, "read fw file failed, err: %d\n", error);
			goto err_free;
		}
	}

	_strcpy(fw->fw_name, file_name, sizeof(fw->fw_name));

	if (!_strcasecmp(file_ext, ".hex")) {
		fw->file.type = fw_hex;
		error = decode_hex(fw, file_buf, 0, file_size,
			fw->file.buf, fw->file.buf_size);
	} else if (!_strcasecmp(file_ext, ".bin")) {
		fw->file.type = fw_bin;
		error = decode_bin(fw, file_buf, file_size,
				   fw->file.buf, fw->file.buf_size);
	}
#ifdef ILITEK_BOOT_UPDATE
	else if (!_strcasecmp(file_ext, ".ili")) {
		fw->file.type = fw_ili;
		error = decode_ili(fw, fw->file.buf, fw->file.buf_size);
	}
#endif
	else {
		error = -EINVAL;
	}

err_free:
	CFREE(file_buf);

	return error;
}

static bool need_fw_update_v3(struct ilitek_fw_handle *fw)
{
	struct ilitek_ts_device *dev = fw->dev;
	bool fw_file_has_data_flash =
		(fw->file.blocks[1].start < fw->file.blocks[1].end);
	bool need = false;

	TP_INFO(dev->id, "------------V3 AP/DF Info.------------\n");

	fw->file.blocks[0].check_match = (fw->setting.force_update) ?
		false : dev->ic[0].crc[0] == fw->file.blocks[0].check;

	TP_INFO(dev->id, "AP block Start/End Addr.: %#x/%#x, IC/File Checksum: %#x/%#x %s\n",
		fw->file.blocks[0].start, fw->file.blocks[0].end,
		dev->ic[0].crc[0], fw->file.blocks[0].check,
		(fw->file.blocks[0].check_match) ? "matched" : "not matched");

	fw->file.blocks[1].check_match = true;
	if (fw_file_has_data_flash) {
		fw->file.blocks[1].check_match = (fw->setting.force_update) ?
			false : dev->ic[0].crc[1] == fw->file.blocks[1].check;

		TP_INFO(dev->id, "DF block Start/End Addr.: %#x/%#x, IC/File Checksum: %#x/%#x %s\n",
			fw->file.blocks[1].start, fw->file.blocks[1].end,
			dev->ic[0].crc[1], fw->file.blocks[1].check,
			(fw->file.blocks[1].check_match) ?
			"matched" : "not matched");
	} else if (!is_231x(dev)) {
		/*
		 * for 251x ICs, if no data flash in fw file,
		 * need to switch to BL mode then erase data flash forcely.
		 */
		need = true;
	}

	TP_INFO(dev->id, "--------------------------------------\n");

	need |= (!fw->file.blocks[0].check_match ||
		 !fw->file.blocks[1].check_match);

	if (dev->ic[0].mode == 0x55)
		return true;

	return need;
}

static bool need_fw_update_v6(struct ilitek_fw_handle *fw)
{
	struct ilitek_ts_device *dev = fw->dev;
	uint8_t i;
	bool need = false;

	TP_INFO(dev->id, "------------Lego Block Info.------------\n");

	/* first block CRC should be got by AP CRC command */
	api_protocol_set_cmd(dev, GET_AP_CRC, NULL);
	for (i = 1; i < fw->file.block_num; i++) {
		dev->ic[0].crc[i] = api_get_block_crc_by_addr(dev,
			CRC_CALCULATE, fw->file.blocks[i].start,
			fw->file.blocks[i].end);
	}

	for (i = 0; i < fw->file.block_num; i++) {
		fw->file.blocks[i].check_match = (fw->setting.force_update) ?
			false : (dev->ic[0].crc[i] == fw->file.blocks[i].check);

		need = (!fw->file.blocks[i].check_match) ? true : need;

		TP_INFO(dev->id, "Block[%hhu]: Start/End Addr.: %#x/%#x, IC/File CRC: %#x/%#x %s\n",
			i, fw->file.blocks[i].start, fw->file.blocks[i].end,
			dev->ic[0].crc[i], fw->file.blocks[i].check,
			(fw->file.blocks[i].check_match) ?
			"matched" : "not matched");
	}

	/* check BL mode firstly before AP-cmd related varaible, ex: ic_num */
	if (dev->ic[0].mode == 0x55) {
		need = true;
		goto force_return;
	}

	for (i = 1; i < dev->tp_info.ic_num; i++) {
		TP_INFO(dev->id, "Master/Slave[%hhu] CRC: %#x/%#x %s, Slave Mode: 0x%hhx %s\n",
			i, fw->file.blocks[0].check, dev->ic[i].crc[0],
			(fw->file.blocks[0].check == dev->ic[i].crc[0]) ?
			"matched" : "not matched",
			dev->ic[i].mode, dev->ic[i].mode_str);

		if (dev->ic[i].crc[0] == fw->file.blocks[0].check &&
		    dev->ic[i].mode == 0x5A)
			continue;
		need = true;
	}

	if (fw->m2v) {
		api_access_slave(dev, 0x80, M2V_GET_CHECKSUM,
				 &fw->m2v_checksum);

		fw->file.blocks[fw->file.block_num].check_match =
			fw->file.blocks[fw->file.block_num].check ==
			fw->m2v_checksum;
		TP_INFO(dev->id, "M2V IC/File Checksum: %#x/%#x %s\n",
			fw->m2v_checksum,
			fw->file.blocks[fw->file.block_num].check,
			(fw->file.blocks[fw->file.block_num].check_match) ?
			"matched" : "not matched");

		fw->m2v_need_update =
			!fw->file.blocks[fw->file.block_num].check_match ||
			fw->setting.force_update;
		need |= fw->m2v_need_update;    //Reed Add : 20230721（'='改成'|='，不然可能导致2326不更新FW）
	}

force_return:
	TP_INFO(dev->id, "----------------------------------------\n");

	return need;
}

static bool need_fw_update(struct ilitek_fw_handle *fw)
{
	struct ilitek_ts_device *dev = fw->dev;
	bool need = false;
	int i;

	if (dev->protocol.flag == PTL_V3)
		need = need_fw_update_v3(fw);
	else if (dev->protocol.flag == PTL_V6)
		need = need_fw_update_v6(fw);

	if (fw->cb.update_fw_ic_info)
		fw->cb.update_fw_ic_info(false,
					 dev->fw_ver, dev->ic[0].crc,
					 fw->file.block_num, fw->_private);

	if (fw->setting.force_update)
		return true;
	else if (fw->setting.fw_check_only)
		return false;

	if (fw->setting.fw_ver_check && dev->ic[0].mode == 0x5A) {
		TP_INFO(dev->id, "IC FW version: %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n",
			dev->fw_ver[0], dev->fw_ver[1], dev->fw_ver[2],
			dev->fw_ver[3], dev->fw_ver[4], dev->fw_ver[5],
			dev->fw_ver[6], dev->fw_ver[7]);
		TP_INFO(dev->id, "File FW version: %02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x\n",
			fw->setting.fw_ver[0], fw->setting.fw_ver[1],
			fw->setting.fw_ver[2], fw->setting.fw_ver[3],
			fw->setting.fw_ver[4], fw->setting.fw_ver[5],
			fw->setting.fw_ver[6], fw->setting.fw_ver[7]);

		for (i = 0; i < 8; i++) {
			if (dev->fw_ver[i] == fw->setting.fw_ver[i])
				continue;
			else if (dev->fw_ver[i] > fw->setting.fw_ver[i])
				break;
			TP_INFO(dev->id, "IC FW version is older than File FW version\n");

			return true;
		}
		TP_INFO(dev->id, "File FW version is older or the same, no need to update\n");

		return false;
	}

	return need;
}

static int update_master(struct ilitek_fw_handle *fw, int idx, uint32_t len)
{
	int error = 0;
	struct ilitek_ts_device *dev = fw->dev;
	unsigned int i;
	uint16_t file_crc;
	int retry = 3;

	TP_MSG(dev->id, "updating block[%d], data len: %u, start/end addr: %#x/%#x\n",
		idx, len, fw->file.blocks[idx].start, fw->file.blocks[idx].end);

err_retry:
	if ((dev->setting.no_retry && error < 0) || retry-- < 0)
		return (error < 0) ? error : -EINVAL;

	if ((error = api_write_enable_v6(dev, false, false,
					 fw->file.blocks[idx].start,
					 fw->file.blocks[idx].end)) < 0)
		return error;

	memset(dev->wbuf, 0xff, sizeof(dev->wbuf));
	for (i = fw->file.blocks[idx].start;
	     i < fw->file.blocks[idx].end; i += len) {
		/*
		 * check end addr. of data write buffer is within valid range.
		 */
		if (i + len > END_ADDR_LEGO) {
			TP_ERR(dev->id, "block[%d] write addr. %#x + %#x > %#x OOB\n",
				idx, i, len, END_ADDR_LEGO);
			return -EINVAL;
		}

		memcpy(dev->wbuf + 1, fw->file.buf + i, len);
		error = api_write_data_v6(dev, len + 1);

		if (error < 0)
			goto err_retry;

		fw->progress_curr = MIN(i + len - fw->file.blocks[idx].offset,
					fw->progress_max);
		fw->progress = (100 * fw->progress_curr) / fw->progress_max;
		TP_DBG(dev->id, "block[%d] update progress: %hhu%%\n",
			idx, fw->progress);

		if (fw->cb.update_progress)
			fw->cb.update_progress(fw->progress, fw->_private);
	}

	file_crc = get_crc(fw->file.blocks[idx].start,
			   fw->file.blocks[idx].end - 1,
			   fw->file.buf, fw->file.buf_size);
	dev->ic[0].crc[idx] =
		api_get_block_crc_by_addr(dev, CRC_GET,
					  fw->file.blocks[idx].start,
					  fw->file.blocks[idx].end);

	TP_INFO(dev->id, "block[%d]: start/end addr.: %#x/%#x, ic/file crc: %#x/%#x %s\n",
		idx, fw->file.blocks[idx].start, fw->file.blocks[idx].end,
		dev->ic[0].crc[idx], file_crc,
		(file_crc == dev->ic[0].crc[idx]) ?
		"matched" : "not matched");

	if (file_crc != dev->ic[0].crc[idx]) {
		error = -EINVAL;
		goto err_retry;
	}

	return 0;
}

static int update_slave(struct ilitek_fw_handle *fw)
{
	int error;
	struct ilitek_ts_device *dev = fw->dev;
	uint8_t i;

	for (i = 0; i < fw->file.block_num; i++) {
		dev->ic[0].crc[i] = api_get_block_crc_by_addr(dev,
			CRC_CALCULATE, fw->file.blocks[i].start,
			fw->file.blocks[i].end);
	}

	if ((error = api_protocol_set_cmd(dev, GET_AP_CRC,
					  &dev->tp_info.ic_num)) < 0)
			return error;

	for (i = 0; i < dev->tp_info.ic_num; i++) {
		if (dev->ic[0].crc[0] == dev->ic[i].crc[0] &&
		    !fw->setting.force_update)
			continue;

		TP_INFO(dev->id, "updating slave, master/slave[%hhu] crc: 0x%hx/0x%hx\n",
			i, dev->ic[0].crc[0], dev->ic[i].crc[0]);
		if ((error = api_access_slave(dev, 0x3, SLAVE_WRITE_AP,
					      NULL)) < 0 ||
		    (error = api_write_enable_v6(dev, false, true,
						 fw->file.blocks[0].start,
						 fw->file.blocks[0].end)) < 0)
			return error;

		goto success_return;
	}

	if ((error = api_protocol_set_cmd(dev, GET_MCU_MOD,
					  &dev->tp_info.ic_num)) < 0)
		return error;

	for (i = 0; i < dev->tp_info.ic_num; i++) {
		if (dev->ic[i].mode == 0x5A &&
		    !fw->setting.force_update)
			continue;

		TP_INFO(dev->id, "changing slave[%hhu]: 0x%hhx to AP mode\n",
			i, dev->ic[i].mode);
		if ((error = api_access_slave(dev, 0x3, SLAVE_SET_AP,
					      NULL)) < 0)
			return  error;
		break;
	}

success_return:
	return 0;
}

static int update_M3_M2V(struct ilitek_fw_handle *fw, uint32_t len)
{
	int error;
	struct ilitek_ts_device *dev = fw->dev;
	uint32_t i;
	uint8_t buf[6];
	////Reed Add : 20230721
	uint32_t AdressRange, M2V_Buf_Len, CheckSumRange;
	uint16_t AddDataCount;

	uint8_t j = fw->file.block_num;

	////Reed Add : 20230721（烧录M2V前，要切到Suspend Mode）
	if ((error = api_set_ctrl_mode(dev, mode_suspend, false)) < 0)
		return error;

	//TODO: [Joe] read garbage ?
	api_protocol_set_cmd(dev, GET_PTL_VER, NULL);
	api_protocol_set_cmd(dev, GET_PTL_VER, NULL);

	//Reed Add : 20230721
	//tmp = (fw->file.blocks[j].end % len) ?
	//	(len - (fw->file.blocks[j].end % len)) * 0xFF : 0;
	//fw->file.blocks[j].check += tmp;
	AdressRange = fw->file.blocks[j].end - fw->file.blocks[j].start;
	M2V_Buf_Len = fw->file.blocks[j].end - fw->file.blocks[j].start + 1;
	CheckSumRange = fw->file.blocks[j].check;
	AddDataCount = len - M2V_Buf_Len % len;
	AddDataCount = (AddDataCount >= 8) ? AddDataCount : AddDataCount + len;
	AdressRange += AddDataCount;
	CheckSumRange += AddDataCount * 0xFF;

	if ((error = api_to_bl_mode_m2v(dev, true)) < 0)
		return error;

	dev->cb.delay_ms(100);//Reed Add : 20230927

	if ((error = api_set_data_len(dev, len)) < 0)
		return error;

	TP_INFO(dev->id, "updating M2V, start/end addr.: %#x/%#x, file checksum: %#x\n",
		fw->file.blocks[j].start, fw->file.blocks[j].end,
		fw->file.blocks[j].check);

	//Reed Add : 20230721
	//buf[0] = (fw->file.blocks[j].end >> 16) & 0xFF;
	//buf[1] = (fw->file.blocks[j].end >> 8) & 0xFF;
	//buf[2] = fw->file.blocks[j].end & 0xFF;
	//buf[3] = (fw->file.blocks[j].check >> 16) & 0xFF;
	//buf[4] = (fw->file.blocks[j].check >> 8) & 0xFF;
	//buf[5] = fw->file.blocks[j].check & 0xFF;
	buf[0] = (AdressRange >> 16) & 0xFF;
	buf[1] = (AdressRange >> 8) & 0xFF;
	buf[2] = AdressRange & 0xFF;
	buf[3] = (CheckSumRange >> 16) & 0xFF;
	buf[4] = (CheckSumRange >> 8) & 0xFF;
	buf[5] = CheckSumRange & 0xFF;
	dev->cb.delay_ms(100);//Reed Add : 20230927
	if ((error = api_access_slave(dev, 0x80, M2V_WRITE_ENABLE, buf)) < 0)
		return error;

	memset(dev->wbuf, 0xff, sizeof(dev->wbuf));
	for (i = fw->file.blocks[j].start;
	     i < fw->file.blocks[j].end; i += len) {
		memcpy(dev->wbuf + 1, fw->m2v_buf + i, len);

		if ((error = api_write_data_m2v(dev, len + 1)) < 0)
			return  error;

		fw->progress_curr = MIN(fw->progress_curr + len,
					fw->progress_max);
		fw->progress = (100 * fw->progress_curr) / fw->progress_max;
		TP_DBG(dev->id, "m2v update progress: %hhu%%\n", fw->progress);

		if (fw->cb.update_progress)
			fw->cb.update_progress(fw->progress, fw->_private);
	}

	//TODO: [Joe] check checksum again ?

	if ((error = api_to_bl_mode_m2v(dev, false)) < 0)
		return error;

	dev->cb.delay_ms(100);//Reed Add : 20230927

	if ((error = api_access_slave(dev, 0x80, M2V_GET_FW_VER,
				      fw->m2v_fw_ver)) < 0)
		return error;

	TP_MSG_ARR(dev->id, "update M2V success, fw version:", TYPE_U8,
		   8, fw->m2v_fw_ver);

	return 0;
}

static int ilitek_update_BL_v1_8(struct ilitek_fw_handle *fw)
{
	int error;
	struct ilitek_ts_device *dev = fw->dev;
	uint8_t i;

	if ((error = api_set_data_len(dev, UPDATE_LEN)) < 0)
		return error;

	for (i = 0; i < fw->file.block_num; i++) {
		if (fw->file.blocks[i].check_match)
			continue;

		if ((error = update_master(fw, i, UPDATE_LEN)) < 0) {
			TP_ERR(dev->id, "Upgrade Block:%hhu failed, err: %d\n",
				i, error);
			return error;
		}
	}

	if ((error = api_to_bl_mode(dev, false, fw->file.blocks[0].start,
		fw->file.blocks[0].end)) < 0)
		return error;

	if ((error = api_set_ctrl_mode(dev, mode_suspend, false)) < 0)
		return error;

	/* get tp info. for updating ic num */
	if ((error = api_protocol_set_cmd(dev, GET_TP_INFO, NULL)) < 0)
		return error;

	if (dev->tp_info.ic_num > 1) {
		if (fw->cb.slave_update_notify)
			fw->cb.slave_update_notify(true, fw->_private);
		error = update_slave(fw);

		if (fw->cb.slave_update_notify)
			fw->cb.slave_update_notify(false, fw->_private);

		if (error < 0) {
			TP_ERR(dev->id, "upgrade slave failed, err: %d\n",
				error);
			return error;
		}
	}

	if (fw->m2v && fw->m2v_need_update &&
	    (error = update_M3_M2V(fw, 1024)) < 0) {
		TP_ERR(dev->id, "upgrade m2v slave failed, err: %d\n", error);
		return error;
	}

	return 0;
}

static int ilitek_update_BL_v1_7(struct ilitek_fw_handle *fw)
{
	int error;
	struct ilitek_ts_device *dev = fw->dev;
	unsigned int i;

	/*
	 * Erase data initially for the case that hex w/o data flash section.
	 * Due to historical factor, please double confirm before modify here.
	 */
	if ((error = api_erase_data_v3(dev)) < 0)
		return error;

	if (fw->file.blocks[1].end > fw->file.blocks[1].start) {
		TP_MSG(dev->id, "updating DF block, start/end addr.: %#x/%#x, file checksum: %#x\n",
			fw->file.blocks[1].start, fw->file.blocks[1].end,
			fw->file.blocks[1].check);

		/* end + 1 as W.A. for V3 BL bug */
		if ((error = api_write_enable_v3(dev, false, false,
						 fw->file.blocks[1].end + 1,
						 fw->file.blocks[1].check)) < 0)
			return error;

		for (i = fw->file.blocks[1].start;
		     i <= fw->file.blocks[1].end; i += 32) {
			memset(dev->wbuf + 1, 0, 32);
			memcpy(dev->wbuf + 1, fw->file.buf + i,
				MIN(fw->file.blocks[1].end - i + 1, 32));

			if ((error = api_write_data_v3(dev)) < 0)
				return error;

			dev->cb.delay_ms(2);

			if ((error = api_check_busy(dev, 1000, 10)) < 0)
				return error;

			fw->progress_curr += 32;
			fw->progress_curr = MIN(fw->progress_curr,
				fw->progress_max);
			fw->progress = (100 * fw->progress_curr) /
				fw->progress_max;
			TP_DBG(dev->id, "DF update progress: %hhu%%\n",
				fw->progress);

			if (fw->cb.update_progress)
				fw->cb.update_progress(fw->progress,
						       fw->_private);
		}

		dev->cb.delay_ms(50);

		dev->wbuf[0] = CMD_GET_AP_CRC;
		if ((error = write_then_read(dev, dev->wbuf, 1, NULL, 0)) < 0 ||
		    (error = api_check_busy(dev, 1000, 10)) < 0)
			return error;

		dev->wbuf[0] = CMD_GET_AP_CRC;
		if ((error = write_then_read(dev, dev->wbuf, 1,
					     dev->rbuf, 4)) < 0)
			return error;

		dev->ic[0].crc[1] = (le16(dev->rbuf + 2) << 16) | le16(dev->rbuf);

		TP_INFO(dev->id, "DF block, start/end addr.: %#x/%#x, ic/file checkksum: %#x/%#x %s\n",
			fw->file.blocks[1].start, fw->file.blocks[1].end,
			dev->ic[0].crc[1], fw->file.blocks[1].check,
			(dev->ic[0].crc[1] == fw->file.blocks[1].check) ?
			"matched" : "not matched");

		if (dev->ic[0].crc[1] != fw->file.blocks[1].check)
			return -EFAULT;
	}

	/*
	 * Update AP code forcely if AP crc/checksum not match or
	 * Data Flash has been updated.
	 */
	if (!fw->file.blocks[0].check_match ||
	    fw->file.blocks[1].end > fw->file.blocks[1].start) {
		TP_MSG(dev->id, "updating AP block, start/end addr.: %#x/%#x, file crc: %#x\n",
			fw->file.blocks[0].start, fw->file.blocks[0].end,
			fw->file.blocks[0].check);

		/* end + 1 as W.A. for V3 BL bug */
		if ((error = api_write_enable_v3(dev, false, true,
						 fw->file.blocks[0].end + 1,
						 fw->file.blocks[0].check)) < 0)
			return error;

		for (i = fw->file.blocks[0].start;
		     i <= fw->file.blocks[0].end; i += 32) {
			memset(dev->wbuf + 1, 0xFF, 32);
			memcpy(dev->wbuf + 1, fw->file.buf + i,
				MIN(fw->file.blocks[1].end - i + 1, 32));
			if ((error = api_write_data_v3(dev)) < 0)
				return error;

			dev->cb.delay_ms(2);

			if ((error = api_check_busy(dev, 1000, 10)) < 0)
				return error;

			fw->progress_curr += 32;
			fw->progress_curr = MIN(fw->progress_curr,
				fw->progress_max);
			fw->progress = (100 * fw->progress_curr) /
				fw->progress_max;
			TP_DBG(dev->id, "AP update progress: %hhu%%\n",
				fw->progress);

			if (fw->cb.update_progress)
				fw->cb.update_progress(fw->progress,
						       fw->_private);
		}

		dev->wbuf[0] = CMD_GET_AP_CRC;
		if ((error = write_then_read(dev, dev->wbuf, 1,
					     NULL, 0)) < 0 ||
		    (error = api_check_busy(dev, 1000, 10)) < 0)
			return error;

		dev->wbuf[0] = CMD_GET_AP_CRC;
		if ((error = write_then_read(dev, dev->wbuf, 1,
					     dev->rbuf, 4)) < 0)
			return error;

		dev->ic[0].crc[0] =
			(le16(dev->rbuf + 2) << 16) | le16(dev->rbuf);
		TP_INFO(dev->id, "AP block, start/end addr.: %#x/%#x, ic/file crc: %#x/%#x %s\n",
			fw->file.blocks[0].start, fw->file.blocks[0].end,
			dev->ic[0].crc[0], fw->file.blocks[0].check,
			(dev->ic[0].crc[0] == fw->file.blocks[0].check) ?
			"matched" : "not matched");

		if (dev->ic[0].crc[0] != fw->file.blocks[0].check)
			return -EFAULT;
	}

	return 0;
}

static int ilitek_update_BL_v1_6(struct ilitek_fw_handle *fw)
{
	int error;
	struct ilitek_ts_device *dev = fw->dev;
	struct ilitek_ts_callback *cb = &dev->cb;
	unsigned int i, j;

	uint32_t bytes, end, check;

	if (fw->file.blocks[1].end > fw->file.blocks[1].start) {
		TP_INFO(dev->id, "updating DF block, start/end addr.: %#x/%#x, file checksum: %#x\n",
			fw->file.blocks[1].start, fw->file.blocks[1].end,
			fw->file.blocks[1].check);

		/* BL 1.6 need more 32 byte 0xFF, end addr need to be multiples of 32 */
		bytes = ((fw->file.blocks[1].end + 1) % 32) ?
			31 + 32 - ((fw->file.blocks[1].end + 1) % 32) : 31;
		end = fw->file.blocks[1].end + bytes;
		check = fw->file.blocks[1].check + (bytes * 0xff);
		TP_MSG(dev->id, "(after modified) DF start/end: %#x/%#x, checksum: %#x\n",
			fw->file.blocks[1].start, end, check);

		/* end + 1 as W.A. for V3 BL bug */
		if ((error = api_write_enable_v3(dev, false, false, end + 1,
						 check)) < 0)
			return error;

		for (i = fw->file.blocks[1].start, j = 0;
		     i <= fw->file.blocks[1].end; i += 32, j++) {
			memset(dev->wbuf + 1, 0xFF, 32);
			memcpy(dev->wbuf + 1, fw->file.buf + i,
				MIN(fw->file.blocks[1].end - i + 1, 32));
			if ((error = api_write_data_v3(dev)) < 0)
				return error;

			cb->delay_ms((j % 16) ? 1 : 5);

			fw->progress_curr = MIN(fw->progress_curr + 32,
				fw->progress_max);
			fw->progress = (100 * fw->progress_curr) / fw->progress_max;
			TP_DBG(dev->id, "DF update progress: %hhu%%\n", fw->progress);

			if (fw->cb.update_progress)
				fw->cb.update_progress(fw->progress, fw->_private);
		}
		cb->delay_ms(10);
		/* write 31 bytes 0xFF at the end */
		memset(dev->wbuf + 1, 0xFF, 32); dev->wbuf[32] = 0;
		if ((error = api_write_data_v3(dev)) < 0)
			return error;
		cb->delay_ms(10);
	}

	/*
	* Update AP code forcely if AP crc/checksum not match or
	* Data Flash has been updated.
	*/
	if (!fw->file.blocks[0].check_match ||
	    fw->file.blocks[1].end > fw->file.blocks[1].start) {
		TP_INFO(dev->id, "updating AP block, start/end addr.: %#x/%#x, file crc: %#x\n",
			fw->file.blocks[0].start, fw->file.blocks[0].end,
			fw->file.blocks[0].check);

		/* BL 1.6 need more 32 byte 0xFF, end addr need to be multiples of 32 */
		bytes = ((fw->file.blocks[0].end + 1) % 32) ?
			31 + 32 - ((fw->file.blocks[0].end + 1) % 32) : 31;
		end = fw->file.blocks[0].end + bytes;
		check = fw->file.blocks[0].check + (bytes * 0xff);
		TP_MSG(dev->id, "(after modified) AP start/end: %#x/%#x, checksum: %#x\n",
			fw->file.blocks[0].start, end, check);

		/* end + 1 as W.A. for V3 BL bug */
		if ((error = api_write_enable_v3(dev, false, true, end + 1,
			check)) < 0)
			return error;

		for (i = fw->file.blocks[0].start, j = 0;
			i <= fw->file.blocks[0].end; i += 32, j++) {
			memset(dev->wbuf + 1, 0xFF, 32);
			memcpy(dev->wbuf + 1, fw->file.buf + i,
				MIN(fw->file.blocks[0].end - i + 1, 32));

			if ((error = api_write_data_v3(dev)) < 0)
				return error;

			cb->delay_ms((j % 16) ? 1 : 5);

			fw->progress_curr = MIN(fw->progress_curr + 32,
				fw->progress_max);
			fw->progress = (100 * fw->progress_curr) / fw->progress_max;
			TP_DBG(dev->id, "AP update progress: %hhu%%\n", fw->progress);

			if (fw->cb.update_progress)
				fw->cb.update_progress(fw->progress, fw->_private);
		}
		cb->delay_ms(10);
		/* write 31 bytes 0xFF at the end */
		memset(dev->wbuf + 1, 0xFF, 32); dev->wbuf[32] = 0;
		if ((error = api_write_data_v3(dev)) < 0)
			return error;
		cb->delay_ms(10);

#ifdef ILITEK_BOOT_UPDATE
		/*
		* .ili file fill DF section with default 0xFF, but not 0
		* which make flow stuck in BL mode.
		* so HW-reset is need before switching to AP mode.
		* remove HW-reset after fixing .ili converter bug.
		*/
		reset_helper(dev);
#endif
	}

	return 0;
}

static void update_progress(struct ilitek_fw_handle *fw)
{
	struct ilitek_ts_device *dev = fw->dev;
	uint8_t i;
	unsigned int last_end = 0, last_offset = 0;

	fw->progress = 0;
	fw->progress_max = 0;
	fw->progress_curr = 0;

	switch (dev->protocol.flag) {
	case PTL_V3:
		fw->progress_max +=
			(fw->file.blocks[1].end > fw->file.blocks[1].start) ?
			fw->file.blocks[1].end - fw->file.blocks[1].start : 0;
		fw->progress_max +=
			(fw->file.blocks[0].end > fw->file.blocks[0].start) ?
			fw->file.blocks[0].end - fw->file.blocks[0].start : 0;
		break;

	case PTL_V6:
		for (i = 0; i < fw->file.block_num; i++) {
			if (fw->file.blocks[i].check_match)
				continue;

			fw->progress_max +=
				fw->file.blocks[i].end -
				fw->file.blocks[i].start;
			last_offset += fw->file.blocks[i].start - last_end;
			fw->file.blocks[i].offset = last_offset;

			last_end = fw->file.blocks[i].end;
		}

		if (fw->m2v_need_update) {
			fw->progress_max +=
				fw->file.blocks[fw->file.block_num].end -
				fw->file.blocks[fw->file.block_num].start;
		}

		break;
	}
}

int ilitek_update_bind_dev(void *handle, void *dev)
{
	struct ilitek_fw_handle *fw = (struct ilitek_fw_handle *)handle;

	if (!handle || !dev)
		return -EINVAL;

	fw->dev = (struct ilitek_ts_device *)dev;

	return api_update_ts_info(dev);
}

void *ilitek_update_init(void *_dev, struct ilitek_update_callback *cb,
			 void *_private)
{
	struct ilitek_fw_handle *fw;
	struct ilitek_ts_device *dev = (struct ilitek_ts_device *)_dev;

	fw = (struct ilitek_fw_handle *)MALLOC(sizeof(*fw));
	if (!fw)
		return NULL;

	/* initial all member to 0/ false/ NULL */
	memset(fw, 0, sizeof(*fw));

	if (dev && ilitek_update_bind_dev(fw, dev) < 0)
		goto err_free_fw;

	fw->dev = dev;
	fw->_private = _private;
	fw->file.buf_size = ILITEK_FW_BUF_SIZE;
	fw->file.buf = (uint8_t *)CALLOC(fw->file.buf_size, 1);
	if (!fw->file.buf)
		goto err_free_fw;

	fw->m2v_buf = (uint8_t *)CALLOC(ILITEK_FW_BUF_SIZE, 1);
	if (!fw->m2v_buf)
		goto err_free_fw_buf;

	if (cb)
		memcpy(&fw->cb, cb, sizeof(*cb));

	return fw;

err_free_fw_buf:
	CFREE(fw->file.buf);
err_free_fw:
	FREE(fw);

	return NULL;
}

void ilitek_update_exit(void *handle)
{
	struct ilitek_fw_handle *fw = (struct ilitek_fw_handle *)handle;

	if (!handle)
		return;

	if (fw->file.buf)
		CFREE(fw->file.buf);

	if (fw->m2v_buf)
		CFREE(fw->m2v_buf);

	if (fw)
		FREE(fw);
}

int ilitek_update_load_fw(void *handle, char *fw_name)
{
	int error;
	struct ilitek_fw_handle *fw = (struct ilitek_fw_handle *)handle;

	uint32_t i;

	if (!handle)
		return -EINVAL;

	if ((error = decode_firmware(fw, fw_name)) < 0)
		return error;

	if (fw->cb.update_fw_file_info)
		fw->cb.update_fw_file_info(&fw->file, fw->_private);

	if (!fw->dev)
		return 0;

	/* for Lego and V6 IC, check block's start/end address validity */
	if (fw->dev->protocol.flag == PTL_V6) {
		for (i = 0; i < fw->file.block_num; i++) {
			if (fw->dev->mcu_info.min_addr <=
				fw->file.blocks[i].start &&
			    fw->dev->mcu_info.max_addr >
				fw->file.blocks[i].end)
				continue;

			if (!(fw->file.blocks[i].start % 0x1000))
				continue;

			TP_ERR(fw->dev->id, "Block[%u] addr. OOB (%#x <= %#x/%#x < %#x) or invalid start addr\n",
				i, fw->dev->mcu_info.min_addr,
				fw->file.blocks[i].start,
				fw->file.blocks[i].end,
				fw->dev->mcu_info.max_addr);
			return -EINVAL;
		}
	}

	TP_MSG(fw->dev->id, "IC: ILI%s, Firmware File: ILI%s matched\n",
		fw->dev->mcu_info.ic_name, fw->file.ic_name);

	return 0;
}

int ilitek_update_start(void *handle)
{
	int error;
	int8_t retry = 0;
	struct ilitek_fw_handle *fw = (struct ilitek_fw_handle *)handle;
	struct ilitek_ts_device *dev;

	if (!handle)
		return -EINVAL;
	dev = fw->dev;

	/*
	 * Some platform (ITS-Bridge) might change touch controller
	 * after loading fw file, get panel info. forcely and
	 * re-check the ic/file are matched.
	 */
	if ((error = api_update_ts_info(dev)) < 0 ||
	    strcmp(dev->mcu_info.ic_name, fw->file.ic_name)) {
		TP_ERR(fw->dev->id, "get ic info failed, err: %d or ic/file (ILI%s/ILI%s) not matched\n",
			error, fw->dev->mcu_info.ic_name, fw->file.ic_name);
		return -EPERM;
	}

	TP_INFO(dev->id, "[ilitek_update_start] start\n");

	do {
		TP_DBG(dev->id, "retry: %hhd, retry_limit: %hhd\n",
			retry, fw->setting.retry);
		if (retry)
			reset_helper(dev);

		if ((error = api_set_ctrl_mode(dev, mode_suspend, false)) < 0)
			continue;

		if (!need_fw_update(fw)) {
			if (is_231x(dev))
				goto erase_data_flash_231x;
			goto success_return;
		}

		update_progress(fw);
		if (fw->cb.update_progress)
			fw->cb.update_progress(0, fw->_private);

		if ((error = api_to_bl_mode(dev, true, 0, 0)) < 0)
			continue;

		TP_INFO_ARR(dev->id, "[BL Firmware Version]",
			    TYPE_U8, 8, dev->fw_ver);
		TP_INFO(dev->id, "[ilitek_update_start] start to program\n");

		switch (dev->protocol.ver & 0xFFFF00) {
		case BL_PROTOCOL_V1_8:
			error = ilitek_update_BL_v1_8(fw);
			break;
		case BL_PROTOCOL_V1_7:
			error = ilitek_update_BL_v1_7(fw);
			break;
		case BL_PROTOCOL_V1_6:
			error = ilitek_update_BL_v1_6(fw);
			break;
		default:
			TP_ERR(dev->id, "BL protocol ver: 0x%x not supported\n",
				dev->protocol.ver);
			continue;
		}
		if (error < 0)
			continue;

		if ((error = api_to_bl_mode(dev, false,
					    fw->file.blocks[0].start,
					    fw->file.blocks[0].end)) < 0)
			continue;

erase_data_flash_231x:
		/*
		 * If no data flash section in firmware file,
		 * 231x need to erase data flash after change to AP mode.
		 */
		if (fw->file.blocks[1].end < fw->file.blocks[1].start &&
		    is_231x(dev) && (error = api_erase_data_v3(dev)) < 0)
			continue;

success_return:
		if ((error = api_update_ts_info(dev)) < 0)
			continue;

		if ((error = api_set_ctrl_mode(dev, mode_normal, false)) < 0)
			continue;

		if (fw->cb.update_fw_ic_info)
			fw->cb.update_fw_ic_info(true,
						 dev->fw_ver, dev->ic[0].crc,
						 fw->file.block_num,
						 fw->_private);

		if (fw->cb.update_progress)
			fw->cb.update_progress(100, fw->_private);

		if (need_retry(fw))
			continue;

		TP_INFO(dev->id, "[ilitek_update_start] success\n");

		return 0;
	} while (!dev->setting.no_retry && ++retry < fw->setting.retry);

	TP_ERR(dev->id, "[ilitek_update_start] fw update failed, err: %d\n",
		error);

	return (error < 0) ? error : -EFAULT;
}

void ilitek_update_setting(void *handle, struct ilitek_fw_settings *setting)
{
	struct ilitek_fw_handle *fw = (struct ilitek_fw_handle *)handle;

	if (!handle)
		return;

	memcpy(&fw->setting, setting, sizeof(struct ilitek_fw_settings));
}

