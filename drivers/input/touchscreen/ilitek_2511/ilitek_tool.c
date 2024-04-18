/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Luca Hsu <luca_hsu@ilitek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */

#include "ilitek_ts.h"
#include "ilitek_common.h"
#include <linux/firmware.h>

struct dev_data {
	dev_t devno;
	struct cdev cdev;
	struct class *class;
};

static struct dev_data ilitek_dev;
static struct proc_dir_entry *ilitek_proc;
static struct proc_dir_entry *ilitek_proc_entry;

#define ILITEK_IOCTL_BASE                       100
#define ILITEK_IOCTL_I2C_WRITE_DATA             _IOWR(ILITEK_IOCTL_BASE, 0, uint8_t*)
#define ILITEK_IOCTL_I2C_WRITE_LENGTH           _IOWR(ILITEK_IOCTL_BASE, 1, int32_t)
#define ILITEK_IOCTL_I2C_READ_DATA              _IOWR(ILITEK_IOCTL_BASE, 2, uint8_t*)
#define ILITEK_IOCTL_I2C_READ_LENGTH            _IOWR(ILITEK_IOCTL_BASE, 3, int32_t)
#define ILITEK_IOCTL_USB_WRITE_DATA             _IOWR(ILITEK_IOCTL_BASE, 4, uint8_t*)
#define ILITEK_IOCTL_USB_WRITE_LENGTH           _IOWR(ILITEK_IOCTL_BASE, 5, int32_t)
#define ILITEK_IOCTL_USB_READ_DATA              _IOWR(ILITEK_IOCTL_BASE, 6, uint8_t*)
#define ILITEK_IOCTL_USB_READ_LENGTH            _IOWR(ILITEK_IOCTL_BASE, 7, int32_t)
#define ILITEK_IOCTL_DRIVER_INFORMATION		_IOWR(ILITEK_IOCTL_BASE, 8, int32_t)
#define ILITEK_IOCTL_USB_UPDATE_RESOLUTION      _IOWR(ILITEK_IOCTL_BASE, 9, int32_t)
#define ILITEK_IOCTL_I2C_INT_FLAG	        _IOWR(ILITEK_IOCTL_BASE, 10, int32_t)
#define ILITEK_IOCTL_I2C_UPDATE                 _IOWR(ILITEK_IOCTL_BASE, 11, int32_t)
#define ILITEK_IOCTL_STOP_READ_DATA             _IOWR(ILITEK_IOCTL_BASE, 12, int32_t)
#define ILITEK_IOCTL_START_READ_DATA            _IOWR(ILITEK_IOCTL_BASE, 13, int32_t)
#define ILITEK_IOCTL_GET_INTERFANCE		_IOWR(ILITEK_IOCTL_BASE, 14, int32_t)	//default setting is i2c interface
#define ILITEK_IOCTL_I2C_SWITCH_IRQ		_IOWR(ILITEK_IOCTL_BASE, 15, int32_t)
#define ILITEK_IOCTL_UPDATE_FLAG		_IOWR(ILITEK_IOCTL_BASE, 16, int32_t)
#define ILITEK_IOCTL_I2C_UPDATE_FW		_IOWR(ILITEK_IOCTL_BASE, 18, int32_t)
#define ILITEK_IOCTL_RESET			_IOWR(ILITEK_IOCTL_BASE, 19, int32_t)
#define ILITEK_IOCTL_INT_STATUS			_IOWR(ILITEK_IOCTL_BASE, 20, int32_t)

#ifdef ILITEK_TUNING_MESSAGE
extern bool ilitek_debug_flag;
#define ILITEK_IOCTL_DEBUG_SWITCH		_IOWR(ILITEK_IOCTL_BASE, 21, int32_t)
#endif

#define ILITEK_IOCTL_I2C_INT_CLR		_IOWR(ILITEK_IOCTL_BASE, 22, int32_t)
#define ILITEK_IOCTL_I2C_INT_POLL		_IOWR(ILITEK_IOCTL_BASE, 23, bool*)
#define ILITEK_IOCTL_I2C_ISR_TYPE		_IOWR(ILITEK_IOCTL_BASE, 24, uint32_t)


#define ILITEK_DEVICE_NODE_PERMISSON			0755

static int32_t ilitek_file_open(struct inode *inode, struct file *filp)
{
	ts->operation_protection = true;
	TP_MSG(NULL, "operation_protection = %d\n", ts->operation_protection);
	return 0;
}

static int32_t ilitek_file_close(struct inode *inode, struct file *filp)
{
	ts->operation_protection = false;
	TP_MSG(NULL, "operation_protection = %d\n", ts->operation_protection);
	return 0;
}

