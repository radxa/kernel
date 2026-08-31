// SPDX-License-Identifier: GPL-2.0-only
/*
 * Radxa SVC GLINK driver.
 *
 * Copyright (c) 2026 Radxa Computer (Shenzhen) Co., Ltd.
 */

#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#define RADXA_SVC_MAGIC		0x58444152 /* "RADX" */
#define RADXA_SVC_VERSION	1
#define RADXA_SVC_TIMEOUT	msecs_to_jiffies(5000)

#define RADXA_SVC_MAX_TX_PAYLOAD	64
#define RADXA_SVC_MAX_RX_PAYLOAD	2048

#define RADXA_SVC_OP_GET_VERSION	0x02
#define RADXA_SVC_OP_SET_PROFILE	0x10
#define RADXA_SVC_OP_GET_PROFILE	0x11
#define RADXA_SVC_OP_FAN_GET_STATE	0x50
#define RADXA_SVC_OP_FAN_SET_CONTROL	0x51
#define RADXA_SVC_OP_FAN_GET_CONTROL	0x52
#define RADXA_SVC_OP_SENSOR_LIST		0x60
#define RADXA_SVC_OP_SENSOR_READ		0x61

#define RADXA_SVC_PROFILE_QUIET		0
#define RADXA_SVC_PROFILE_PERFORMANCE	1

#define RADXA_SVC_FAN_CONTROL_FULL_SPEED	0
#define RADXA_SVC_FAN_CONTROL_MANUAL		1
#define RADXA_SVC_FAN_CONTROL_AUTO		2
#define RADXA_SVC_FAN_PWM_MAX			255

#define RADXA_SVC_PWM_MODE_FULL_SPEED		0
#define RADXA_SVC_PWM_MODE_MANUAL		1
#define RADXA_SVC_PWM_MODE_AUTO_QUIET		2
#define RADXA_SVC_PWM_MODE_AUTO_PERFORMANCE	3

#define RADXA_SVC_CAP_PROFILE		BIT(1)
#define RADXA_SVC_CAP_FANCTL		BIT(5)
#define RADXA_SVC_CAP_FANCTL_CTRL	BIT(6)
#define RADXA_SVC_CAP_SENSORS		BIT(7)

#define RADXA_SVC_REQUIRED_CAPS		(RADXA_SVC_CAP_PROFILE | \
					 RADXA_SVC_CAP_FANCTL | \
					 RADXA_SVC_CAP_FANCTL_CTRL)

#define RADXA_SVC_SENSOR_MAX_SENSORS	64
#define RADXA_SVC_SENSOR_NAME_LEN	32
#define RADXA_SVC_SENSOR_PAGE_MAX	5
#define RADXA_SVC_SENSOR_CACHE_TIME	msecs_to_jiffies(50)

#define RADXA_SVC_SENSOR_TEMP		1
#define RADXA_SVC_SENSOR_VOLTAGE		2
#define RADXA_SVC_SENSOR_CURRENT		3
#define RADXA_SVC_SENSOR_VOLTAGE_CURRENT	4

#define RADXA_SVC_SENSOR_VALID_TEMP	BIT(0)
#define RADXA_SVC_SENSOR_VALID_VOLTAGE	BIT(1)
#define RADXA_SVC_SENSOR_VALID_CURRENT	BIT(2)
#define RADXA_SVC_SENSOR_VALID_MASK	(RADXA_SVC_SENSOR_VALID_TEMP | \
					 RADXA_SVC_SENSOR_VALID_VOLTAGE | \
					 RADXA_SVC_SENSOR_VALID_CURRENT)

struct radxa_svc_hdr {
	__le32 magic;
	__le16 version;
	__le16 header_size;
	__le32 opcode;
	__le32 seq;
	__le32 status;
	__le32 payload_len;
} __packed;

struct radxa_svc_version_resp {
	__le16 major;
	__le16 minor;
	__le32 caps;
} __packed;

struct radxa_svc_fan_state_resp {
	__le32 profile;
	__le32 running;
	__le32 emergency;
	__le32 cpu_valid;
	__le32 cpu_stale_ticks;
	__le32 cpu_temp_deci_c;
	__le32 gpu_valid;
	__le32 gpu_stale_ticks;
	__le32 gpu_temp_deci_c;
	__le32 current_duty_ns;
	__le32 target_duty_ns;
	__le32 pwm_channel;
	__le32 pwm_period_ns;
	__le32 loop_count;
	__le32 fault_count;
	__le32 last_status;
	__le32 control_mode;
	__le32 manual_pwm;
} __packed;

struct radxa_svc_fan_control_req {
	__le32 control_mode;
	__le32 manual_pwm;
} __packed;

struct radxa_svc_fan_control_resp {
	__le32 control_mode;
	__le32 manual_pwm;
} __packed;

struct radxa_svc_sensor_list_req {
	__le32 start_index;
	__le32 max_entries;
} __packed;

