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

#define RADXA_SVC_REQUIRED_CAPS		(RADXA_SVC_CAP_PROFILE | \
					 RADXA_SVC_CAP_FANCTL | \
					 RADXA_SVC_CAP_FANCTL_CTRL)

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

struct radxa_svc_glink {
	struct device *dev;
	struct rpmsg_device *rpdev;
	struct device *hwmon_dev;

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