static ssize_t ilitek_file_write(struct file *filp,
				 const char *buf, size_t size, loff_t *f_pos)
{
	int32_t ret = 0, count = 0;
	uint8_t buffer[512];
	uint32_t *data;
	char *token = NULL, *cur = NULL;

	if (size > sizeof(buffer)) {
		TP_ERR(NULL, "invalid buf len: %zu > %zu too large\n",
			size, sizeof(buffer));
		return -EINVAL;
	}

	ret = copy_from_user(buffer, buf, size);
	if (ret < 0) {
		TP_ERR(NULL, "copy data from user space, failed");
		return -1;
	}

	token = cur = buffer;

	data = kcalloc(size, sizeof(u32), GFP_KERNEL);

	while ((token = strsep(&cur, ",")) != NULL) {
		//data[count] = str2hex(token);
		sscanf(token,"%x", &data[count]);
		TP_MSG(NULL, "data[%d] = %x\n", count, data[count]);
		count++;
	}

	if (buffer[size - 2] == 'I' && (size == 20 || size == 52) && buffer[0] == 0x77 && buffer[1] == 0x77) {

		TP_MSG(NULL, "IOCTL_WRITE CMD = %d\n", buffer[2]);
		switch (buffer[2]) {
		case 13:
			//ilitek_irq_enable();
			TP_MSG(NULL, "ilitek_irq_enable. do nothing\n");
			break;
		case 12:
			//ilitek_irq_disable();
			TP_MSG(NULL, "ilitek_irq_disable. do nothing\n");
			break;
		case 19:
			ilitek_reset(ts->dev->reset_time);
			break;
#ifdef ILITEK_TUNING_MESSAGE
		case 21:
			TP_MSG(NULL, "ilitek The ilitek_debug_flag = %d.\n", buffer[3]);
			if (buffer[3] == 0) {
				ilitek_debug_flag = false;
			} else if (buffer[3] == 1) {
				ilitek_debug_flag = true;
			}
			break;
#endif
		case 15:
			if (buffer[3] == 0)
				ilitek_irq_disable();
			else
				ilitek_irq_enable();

			break;
		case 16:
			ts->operation_protection = buffer[3];
			TP_MSG(NULL, "ts->operation_protection = %d\n", ts->operation_protection);
			break;
		case 18:
			ilitek_irq_disable();
			mutex_lock(&ts->ilitek_mutex);
			ret = ilitek_write(&buffer[3], 33);
			mutex_unlock(&ts->ilitek_mutex);
			ilitek_irq_enable();
			if (ret < 0)
				TP_ERR(NULL, "i2c write error, ret %d\n", ret);

			return ret;
			break;
		default:
			return -1;
		}
	}

	if (buffer[size - 2] == 'W') {
		ilitek_irq_disable();
		mutex_lock(&ts->ilitek_mutex);
		ret = ilitek_write(buffer, size - 2);
		mutex_unlock(&ts->ilitek_mutex);
		ilitek_irq_enable();
		if (ret < 0) {
			TP_ERR(NULL, "i2c write error, ret %d\n", ret);
			return ret;
		}
	} else if (!strncmp(buffer, "unhandle_irq", strlen("unhandle_irq"))) {
		ts->unhandle_irq = !ts->unhandle_irq;
		TP_MSG(NULL, "ts->unhandle_irq = %d.\n", ts->unhandle_irq);
	} else if (!strncmp(buffer, "dbg_pkt", strlen("dbg_pkt"))) {
		set_log_level(log_level_pkt);
		TP_MSG(NULL, "ilitek_log_level_value = %d.\n", log_level_pkt);
	} else if (!strncmp(buffer, "dbg_debug", strlen("dbg_debug"))) {
		set_log_level(log_level_dbg);
		TP_MSG(NULL, "ilitek_log_level_value = %d.\n", log_level_dbg);
	} else if (!strncmp(buffer, "dbg_info", strlen("dbg_info"))) {
		set_log_level(log_level_msg);
		TP_MSG(NULL, "ilitek_log_level_value = %d.\n", log_level_msg);
	} else if (!strncmp(buffer, "dbg_err", strlen("dbg_err"))) {
		set_log_level(log_level_err);
		TP_MSG(NULL, "ilitek_log_level_value = %d.\n", log_level_err);
	} else if (!strncmp(buffer, "dbg_num", strlen("dbg_num"))) {
		TP_MSG(NULL, "ilitek_log_level_value = %d.\n", tp_log_level);
	}
#ifdef ILITEK_TUNING_MESSAGE
	else if (!strncmp(buffer, "truning_dbg_flag", strlen("truning_dbg_flag"))) {
		ilitek_debug_flag = !ilitek_debug_flag;
		TP_MSG(NULL, " %s debug_flag message(%X).\n", ilitek_debug_flag ? "Enabled" : "Disabled", ilitek_debug_flag);
	}
#endif
	else if (!strncmp(buffer, "irq_status", strlen("irq_status"))) {
		TP_MSG(NULL, "gpio_get_value(i2c.irq_gpio) = %d.\n", gpio_get_value(ts->irq_gpio));
	} else if (!strncmp(buffer, "enable", strlen("enable"))) {
		ilitek_irq_enable();
		TP_MSG(NULL, "irq enable\n");
	} else if (!strncmp(buffer, "disable", strlen("disable"))) {
		ilitek_irq_disable();
		TP_MSG(NULL, "irq disable\n");
	} else if (!strncmp(buffer, "info", strlen("info"))) {
		ilitek_irq_disable();
		mutex_lock(&ts->ilitek_mutex);
		api_update_ts_info(ts->dev);
		mutex_unlock(&ts->ilitek_mutex);
		ilitek_irq_enable();
	} else if (!strncmp(buffer, "reset", strlen("reset"))) {
		ilitek_reset(ts->dev->reset_time);
 	}

	TP_DBG(NULL, "ilitek return count = %zu\n", size);
	kfree(data);
	return size;
}