struct radxa_svc_sensor_desc {
	__le32 sensor_id;
	__le32 sensor_type;
	char name[RADXA_SVC_SENSOR_NAME_LEN];
} __packed;

struct radxa_svc_sensor_list_resp {
	__le32 total_count;
	__le32 returned_count;
	struct radxa_svc_sensor_desc entries[];
} __packed;

struct radxa_svc_sensor_read_req {
	__le32 sensor_id;
} __packed;

struct radxa_svc_sensor_read_resp {
	__le32 sensor_id;
	__le32 valid_mask;
	__le32 temp_millic;
	__le32 voltage_mv;
	__le32 current_ma;
} __packed;

struct radxa_svc_glink;

struct radxa_svc_sensor {
	struct radxa_svc_glink *svc;
	struct device *hwmon_dev;
	struct mutex cache_lock; /* protects cached sample fields */
	unsigned long last_updated;
	u32 sensor_id;
	u32 sensor_type;
	char name[RADXA_SVC_SENSOR_NAME_LEN];
	s32 temp_millic;
	s32 voltage_mv;
	s64 current_ma;
	bool cache_valid;
};

struct radxa_svc_glink {
	struct device *dev;
	struct rpmsg_device *rpdev;
	struct device *hwmon_dev;
	struct radxa_svc_sensor *sensors;
	u32 num_sensors;

	struct mutex xfer_lock; /* serializes request/response transactions */
	struct mutex fan_lock; /* serializes multi-request fan configuration */
	spinlock_t rsp_lock;
	struct completion rsp;
	bool pending;
	bool shutting_down;
	u32 pending_seq;
	u32 seq;

	u32 rsp_opcode;
	int rsp_status;
	size_t rsp_len;
	u8 rsp_payload[RADXA_SVC_MAX_RX_PAYLOAD];
	u32 caps;
};

static int radxa_svc_request(struct radxa_svc_glink *svc, u32 opcode,
			     const void *req_payload, size_t req_len,
			     void *rsp_payload, size_t *rsp_len);

static int radxa_svc_get_version(struct radxa_svc_glink *svc,
				 struct radxa_svc_version_resp *resp)
{
	size_t len = sizeof(*resp);
	int ret;

	ret = radxa_svc_request(svc, RADXA_SVC_OP_GET_VERSION, NULL, 0,
				resp, &len);
	if (ret)
		return ret;

	if (len < sizeof(*resp))
		return -EIO;

	return 0;
}

static int radxa_svc_check_version(struct device *dev,
				   const struct radxa_svc_version_resp *resp,
				   u32 *caps)
{
	u32 missing_caps;
	u16 major;

	major = le16_to_cpu(resp->major);
	*caps = le32_to_cpu(resp->caps);

	if (major != RADXA_SVC_VERSION)
		return dev_err_probe(dev, -EPROTONOSUPPORT,
				     "unsupported service major version %u\n",
				     major);

	missing_caps = RADXA_SVC_REQUIRED_CAPS & ~*caps;
	if (missing_caps)
		return dev_err_probe(dev, -ENODEV,
				     "service missing required caps 0x%08x\n",
				     missing_caps);

	return 0;
}

static int radxa_svc_get_profile(struct radxa_svc_glink *svc, u32 *profile)
{
	__le32 resp;
	size_t len = sizeof(resp);
	int ret;

	ret = radxa_svc_request(svc, RADXA_SVC_OP_GET_PROFILE, NULL, 0,
				&resp, &len);
	if (ret)
		return ret;

	if (len < sizeof(resp))
		return -EIO;

	*profile = le32_to_cpu(resp);

	return 0;
}

static int radxa_svc_set_profile(struct radxa_svc_glink *svc, u32 profile)
{
	__le32 payload;

	switch (profile) {
	case RADXA_SVC_PROFILE_QUIET:
	case RADXA_SVC_PROFILE_PERFORMANCE:
		break;
	default:
		return -EINVAL;
	}

	payload = cpu_to_le32(profile);

	return radxa_svc_request(svc, RADXA_SVC_OP_SET_PROFILE, &payload,
				 sizeof(payload), NULL, NULL);
}

static int radxa_svc_fan_get_state(struct radxa_svc_glink *svc,
				   struct radxa_svc_fan_state_resp *resp)
{
	size_t len = sizeof(*resp);
	size_t base_len = offsetof(struct radxa_svc_fan_state_resp,
				   control_mode);
	int ret;

	ret = radxa_svc_request(svc, RADXA_SVC_OP_FAN_GET_STATE, NULL, 0,
				resp, &len);
	if (ret)
		return ret;

	if (len < base_len)
		return -EIO;

	return 0;
}

static int radxa_svc_fan_get_control(struct radxa_svc_glink *svc,
				     struct radxa_svc_fan_control_resp *resp)
{
	size_t len = sizeof(*resp);
	u32 control_mode;
	u32 manual_pwm;
	int ret;