static FOPS_IOCTL_FUNC(ilitek_file_ioctl, uint32_t cmd, unsigned long arg)
{
	static uint8_t *buffer;
	static unsigned long len = 0;
	int32_t ret = 0;
	int tmp;

	buffer = kmalloc(ILITEK_IOCTL_MAX_TRANSFER, GFP_KERNEL);
	memset(buffer, 0 , ILITEK_IOCTL_MAX_TRANSFER);

	switch (cmd) {
	case ILITEK_IOCTL_I2C_WRITE_DATA:
		if (len > ILITEK_IOCTL_MAX_TRANSFER) {
			TP_ERR(NULL, "invalid write len: %lu > %lu too large\n",
				len, ILITEK_IOCTL_MAX_TRANSFER);
			ret = -EINVAL;
			break;
		}
		
		if (copy_from_user(buffer, (uint8_t *)arg, len)) {
			TP_ERR(NULL, "copy data from user space, failed\n");
			ret = -EFAULT;
			break;
		}

		mutex_lock(&ts->ilitek_mutex);
		ret = ilitek_write_and_read(buffer, len, 0, NULL, 0);
		mutex_unlock(&ts->ilitek_mutex);
		if (ret < 0)
			TP_ERR(NULL, "i2c write failed, cmd: %x\n", buffer[0]);
		break;
	case ILITEK_IOCTL_I2C_READ_DATA:
		if (len > ILITEK_IOCTL_MAX_TRANSFER) {
			TP_ERR(NULL, "invalid read len: %lu > %lu too large\n",
				len, ILITEK_IOCTL_MAX_TRANSFER);
			ret = -EINVAL;
			break;
		}

		mutex_lock(&ts->ilitek_mutex);
		ret = ilitek_write_and_read(NULL, 0, 0, buffer, len);
		mutex_unlock(&ts->ilitek_mutex);
		if (ret < 0) {
			TP_ERR(NULL, "i2c read failed, buf: %x\n", buffer[0]);
			break;
		}

		if (copy_to_user((uint8_t *)arg, buffer, len)) {
			ret = -EFAULT;
			TP_ERR(NULL, "copy data to user space, failed\n");
		}
		break;
	case ILITEK_IOCTL_I2C_WRITE_LENGTH:
	case ILITEK_IOCTL_I2C_READ_LENGTH:
		len = arg;
		break;
	case ILITEK_IOCTL_DRIVER_INFORMATION:
		memcpy(buffer, driver_ver, 7);
		if (copy_to_user((uint8_t *)arg, buffer, 7))
			ret = -EFAULT;
		break;
	case ILITEK_IOCTL_I2C_UPDATE:
		break;
	case ILITEK_IOCTL_I2C_INT_FLAG:
		buffer[0] = !(gpio_get_value(ts->irq_gpio));
		if (copy_to_user((uint8_t *)arg, buffer, 1)) {
			TP_ERR(NULL, "copy data to user space, failed\n");
			ret = -EFAULT;
			break;
		}
		TP_DBG(NULL, "ILITEK_IOCTL_I2C_INT_FLAG = %d.\n", buffer[0]);
		break;
	case ILITEK_IOCTL_START_READ_DATA:
		ilitek_irq_enable();
		ts->unhandle_irq = false;
		TP_MSG(NULL, "enable_irq and ts->unhandle_irq = false.\n");
		break;
	case ILITEK_IOCTL_STOP_READ_DATA:
		ilitek_irq_disable();
		ts->unhandle_irq = true;
		TP_MSG(NULL, "disable_irq and ts->unhandle_irq = true.\n");
		break;
	case ILITEK_IOCTL_RESET:
		ilitek_reset(ts->dev->reset_time);
		break;
	case ILITEK_IOCTL_INT_STATUS:
		if (put_user(gpio_get_value(ts->irq_gpio), (int32_t *)arg))
			ret = -EFAULT;
		break;
#ifdef ILITEK_TUNING_MESSAGE
	case ILITEK_IOCTL_DEBUG_SWITCH:
		if (copy_from_user(buffer, (uint8_t *)arg, 1)) {
			ret = -EFAULT;
			break;
		}
		TP_MSG(NULL, "ilitek The debug_flag = %d.\n", buffer[0]);
		if (buffer[0] == 0)
			ilitek_debug_flag = false;
		else if (buffer[0] == 1)
			ilitek_debug_flag = true;
		break;
#endif
	case ILITEK_IOCTL_I2C_SWITCH_IRQ:
		if (copy_from_user(buffer, (uint8_t *)arg, 1)) {
			ret = -EFAULT;
			break;
		}

		if (buffer[0] == 0)
			ilitek_irq_disable();
		else
			ilitek_irq_enable();

		break;
	case ILITEK_IOCTL_UPDATE_FLAG:
		ts->operation_protection = arg;
		TP_MSG(NULL, "operation_protection = %d\n", ts->operation_protection);
		break;
	case ILITEK_IOCTL_I2C_UPDATE_FW:
		if (copy_from_user(buffer, (uint8_t *)arg, 35)) {
			TP_ERR(NULL, "copy data from user space, failed\n");
			ret = -EFAULT;
			break;
		}

		ilitek_irq_disable();
		mutex_lock(&ts->ilitek_mutex);
		ret = ilitek_write_and_read(buffer, buffer[34], 0, NULL, 0);
		mutex_unlock(&ts->ilitek_mutex);
		ilitek_irq_enable();

		if (ret < 0)
			TP_ERR(NULL, "i2c write, failed\n");

		break;
	case ILITEK_IOCTL_I2C_INT_CLR:
		TP_DBG(NULL, "ILITEK_IOCTL_I2C_INT_CLR, set get_INT false\n");
		atomic_set(&ts->get_INT, 0);
		break;
	case ILITEK_IOCTL_I2C_INT_POLL:
		tmp = atomic_read(&ts->get_INT);
		TP_DBG(NULL, "ILITEK_IOCTL_I2C_INT_POLL, get_INT: %d\n", tmp);

		if (copy_to_user((uint8_t *)arg, &tmp, 1)) {
			TP_ERR(NULL, "copy data to user space, failed\n");
			ret = -EFAULT;
		}
		break;
	case ILITEK_IOCTL_I2C_ISR_TYPE:
		TP_MSG(NULL, "ILITEK_IOCTL_I2C_ISR_TYPE, set ISR type: %lu\n", arg);
		ts->irq_handle_type = (arg >> 16);
		ts->irq_read_len = arg & 0xFFFF;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	kfree(buffer);
	return (ret < 0) ? ret : 0;
}

static ssize_t ilitek_file_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	uint8_t *tmp;
	int32_t ret;
	long rc;

	if (count > 8192)
		count = 8192;

	tmp = kmalloc(count, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	ret = ilitek_read(tmp, count);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();
	if (ret < 0)
		TP_ERR(NULL, "i2c read error, ret %d\n", ret);

	rc = copy_to_user(buf, tmp, count);

	kfree(tmp);

	return ret > 0 ? count : ret;
}

static struct file_operations ilitek_fops = {
	.FOPS_IOCTL = ilitek_file_ioctl,
	.read = ilitek_file_read,
	.write = ilitek_file_write,
	.open = ilitek_file_open,
	.release = ilitek_file_close,
};

static struct PROC_FOPS_T ilitek_proc_fops = {
	.PROC_IOCTL = ilitek_file_ioctl,
	.PROC_READ = ilitek_file_read,
	.PROC_WRITE = ilitek_file_write,
	.PROC_OPEN = ilitek_file_open,
	.PROC_RELEASE = ilitek_file_close,
};

static ssize_t ilitek_update_fw_read(struct file *fp, char __user *buf,
				     size_t size, loff_t *off)
{
	int error;
	int cnt = 0;
	uint8_t str[256];

	if (*off)
		return 0;

	memset(str, 0, sizeof(str));
	if ((error = ilitek_upgrade_firmware("ilitek.hex")) < 0 &&
	    (error = ilitek_upgrade_firmware("ilitek.bin")) < 0)
		cnt += scnprintf(str, sizeof(str),
				 "upgrade failed, err: %d\n", error);
	else
		cnt += scnprintf(str, sizeof(str),
				 "upgrade success, fw version: %*phD\n",
				 8, ts->dev->fw_ver);

	if (copy_to_user(buf, str, cnt))
		return -EFAULT;

	*off += cnt;
	return cnt;
}

static struct PROC_FOPS_T ilitek_fops_fwupdate = {
	.PROC_READ = ilitek_update_fw_read,
};

static ssize_t ilitek_firmware_version_read(struct file *fp, char __user *buf,
					    size_t size, loff_t *off)
{
	int error;
	int cnt;
	uint8_t str[256];

	if (*off)
		return 0;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	error = api_update_ts_info(ts->dev);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	if (error < 0)
		cnt = scnprintf(str, sizeof(str), "read failed, err: %d\n",
				error);
	else
		cnt = scnprintf(str, sizeof(str), "fw-version-tag: [%*phD]\n",
				8, ts->dev->fw_ver); 

	if (copy_to_user(buf, str, cnt))
		return -EFAULT;

	*off += cnt;

	return cnt;
}

static struct PROC_FOPS_T ilitek_fops_fwversion = {
	.PROC_READ = ilitek_firmware_version_read,
};

static ssize_t ilitek_console_write(struct file *filp, const char *buf,
				    size_t size, loff_t *f_pos)
{
	int error;
	char tmp[256], str[256];
	char *ptr, *cur = str;
	size_t wlen, rlen, _wlen = 0;
	uint8_t cmd[64], data[64];

	if (size > sizeof(tmp)) {
		TP_ERR(NULL, "invalid buf len: %zu > %zu too large\n",
			size, sizeof(tmp));
		return -EINVAL;
	}

	if ((error = copy_from_user(tmp, buf, size)) < 0) {
		TP_ERR(NULL, "copy_from_user failed, err: %d", error);
		return error;
	}

	memset(str, 0, sizeof(str));
	sscanf(tmp, "%zu:%zu:%s", &wlen, &rlen, str);
	TP_MSG(NULL, "wlen: %zu, rlen: %zu, command: %s\n", wlen, rlen, str);

	while ((ptr = strsep(&cur, "-")) && _wlen < sizeof(cmd))
		sscanf(ptr, "%hhx", &cmd[_wlen++]);

	if (wlen > 0 && wlen != _wlen) {
		TP_ERR(NULL, "write cmd length: %zu not match with %s\n",
			wlen, str);
		return -EINVAL;
	} else if (rlen > sizeof(data)) {
		TP_ERR(NULL, "invalid read cmd length: %zu > %zu too large\n",
			rlen, sizeof(data));
		return -EINVAL;
	}

	if (wlen > 0)
		TP_MSG(NULL, "[write]: %*phD, len: %zu\n",
			(int)wlen, cmd, wlen);

	if ((error = ilitek_write_and_read(cmd, wlen, 1, data, rlen)) < 0)
		return error;

	if (rlen > 0)
		TP_MSG(NULL, "[read]: %*phD, len: %zu\n",
			(int)rlen, data, rlen);

	return size;
}

static struct PROC_FOPS_T ilitek_fops_console = {
	.PROC_WRITE = ilitek_console_write,
};

static ssize_t ilitek_func_mode_write(struct file *fp, const char *buf,
				      size_t size, loff_t *off)
{
	int error, cnt;
	char str[64];
	uint8_t func_mode;

	cnt = MIN(size, sizeof(str));
	memset(str, 0, sizeof(str));
	if (copy_from_user(str, buf, cnt))
		return -EFAULT;

	sscanf(str, "%hhu", &func_mode);
	TP_MSG(NULL, "set func mode: %hhu, support max: %hhu modes\n",
		func_mode, ts->dev->tp_info.support_modes);

	if (func_mode >= ts->dev->tp_info.support_modes)
		return -EINVAL;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	error = api_set_ctrl_mode(ts->dev, mode_suspend, false);
	error |= api_set_func_mode(ts->dev, func_mode);
	error |= api_set_ctrl_mode(ts->dev, mode_normal, false);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	return (error < 0) ? error : cnt;
}

static ssize_t ilitek_func_mode_read(struct file *fp, char __user *buf,
				     size_t size, loff_t *off)
{
	int cnt = 0;
	uint8_t i;
	uint8_t str[256];

	if (*off)
		return 0;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	api_set_ctrl_mode(ts->dev, mode_suspend, false);
	api_protocol_set_cmd(ts->dev, GET_TP_INFO, NULL);
	api_get_func_mode(ts->dev);
	api_set_ctrl_mode(ts->dev, mode_normal, false);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	memset(str, 0, sizeof(str));
	cnt += scnprintf(str, sizeof(str), "function mode: ");
	for (i = 0; i < ts->dev->tp_info.support_modes; i++) {
		if (i == ts->dev->func_mode)
			cnt += scnprintf(str + strlen(str), sizeof(str) - cnt,
					 "[%hhu] ", i);
		else
			cnt += scnprintf(str + strlen(str), sizeof(str) - cnt,
					 "%hhu ", i);
	}
	cnt += scnprintf(str + strlen(str), sizeof(str) - cnt, "\n");

	if (copy_to_user(buf, str, cnt))
		return -EFAULT;

	*off += cnt;
	
	return cnt;
}

static struct PROC_FOPS_T ilitek_fops_func_mode = {
	.PROC_READ = ilitek_func_mode_read,
	.PROC_WRITE = ilitek_func_mode_write,
};

static ssize_t ilitek_crypto_key_write(struct file *fp, const char *buf,
				       size_t size, loff_t *off)
{
	uint8_t crypto[AES_KEY_LEN * 2];

	if (size != sizeof(crypto) ||
	    copy_from_user(crypto, buf, sizeof(crypto)))
		return -EFAULT;

	TP_MSG(NULL, "set crypto_key: %*phD\n", AES_KEY_LEN, crypto);
	TP_MSG(NULL, "set crypto_iv: %*phD\n", AES_KEY_LEN,
		crypto + AES_KEY_LEN);

	memcpy(crypto_key, crypto, AES_KEY_LEN);
	memcpy(crypto_iv, crypto + AES_KEY_LEN, AES_KEY_LEN);

	return sizeof(crypto);
}

static ssize_t ilitek_crypto_key_read(struct file *fp, char __user *buf,
				      size_t size, loff_t *off)
{
	int cnt = 0;
	uint8_t str[256];

	if (*off)
		return 0;

	memset(str, 0, sizeof(str));
	cnt += scnprintf(str, sizeof(str), "key: %*phD\n",
			 AES_KEY_LEN, crypto_key);
	cnt += scnprintf(str + strlen(str), sizeof(str), "iv: %*phD\n",
			 AES_KEY_LEN, crypto_iv);

	if (copy_to_user(buf, str, cnt))
		return -EFAULT;

	*off += cnt;
	
	return cnt;
}

static struct PROC_FOPS_T ilitek_fops_crypto_key = {
	.PROC_READ = ilitek_crypto_key_read,
	.PROC_WRITE = ilitek_crypto_key_write,
};

static ssize_t ilitek_setmode_0_read(struct file *fp, char __user *buf,
				     size_t size, loff_t *off)
{
	int error;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	error = api_set_ctrl_mode(ts->dev, mode_suspend, false);
	error |= api_set_func_mode(ts->dev, 0);
	error |= api_set_ctrl_mode(ts->dev, mode_normal, false);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	return 0;
}

static struct PROC_FOPS_T ilitek_fops_setmode_0 = {
	.PROC_READ = ilitek_setmode_0_read,
};

static ssize_t ilitek_setmode_1_read(struct file *fp, char __user *buf,
				     size_t size, loff_t *off)
{
	int error;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	error = api_set_ctrl_mode(ts->dev, mode_suspend, false);
	error |= api_set_func_mode(ts->dev, 1);
	error |= api_set_ctrl_mode(ts->dev, mode_normal, false);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	return 0;
}

static struct PROC_FOPS_T ilitek_fops_setmode_1 = {
	.PROC_READ = ilitek_setmode_1_read,
};

static ssize_t ilitek_setmode_2_read(struct file *fp, char __user *buf,
				     size_t size, loff_t *off)
{
	int error;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	error = api_set_ctrl_mode(ts->dev, mode_suspend, false);
	error |= api_set_func_mode(ts->dev, 2);
	error |= api_set_ctrl_mode(ts->dev, mode_normal, false);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	return 0;
}

static struct PROC_FOPS_T ilitek_fops_setmode_2 = {
	.PROC_READ = ilitek_setmode_2_read,
};

static ssize_t ilitek_eds_check_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	if (ts->esd_check)
		return scnprintf(buf, PAGE_SIZE, "[enable] disable\n");

	return scnprintf(buf, PAGE_SIZE, "enable [disable]\n");
}

static ssize_t ilitek_esd_check_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	TP_MSG(NULL, "set esd check: %hhx to %s\n", ts->esd_check, buf);

	if (!strncmp(buf, "enable", 6) && !ts->esd_check) {
		ilitek_create_esd_check_workqueue();
		ts->esd_check = true;
	} else if (!strncmp(buf, "disable", 7) && ts->esd_check) {
		ilitek_remove_esd_check_workqueue();
		ts->esd_check = false;
	}

	return size;
}
static DEVICE_ATTR(esd_check, 0664,
		   ilitek_eds_check_show,
		   ilitek_esd_check_store);


static ssize_t ilitek_low_power_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t cnt = 0;

#define X(_type, _id, _str)	{.str = _str, .id = _id},
	struct {
		char *str;
		uint8_t id;
	} modes[] = { ILITEK_LOW_POWER_TYPES };
#undef X


	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (modes[i].id == ts->low_power_status) {
			cnt += scnprintf(buf + strlen(buf), PAGE_SIZE - cnt,
					 "[%s] ", modes[i].str);
			continue;
		}

		cnt += scnprintf(buf + strlen(buf), PAGE_SIZE - cnt,
				 "%s ", modes[i].str);
	}

 	cnt += scnprintf(buf + strlen(buf), PAGE_SIZE - cnt, "\n");

	return cnt;
}