	ret = radxa_svc_request(svc, RADXA_SVC_OP_FAN_GET_CONTROL, NULL, 0,
				resp, &len);
	if (ret)
		return ret;

	if (len < sizeof(*resp))
		return -EIO;

	control_mode = le32_to_cpu(resp->control_mode);
	manual_pwm = le32_to_cpu(resp->manual_pwm);
	if (control_mode > RADXA_SVC_FAN_CONTROL_AUTO ||
	    manual_pwm > RADXA_SVC_FAN_PWM_MAX)
		return -EIO;

	return 0;
}

static int radxa_svc_fan_set_control(struct radxa_svc_glink *svc,
				     u32 control_mode, u32 manual_pwm)
{
	struct radxa_svc_fan_control_req req;

	if (control_mode > RADXA_SVC_FAN_CONTROL_AUTO ||
	    manual_pwm > RADXA_SVC_FAN_PWM_MAX)
		return -EINVAL;

	req.control_mode = cpu_to_le32(control_mode);
	req.manual_pwm = cpu_to_le32(manual_pwm);

	return radxa_svc_request(svc, RADXA_SVC_OP_FAN_SET_CONTROL, &req,
				 sizeof(req), NULL, NULL);
}

static int radxa_svc_fan_duty_to_pwm(u32 period_ns, u32 duty_ns, u32 *value)
{
	u64 pwm;

	if (!period_ns)
		return -ENODATA;

	if (duty_ns > period_ns)
		return -EPROTO;

	pwm = (u64)(period_ns - duty_ns) * RADXA_SVC_FAN_PWM_MAX;
	*value = min_t(u64, DIV_ROUND_CLOSEST_ULL(pwm, period_ns),
		       RADXA_SVC_FAN_PWM_MAX);

	return 0;
}

static int radxa_svc_request(struct radxa_svc_glink *svc, u32 opcode,
			     const void *req_payload, size_t req_len,
			     void *rsp_payload, size_t *rsp_len)
{
	struct radxa_svc_hdr *hdr;
	unsigned long flags;
	size_t tx_len;
	u32 seq;
	u8 *tx_buf;
	int ret;

	if (req_len > RADXA_SVC_MAX_TX_PAYLOAD)
		return -EMSGSIZE;

	tx_len = sizeof(*hdr) + req_len;
	tx_buf = kzalloc(tx_len, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;

	mutex_lock(&svc->xfer_lock);

	if (!svc->rpdev || !svc->rpdev->ept) {
		ret = -ENODEV;
		goto out_unlock;
	}

	seq = ++svc->seq;
	if (!seq)
		seq = ++svc->seq;

	hdr = (struct radxa_svc_hdr *)tx_buf;
	hdr->magic = cpu_to_le32(RADXA_SVC_MAGIC);
	hdr->version = cpu_to_le16(RADXA_SVC_VERSION);
	hdr->header_size = cpu_to_le16(sizeof(*hdr));
	hdr->opcode = cpu_to_le32(opcode);
	hdr->seq = cpu_to_le32(seq);
	hdr->status = cpu_to_le32(0);
	hdr->payload_len = cpu_to_le32(req_len);

	if (req_len)
		memcpy(tx_buf + sizeof(*hdr), req_payload, req_len);

	reinit_completion(&svc->rsp);

	spin_lock_irqsave(&svc->rsp_lock, flags);
	if (svc->shutting_down) {
		spin_unlock_irqrestore(&svc->rsp_lock, flags);
		ret = -ENODEV;
		goto out_unlock;
	}

	svc->pending = true;
	svc->pending_seq = seq;
	svc->rsp_opcode = 0;
	svc->rsp_status = 0;
	svc->rsp_len = 0;
	spin_unlock_irqrestore(&svc->rsp_lock, flags);

	ret = rpmsg_send(svc->rpdev->ept, tx_buf, tx_len);
	if (ret) {
		spin_lock_irqsave(&svc->rsp_lock, flags);
		svc->pending = false;
		spin_unlock_irqrestore(&svc->rsp_lock, flags);
		goto out_unlock;
	}

	if (!wait_for_completion_timeout(&svc->rsp, RADXA_SVC_TIMEOUT)) {
		spin_lock_irqsave(&svc->rsp_lock, flags);
		svc->pending = false;
		spin_unlock_irqrestore(&svc->rsp_lock, flags);
		ret = -ETIMEDOUT;
		goto out_unlock;
	}

	spin_lock_irqsave(&svc->rsp_lock, flags);
	if (svc->shutting_down) {
		ret = -ENODEV;
	} else if (svc->rsp_opcode != opcode) {
		ret = -EIO;
	} else {
		ret = svc->rsp_status;
		if (rsp_payload && rsp_len) {
			size_t copy_len = min(*rsp_len, svc->rsp_len);

			memcpy(rsp_payload, svc->rsp_payload, copy_len);
			if (*rsp_len < svc->rsp_len && !ret)
				ret = -EMSGSIZE;
			*rsp_len = svc->rsp_len;
		}
	}
	spin_unlock_irqrestore(&svc->rsp_lock, flags);

out_unlock:
	mutex_unlock(&svc->xfer_lock);
	kfree(tx_buf);

	return ret;
}

static int radxa_svc_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				    int len, void *priv, u32 addr)
{
	struct radxa_svc_glink *svc = dev_get_drvdata(&rpdev->dev);
	const struct radxa_svc_hdr *hdr = data;
	unsigned long flags;
	size_t header_size;
	size_t payload_len;
	bool do_complete = false;
	u32 seq;

	if (len < sizeof(*hdr))
		goto bad_msg;

	if (le32_to_cpu(hdr->magic) != RADXA_SVC_MAGIC ||
	    le16_to_cpu(hdr->version) != RADXA_SVC_VERSION)
		goto bad_msg;

	header_size = le16_to_cpu(hdr->header_size);
	if (header_size < sizeof(*hdr) || header_size > len)
		goto bad_msg;

	payload_len = le32_to_cpu(hdr->payload_len);
	if (payload_len > len - header_size ||
	    payload_len > RADXA_SVC_MAX_RX_PAYLOAD)
		goto bad_msg;

	seq = le32_to_cpu(hdr->seq);

	spin_lock_irqsave(&svc->rsp_lock, flags);
	if (svc->pending && seq == svc->pending_seq) {
		svc->rsp_opcode = le32_to_cpu(hdr->opcode);
		svc->rsp_status = (s32)le32_to_cpu(hdr->status);
		svc->rsp_len = payload_len;
		memcpy(svc->rsp_payload, data + header_size, payload_len);
		svc->pending = false;
		do_complete = true;
	}
	spin_unlock_irqrestore(&svc->rsp_lock, flags);

	if (do_complete)
		complete(&svc->rsp);

	return 0;

bad_msg:
	return 0;
}