static ssize_t ilitek_low_power_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	int i;

#define X(_type, _id, _str)	{.str = _str, .id = _id},
	struct {
		char *str;
		uint8_t id;
	} modes[] = { ILITEK_LOW_POWER_TYPES };
#undef X

	TP_MSG(NULL, "set low power: %hhx to %s\n", ts->low_power_status, buf);

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strncmp(buf, modes[i].str, strlen(modes[i].str)))
			continue;
		ts->low_power_status = modes[i].id;
	}

	return size;
}
static DEVICE_ATTR(low_power, 0664,
		   ilitek_low_power_show,
		   ilitek_low_power_store);

static ssize_t ilitek_gesture_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int i;
	ssize_t cnt = 0;

#define X(_type, _id, _str)	{.str = _str, .id = _id},
	struct {
		char *str;
		uint8_t id;
	} modes[] = { ILITEK_GESTURE_TYPES };
#undef X


	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (modes[i].id == ts->gesture_status) {
			cnt += scnprintf(buf + strlen(buf), PAGE_SIZE - cnt,
					 "[%s] ", modes[i].str);
			continue;
		}

		cnt += scnprintf(buf + strlen(buf), PAGE_SIZE - cnt,
				 "%s ", modes[i].str);
	}

 	cnt += scnprintf(buf + strlen(buf), PAGE_SIZE - cnt, "\n");

	return cnt;
}

static ssize_t ilitek_gesture_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	int i;
	uint8_t type = ts->gesture_status;

#define X(_type, _id, _str)	{.str = _str, .id = _id},
	struct {
		char *str;
		uint8_t id;
	} modes[] = { ILITEK_GESTURE_TYPES };
#undef X

	TP_MSG(NULL, "set gesture: %hhx to %s\n", ts->gesture_status, buf);

	for (i = 0; i < ARRAY_SIZE(modes); i++) {
		if (strncmp(buf, modes[i].str, strlen(modes[i].str)))
			continue;
		type = modes[i].id;
	}

	if (!ts->gesture_status && type)
		ilitek_register_gesture(ts, true);
	else if (ts->gesture_status && !type)
		ilitek_register_gesture(ts, false);

	ts->gesture_status = type;

	return size;
}
static DEVICE_ATTR(gesture, 0664, ilitek_gesture_show, ilitek_gesture_store);

static ssize_t ilitek_firmware_version_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	int ret;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	ret = api_update_ts_info(ts->dev);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();

	if (ret < 0) {
		TP_ERR(NULL, "read tp info failed, err: %d\n", ret);
		return scnprintf(buf, PAGE_SIZE, "read failed, err: %d\n", ret);
	}

	return scnprintf(buf, PAGE_SIZE, "fw-version-tag: [%*phD]\n",
			 8, ts->dev->fw_ver);
}