static u32 radxa_svc_sensor_expected_mask(u32 sensor_type)
{
	switch (sensor_type) {
	case RADXA_SVC_SENSOR_TEMP:
		return RADXA_SVC_SENSOR_VALID_TEMP;
	case RADXA_SVC_SENSOR_VOLTAGE:
		return RADXA_SVC_SENSOR_VALID_VOLTAGE;
	case RADXA_SVC_SENSOR_CURRENT:
		return RADXA_SVC_SENSOR_VALID_CURRENT;
	case RADXA_SVC_SENSOR_VOLTAGE_CURRENT:
		return RADXA_SVC_SENSOR_VALID_VOLTAGE |
		       RADXA_SVC_SENSOR_VALID_CURRENT;
	default:
		return 0;
	}
}

static int radxa_svc_sensor_update(struct radxa_svc_sensor *sensor)
{
	struct radxa_svc_sensor_read_req req;
	struct radxa_svc_sensor_read_resp resp = {};
	unsigned long cache_expires;
	u32 expected_mask;
	u32 valid_mask;
	size_t len = sizeof(resp);
	int ret;

	mutex_lock(&sensor->cache_lock);

	cache_expires = sensor->last_updated + RADXA_SVC_SENSOR_CACHE_TIME;
	if (sensor->cache_valid && time_before(jiffies, cache_expires)) {
		ret = 0;
		goto out_unlock;
	}

	req.sensor_id = cpu_to_le32(sensor->sensor_id);
	ret = radxa_svc_request(sensor->svc, RADXA_SVC_OP_SENSOR_READ,
				&req, sizeof(req), &resp, &len);
	if (ret)
		goto out_unlock;

	if (len != sizeof(resp) ||
	    le32_to_cpu(resp.sensor_id) != sensor->sensor_id) {
		ret = -EPROTO;
		goto out_unlock;
	}

	valid_mask = le32_to_cpu(resp.valid_mask);
	expected_mask = radxa_svc_sensor_expected_mask(sensor->sensor_type);
	if ((valid_mask & RADXA_SVC_SENSOR_VALID_MASK) != expected_mask ||
	    valid_mask & ~RADXA_SVC_SENSOR_VALID_MASK) {
		ret = -EPROTO;
		goto out_unlock;
	}

	sensor->temp_millic = (s32)le32_to_cpu(resp.temp_millic);
	sensor->voltage_mv = (s32)le32_to_cpu(resp.voltage_mv);
	sensor->current_ma = (s32)le32_to_cpu(resp.current_ma);
	sensor->last_updated = jiffies;
	sensor->cache_valid = true;
	ret = 0;

out_unlock:
	mutex_unlock(&sensor->cache_lock);
	return ret;
}