static DEVICE_ATTR(firmware_version, 0664, ilitek_firmware_version_show, NULL);

static ssize_t ilitek_module_name_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	int error;

	ilitek_irq_disable();
	mutex_lock(&ts->ilitek_mutex);
	if ((error = api_set_ctrl_mode(ts->dev, mode_suspend, false)) < 0 ||
	    (error = api_protocol_set_cmd(ts->dev, GET_MCU_VER, NULL)) < 0 ||
	    (error = api_set_ctrl_mode(ts->dev, mode_normal, false)) < 0)
		TP_ERR(NULL, "read mcu ver. failed, err: %d\n", error);
	mutex_unlock(&ts->ilitek_mutex);
	ilitek_irq_enable();


	if (error < 0)
		return scnprintf(buf, PAGE_SIZE, "read failed, err: %d\n",
				 error);
	
	return scnprintf(buf, PAGE_SIZE, "module-name-tag: [%s]\n",
			 ts->dev->mcu_info.module_name);
}

static DEVICE_ATTR(product_id, 0664, ilitek_module_name_show, NULL);

static ssize_t ilitek_update_fw_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int error;

	if ((error = ilitek_upgrade_firmware("ilitek.hex")) < 0 &&
	    (error = ilitek_upgrade_firmware("ilitek.bin")) < 0)
		return scnprintf(buf, PAGE_SIZE, "upgrade failed, err: %d\n",
				 error);

	return scnprintf(buf, PAGE_SIZE,
			 "upgrade success, current fw version: %*phD\n",
			 8, ts->dev->fw_ver);
}

static DEVICE_ATTR(update_fw, 0664, ilitek_update_fw_show, NULL);

static struct attribute *ilitek_sysfs_attrs_ctrl[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_product_id.attr,
	&dev_attr_gesture.attr,
	&dev_attr_low_power.attr,
	&dev_attr_esd_check.attr,
	&dev_attr_update_fw.attr,
	NULL
};

static struct attribute_group ilitek_attribute_group[] = {
	{.attrs = ilitek_sysfs_attrs_ctrl},
};