static int radxa_svc_sensor_hwmon_read(struct device *dev,
				       enum hwmon_sensor_types type, u32 attr,
				       int channel, long *val)
{
	struct radxa_svc_sensor *sensor = dev_get_drvdata(dev);
	s64 power;
	int ret;

	if (channel)
		return -EOPNOTSUPP;

	ret = radxa_svc_sensor_update(sensor);
	if (ret)
		return ret;

	switch (type) {
	case hwmon_temp:
		if (attr != hwmon_temp_input ||
		    sensor->sensor_type != RADXA_SVC_SENSOR_TEMP)
			return -EOPNOTSUPP;
		*val = sensor->temp_millic;
		return 0;
	case hwmon_in:
		if (attr != hwmon_in_input ||
		    !(radxa_svc_sensor_expected_mask(sensor->sensor_type) &
		      RADXA_SVC_SENSOR_VALID_VOLTAGE))
			return -EOPNOTSUPP;
		*val = sensor->voltage_mv;
		return 0;
	case hwmon_curr:
		if (attr != hwmon_curr_input ||
		    !(radxa_svc_sensor_expected_mask(sensor->sensor_type) &
		      RADXA_SVC_SENSOR_VALID_CURRENT))
			return -EOPNOTSUPP;
		if (sensor->current_ma > LONG_MAX ||
		    sensor->current_ma < LONG_MIN)
			return -ERANGE;
		*val = sensor->current_ma;
		return 0;
	case hwmon_power:
		if (attr != hwmon_power_input ||
		    sensor->sensor_type != RADXA_SVC_SENSOR_VOLTAGE_CURRENT)
			return -EOPNOTSUPP;

		power = (s64)sensor->voltage_mv * sensor->current_ma;
		if (power > LONG_MAX || power < LONG_MIN)
			return -ERANGE;
		*val = power;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int radxa_svc_sensor_hwmon_read_string(struct device *dev,
					      enum hwmon_sensor_types type, u32 attr,
					      int channel, const char **str)
{
	if (channel)
		return -EOPNOTSUPP;

	switch (type) {
	case hwmon_temp:
		if (attr == hwmon_temp_label)
			*str = "Temperature";
		else
			return -EOPNOTSUPP;
		break;
	case hwmon_in:
		if (attr == hwmon_in_label)
			*str = "Voltage";
		else
			return -EOPNOTSUPP;
		break;
	case hwmon_curr:
		if (attr == hwmon_curr_label)
			*str = "Current";
		else
			return -EOPNOTSUPP;
		break;
	case hwmon_power:
		if (attr == hwmon_power_label)
			*str = "Power";
		else
			return -EOPNOTSUPP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static umode_t radxa_svc_sensor_hwmon_is_visible(const void *data,
						 enum hwmon_sensor_types type,
						 u32 attr, int channel)
{
	const struct radxa_svc_sensor *sensor = data;

	if (channel)
		return 0;

	switch (type) {
	case hwmon_temp:
		if (sensor->sensor_type == RADXA_SVC_SENSOR_TEMP &&
		    (attr == hwmon_temp_input || attr == hwmon_temp_label))
			return 0444;
		break;
	case hwmon_in:
		if ((radxa_svc_sensor_expected_mask(sensor->sensor_type) &
		     RADXA_SVC_SENSOR_VALID_VOLTAGE) &&
		    (attr == hwmon_in_input || attr == hwmon_in_label))
			return 0444;
		break;
	case hwmon_curr:
		if ((radxa_svc_sensor_expected_mask(sensor->sensor_type) &
		     RADXA_SVC_SENSOR_VALID_CURRENT) &&
		    (attr == hwmon_curr_input || attr == hwmon_curr_label))
			return 0444;
		break;
	case hwmon_power:
		if (sensor->sensor_type == RADXA_SVC_SENSOR_VOLTAGE_CURRENT &&
		    (attr == hwmon_power_input || attr == hwmon_power_label))
			return 0444;
		break;
	default:
		break;
	}

	return 0;
}

static const struct hwmon_ops radxa_svc_sensor_hwmon_ops = {
	.is_visible = radxa_svc_sensor_hwmon_is_visible,
	.read = radxa_svc_sensor_hwmon_read,
	.read_string = radxa_svc_sensor_hwmon_read_string,
};

static const struct hwmon_channel_info * const radxa_svc_temp_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	NULL
};

static const struct hwmon_channel_info * const radxa_svc_voltage_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL),
	NULL
};

static const struct hwmon_channel_info * const radxa_svc_current_info[] = {
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_channel_info * const radxa_svc_voltage_current_info[] = {
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power, HWMON_P_INPUT | HWMON_P_LABEL),
	NULL
};

static const struct hwmon_chip_info radxa_svc_temp_chip_info = {
	.ops = &radxa_svc_sensor_hwmon_ops,
	.info = radxa_svc_temp_info,
};

static const struct hwmon_chip_info radxa_svc_voltage_chip_info = {
	.ops = &radxa_svc_sensor_hwmon_ops,
	.info = radxa_svc_voltage_info,
};

static const struct hwmon_chip_info radxa_svc_current_chip_info = {
	.ops = &radxa_svc_sensor_hwmon_ops,
	.info = radxa_svc_current_info,
};

static const struct hwmon_chip_info radxa_svc_voltage_current_chip_info = {
	.ops = &radxa_svc_sensor_hwmon_ops,
	.info = radxa_svc_voltage_current_info,
};

static const struct hwmon_chip_info *radxa_svc_sensor_chip_info(u32 sensor_type)
{
	switch (sensor_type) {
	case RADXA_SVC_SENSOR_TEMP:
		return &radxa_svc_temp_chip_info;
	case RADXA_SVC_SENSOR_VOLTAGE:
		return &radxa_svc_voltage_chip_info;
	case RADXA_SVC_SENSOR_CURRENT:
		return &radxa_svc_current_chip_info;
	case RADXA_SVC_SENSOR_VOLTAGE_CURRENT:
		return &radxa_svc_voltage_current_chip_info;
	default:
		return NULL;
	}
}

static int radxa_svc_sensor_validate_desc(struct radxa_svc_glink *svc,
					  const struct radxa_svc_sensor_desc *desc,
					  u32 index)
{
	const char *nul;
	u32 sensor_id = le32_to_cpu(desc->sensor_id);
	u32 sensor_type = le32_to_cpu(desc->sensor_type);
	u32 i;

	nul = memchr(desc->name, '\0', sizeof(desc->name));
	if (!nul || nul == desc->name || !radxa_svc_sensor_chip_info(sensor_type))
		return -EPROTO;

	for (i = 0; i < index; i++) {
		if (svc->sensors[i].sensor_id == sensor_id ||
		    !strcmp(svc->sensors[i].name, desc->name))
			return -EPROTO;
	}

	svc->sensors[index].svc = svc;
	svc->sensors[index].sensor_id = sensor_id;
	svc->sensors[index].sensor_type = sensor_type;
	strscpy(svc->sensors[index].name, desc->name,
		sizeof(svc->sensors[index].name));
	mutex_init(&svc->sensors[index].cache_lock);

	return 0;
}

static int radxa_svc_sensor_hwmon_init(struct radxa_svc_glink *svc)
{
	struct radxa_svc_sensor_list_resp *resp;
	struct radxa_svc_sensor_list_req req;
	const struct hwmon_chip_info *chip_info;
	char *hwmon_name;
	size_t max_len;
	size_t len;
	u32 returned;
	u32 total = 0;
	u32 start = 0;
	u32 i;
	int ret;

	if (!(svc->caps & RADXA_SVC_CAP_SENSORS))
		return 0;

	max_len = struct_size(resp, entries, RADXA_SVC_SENSOR_PAGE_MAX);
	resp = kzalloc(max_len, GFP_KERNEL);
	if (!resp)
		return -ENOMEM;

	do {
		req.start_index = cpu_to_le32(start);
		req.max_entries = cpu_to_le32(RADXA_SVC_SENSOR_PAGE_MAX);
		len = max_len;
		ret = radxa_svc_request(svc, RADXA_SVC_OP_SENSOR_LIST,
					&req, sizeof(req), resp, &len);
		if (ret)
			goto out_free;

		if (len < sizeof(*resp)) {
			ret = -EPROTO;
			goto out_free;
		}

		returned = le32_to_cpu(resp->returned_count);
		if (!start) {
			total = le32_to_cpu(resp->total_count);
			if (total > RADXA_SVC_SENSOR_MAX_SENSORS) {
				ret = -EOVERFLOW;
				goto out_free;
			}

			if (total) {
				svc->sensors =
					devm_kcalloc(svc->dev, total,
						     sizeof(*svc->sensors), GFP_KERNEL);
				if (!svc->sensors) {
					ret = -ENOMEM;
					goto out_free;
				}
			}
		} else if (le32_to_cpu(resp->total_count) != total) {
			ret = -EPROTO;
			goto out_free;
		}

		if (returned > RADXA_SVC_SENSOR_PAGE_MAX || returned > total - start ||
		    len != struct_size(resp, entries, returned) ||
		    (start < total && !returned)) {
			ret = -EPROTO;
			goto out_free;
		}

		for (i = 0; i < returned; i++) {
			ret = radxa_svc_sensor_validate_desc(svc, &resp->entries[i],
							     start + i);
			if (ret)
				goto out_free;
		}

		start += returned;
	} while (start < total);

	svc->num_sensors = total;
	for (i = 0; i < svc->num_sensors; i++) {
		struct radxa_svc_sensor *sensor = &svc->sensors[i];

		chip_info = radxa_svc_sensor_chip_info(sensor->sensor_type);
		hwmon_name = devm_hwmon_sanitize_name(svc->dev, sensor->name);
		if (IS_ERR(hwmon_name)) {
			ret = PTR_ERR(hwmon_name);
			goto out_free;
		}

		sensor->hwmon_dev =
			devm_hwmon_device_register_with_info(svc->dev, hwmon_name,
							     sensor, chip_info, NULL);
		if (IS_ERR(sensor->hwmon_dev)) {
			ret = PTR_ERR(sensor->hwmon_dev);
			dev_err_probe(svc->dev, ret,
				      "failed to register sensor %s hwmon\n",
				      sensor->name);
			goto out_free;
		}
	}

	ret = 0;

out_free:
	kfree(resp);
	return ret;
}

static int radxa_svc_fan_get_pwm_mode(struct radxa_svc_glink *svc,
				      long *mode)
{
	struct radxa_svc_fan_control_resp control = {};
	u32 control_mode;
	u32 profile;
	int ret;

	ret = radxa_svc_fan_get_control(svc, &control);
	if (ret)
		return ret;

	control_mode = le32_to_cpu(control.control_mode);

	switch (control_mode) {
	case RADXA_SVC_FAN_CONTROL_FULL_SPEED:
		*mode = RADXA_SVC_PWM_MODE_FULL_SPEED;
		return 0;
	case RADXA_SVC_FAN_CONTROL_MANUAL:
		*mode = RADXA_SVC_PWM_MODE_MANUAL;
		return 0;
	case RADXA_SVC_FAN_CONTROL_AUTO:
		break;
	default:
		return -EIO;
	}

	ret = radxa_svc_get_profile(svc, &profile);
	if (ret)
		return ret;

	switch (profile) {
	case RADXA_SVC_PROFILE_QUIET:
		*mode = RADXA_SVC_PWM_MODE_AUTO_QUIET;
		return 0;
	case RADXA_SVC_PROFILE_PERFORMANCE:
		*mode = RADXA_SVC_PWM_MODE_AUTO_PERFORMANCE;
		return 0;
	default:
		return -EIO;
	}
}

static int radxa_svc_fan_set_auto_mode(struct radxa_svc_glink *svc,
				       u32 profile)
{
	struct radxa_svc_fan_control_resp control = {};
	int ret;

	ret = radxa_svc_fan_get_control(svc, &control);
	if (ret)
		return ret;

	ret = radxa_svc_set_profile(svc, profile);
	if (ret)
		return ret;

	return radxa_svc_fan_set_control(svc, RADXA_SVC_FAN_CONTROL_AUTO,
					 le32_to_cpu(control.manual_pwm));
}

static int radxa_svc_fan_set_pwm_mode(struct radxa_svc_glink *svc, long mode)
{
	struct radxa_svc_fan_state_resp state = {};
	u32 duty_ns;
	u32 manual_pwm;
	u32 period_ns;
	int ret;

	switch (mode) {
	case RADXA_SVC_PWM_MODE_FULL_SPEED:
		return radxa_svc_fan_set_control(svc,
						 RADXA_SVC_FAN_CONTROL_FULL_SPEED,
						 RADXA_SVC_FAN_PWM_MAX);
	case RADXA_SVC_PWM_MODE_MANUAL:
		ret = radxa_svc_fan_get_state(svc, &state);
		if (ret)
			return ret;

		period_ns = le32_to_cpu(state.pwm_period_ns);
		duty_ns = le32_to_cpu(state.current_duty_ns);
		if (!period_ns) {
			manual_pwm = RADXA_SVC_FAN_PWM_MAX;
		} else {
			ret = radxa_svc_fan_duty_to_pwm(period_ns, duty_ns,
							&manual_pwm);
			if (ret)
				return ret;
		}

		return radxa_svc_fan_set_control(svc,
						 RADXA_SVC_FAN_CONTROL_MANUAL,
						 manual_pwm);
	case RADXA_SVC_PWM_MODE_AUTO_QUIET:
		return radxa_svc_fan_set_auto_mode(svc,
						RADXA_SVC_PROFILE_QUIET);
	case RADXA_SVC_PWM_MODE_AUTO_PERFORMANCE:
		return radxa_svc_fan_set_auto_mode(svc,
						RADXA_SVC_PROFILE_PERFORMANCE);
	default:
		return -EINVAL;
	}
}

static umode_t radxa_svc_hwmon_is_visible(const void *data,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel)
{
	if (type != hwmon_pwm || channel)
		return 0;

	switch (attr) {
	case hwmon_pwm_input:
	case hwmon_pwm_enable:
		return 0644;
	default:
		return 0;
	}
}

static int radxa_svc_hwmon_read(struct device *dev,
				enum hwmon_sensor_types type, u32 attr,
				int channel, long *val)
{
	struct radxa_svc_glink *svc = dev_get_drvdata(dev);
	struct radxa_svc_fan_state_resp state = {};
	u32 duty_ns;
	u32 period_ns;
	u32 pwm;
	int ret;

	if (type != hwmon_pwm || channel)
		return -EOPNOTSUPP;

	switch (attr) {
	case hwmon_pwm_input:
		ret = radxa_svc_fan_get_state(svc, &state);
		if (ret)
			return ret;

		period_ns = le32_to_cpu(state.pwm_period_ns);
		duty_ns = le32_to_cpu(state.current_duty_ns);
		ret = radxa_svc_fan_duty_to_pwm(period_ns, duty_ns, &pwm);
		if (ret)
			return ret;

		*val = pwm;
		return 0;
	case hwmon_pwm_enable:
		mutex_lock(&svc->fan_lock);
		ret = radxa_svc_fan_get_pwm_mode(svc, val);
		mutex_unlock(&svc->fan_lock);

		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static int radxa_svc_hwmon_write(struct device *dev,
				 enum hwmon_sensor_types type, u32 attr,
				 int channel, long val)
{
	struct radxa_svc_glink *svc = dev_get_drvdata(dev);
	struct radxa_svc_fan_control_resp control = {};
	int ret;

	if (type != hwmon_pwm || channel)
		return -EOPNOTSUPP;

	mutex_lock(&svc->fan_lock);

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > RADXA_SVC_FAN_PWM_MAX) {
			ret = -EINVAL;
			break;
		}

		ret = radxa_svc_fan_get_control(svc, &control);
		if (ret)
			break;

		if (le32_to_cpu(control.control_mode) !=
		    RADXA_SVC_FAN_CONTROL_MANUAL) {
			ret = -EINVAL;
			break;
		}

		ret = radxa_svc_fan_set_control(svc,
						RADXA_SVC_FAN_CONTROL_MANUAL,
						val);
		break;
	case hwmon_pwm_enable:
		ret = radxa_svc_fan_set_pwm_mode(svc, val);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&svc->fan_lock);

	return ret;
}

static const struct hwmon_ops radxa_svc_hwmon_ops = {
	.is_visible = radxa_svc_hwmon_is_visible,
	.read = radxa_svc_hwmon_read,
	.write = radxa_svc_hwmon_write,
};

static const struct hwmon_channel_info * const radxa_svc_hwmon_info[] = {
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL
};

static const struct hwmon_chip_info radxa_svc_hwmon_chip_info = {
	.ops = &radxa_svc_hwmon_ops,
	.info = radxa_svc_hwmon_info,
};

static int radxa_svc_hwmon_init(struct radxa_svc_glink *svc)
{
	svc->hwmon_dev = devm_hwmon_device_register_with_info(svc->dev,
							      "radxa_svc_glink",
							      svc,
							      &radxa_svc_hwmon_chip_info,
							      NULL);

	return PTR_ERR_OR_ZERO(svc->hwmon_dev);
}

static int radxa_svc_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct radxa_svc_version_resp version = {};
	struct radxa_svc_glink *svc;
	int ret;

	svc = devm_kzalloc(&rpdev->dev, sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->dev = &rpdev->dev;
	svc->rpdev = rpdev;
	mutex_init(&svc->xfer_lock);
	mutex_init(&svc->fan_lock);
	spin_lock_init(&svc->rsp_lock);
	init_completion(&svc->rsp);

	dev_set_drvdata(&rpdev->dev, svc);

	ret = radxa_svc_get_version(svc, &version);
	if (ret)
		return dev_err_probe(&rpdev->dev, ret,
				     "failed to read service version\n");

	ret = radxa_svc_check_version(&rpdev->dev, &version, &svc->caps);
	if (ret)
		return ret;

	ret = radxa_svc_hwmon_init(svc);
	if (ret)
		return dev_err_probe(&rpdev->dev, ret,
				     "failed to register hwmon\n");

	ret = radxa_svc_sensor_hwmon_init(svc);
	if (ret)
		return dev_err_probe(&rpdev->dev, ret,
				     "failed to register sensor hwmon devices\n");

	return 0;
}

static void radxa_svc_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct radxa_svc_glink *svc = dev_get_drvdata(&rpdev->dev);
	unsigned long flags;

	spin_lock_irqsave(&svc->rsp_lock, flags);
	svc->shutting_down = true;
	svc->pending = false;
	spin_unlock_irqrestore(&svc->rsp_lock, flags);
	complete_all(&svc->rsp);

	mutex_lock(&svc->xfer_lock);
	svc->rpdev = NULL;
	mutex_unlock(&svc->xfer_lock);
}

static const struct rpmsg_device_id radxa_svc_rpmsg_id_match[] = {
	{ "RADXA_SVC_ADSP_APPS" },
	{}
};
MODULE_DEVICE_TABLE(rpmsg, radxa_svc_rpmsg_id_match);

static struct rpmsg_driver radxa_svc_rpmsg_driver = {
	.probe = radxa_svc_rpmsg_probe,
	.remove = radxa_svc_rpmsg_remove,
	.callback = radxa_svc_rpmsg_callback,
	.id_table = radxa_svc_rpmsg_id_match,
	.drv = {
		.name = "radxa_svc_glink",
	},
};
module_rpmsg_driver(radxa_svc_rpmsg_driver);

MODULE_AUTHOR("Xilin Wu <sophon@radxa.com>");
MODULE_DESCRIPTION("Radxa SVC GLINK driver");
MODULE_LICENSE("GPL");