int ilitek_create_sysfsnode(void)
{
	int error;

	error = sysfs_create_group(&ts->device->kobj, ilitek_attribute_group);
	if (error < 0) {
		TP_ERR(NULL, "sysfs_create_group failed, err: %d\n", error);
		return error;
	}

	return 0;
}

void ilitek_remove_sys_node(void)
{
	sysfs_remove_group(&ts->device->kobj, ilitek_attribute_group);
}

int ilitek_create_tool_node(void)
{
	int ret;

#ifdef ILITEK_TOOL
	ret = alloc_chrdev_region(&ilitek_dev.devno, 0, 1, "ilitek_file");
	if (ret) {
		TP_ERR(NULL, "can't allocate chrdev\n");
	} else {
		TP_MSG(NULL, "register chrdev(%d, %d)\n", MAJOR(ilitek_dev.devno), MINOR(ilitek_dev.devno));

		// initialize character device driver
		cdev_init(&ilitek_dev.cdev, &ilitek_fops);
		ilitek_dev.cdev.owner = THIS_MODULE;
		ret = cdev_add(&ilitek_dev.cdev, ilitek_dev.devno, 1);
		if (ret < 0) {
			TP_ERR(NULL, "add character device error, ret %d\n", ret);
		} else {
			ilitek_dev.class = class_create(THIS_MODULE, "ilitek_file");
			if (IS_ERR(ilitek_dev.class))
				TP_ERR(NULL, "create class, error\n");

			device_create(ilitek_dev.class, NULL, ilitek_dev.devno, NULL, "ilitek_ctrl");
		}
	}

	ilitek_proc = proc_create("ilitek_ctrl", ILITEK_DEVICE_NODE_PERMISSON,
				  NULL, &ilitek_proc_fops);

	if (!ilitek_proc)
		TP_ERR(NULL, "proc_create(ilitek_ctrl, ILITEK_DEVICE_NODE_PERMISSON, NULL, &ilitek_fops) fail\n");

	ilitek_proc_entry = proc_mkdir("ilitek", NULL);
	if (!ilitek_proc_entry) {
		TP_ERR(NULL, "Error, failed to creat procfs.\n");
		return -EINVAL;
	}

	if (!proc_create("firmware_version", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_fwversion))
		TP_ERR(NULL, "failed to create procfs firmware_version.\n");

	if (!proc_create("console", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_console))
		TP_ERR(NULL, "failed to create procfs console.\n");

	if (!proc_create("update_fw", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_fwupdate))
		TP_ERR(NULL, "failed to create procfs update_fw.\n");

	if (!proc_create("func_mode", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_func_mode))
		TP_ERR(NULL, "failed to create procfs func_mode.\n");

	if (!proc_create("crypto_key", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_crypto_key))
		TP_ERR(NULL, "failed to create procfs crypto_key.\n");

	/*
	 * below setmode_X is historical setting for some customer need
	 * to set function mode by cat procfs node.
	 * please make sure it's not risky to modifiy below.
	 */
	if (!proc_create("setmode_0", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_setmode_0))
		TP_ERR(NULL, "failed to create procfs setmode_0.\n");
	if (!proc_create("setmode_1", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_setmode_1))
		TP_ERR(NULL, "failed to create procfs setmode_1.\n");
	if (!proc_create("setmode_2", ILITEK_DEVICE_NODE_PERMISSON,
			 ilitek_proc_entry, &ilitek_fops_setmode_2))
		TP_ERR(NULL, "failed to create procfs setmode_2.\n");
#endif

	return 0;
}

int ilitek_remove_tool_node(void)
{
#ifdef ILITEK_TOOL
	cdev_del(&ilitek_dev.cdev);
	unregister_chrdev_region(ilitek_dev.devno, 1);
	device_destroy(ilitek_dev.class, ilitek_dev.devno);
	class_destroy(ilitek_dev.class);
	if (ilitek_proc) {
		TP_MSG(NULL, "remove procfs ilitek_ctrl.\n");
		remove_proc_entry("ilitek_ctrl", NULL);
		ilitek_proc = NULL;
	}

	if (ilitek_proc_entry) {
		TP_MSG(NULL, "remove procfs inode\n");
		remove_proc_entry("firmware_version", ilitek_proc_entry);
		remove_proc_entry("console", ilitek_proc_entry);
		remove_proc_entry("update_fw", ilitek_proc_entry);
		remove_proc_entry("func_mode", ilitek_proc_entry);
		remove_proc_entry("crypto_key", ilitek_proc_entry);

		remove_proc_entry("setmode_0", ilitek_proc_entry);
		remove_proc_entry("setmode_1", ilitek_proc_entry);
		remove_proc_entry("setmode_2", ilitek_proc_entry);

		remove_proc_entry("ilitek", NULL);
		ilitek_proc_entry = NULL;
	}
#endif
	return 0;
}
