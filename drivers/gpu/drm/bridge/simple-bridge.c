// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015-2016 Free Electrons
 * Copyright (C) 2015-2016 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

/* how many times a sink that keeps changing is worth restarting for */
#define SIMPLE_BRIDGE_EDID_RESTARTS 3

struct simple_bridge_info {
	const struct drm_bridge_timings *timings;
	unsigned int connector_type;
	/* the device samples the downstream EDID while it boots */
	bool edid_at_power_up;
};

struct simple_bridge {
	struct drm_bridge	bridge;
	struct drm_connector	connector;

	const struct simple_bridge_info *info;

	struct mutex		lock;
	struct regulator	*vdd;
	struct gpio_desc	*enable;
	struct work_struct	hpd_work;
	enum drm_connector_status hpd_status;
	bool			hpd_enabled;
	bool			stream_enabled;
	bool			powered;
	bool			reset_pending;
	bool			hpd_disconnected;
	bool			edid_sampled;
	u64			edid_epoch;
	unsigned int		edid_restarts;
};

static inline struct simple_bridge *
drm_bridge_to_simple_bridge(struct drm_bridge *bridge)
{
	return container_of(bridge, struct simple_bridge, bridge);
}

static inline struct simple_bridge *
drm_connector_to_simple_bridge(struct drm_connector *connector)
{
	return container_of(connector, struct simple_bridge, connector);
}

static int simple_bridge_get_modes(struct drm_connector *connector)
{
	struct simple_bridge *sbridge = drm_connector_to_simple_bridge(connector);
	const struct drm_edid *drm_edid;
	int ret;

	if (sbridge->bridge.next_bridge->ops & DRM_BRIDGE_OP_EDID) {
		drm_edid = drm_bridge_edid_read(sbridge->bridge.next_bridge, connector);
		if (!drm_edid)
			DRM_INFO("EDID read failed. Fallback to standard modes\n");
	} else {
		drm_edid = NULL;
	}

	drm_edid_connector_update(connector, drm_edid);

	if (!drm_edid) {
		/*
		 * In case we cannot retrieve the EDIDs (missing or broken DDC
		 * bus from the next bridge), fallback on the XGA standards and
		 * prefer a mode pretty much anyone can handle.
		 */
		ret = drm_add_modes_noedid(connector, 1920, 1200);
		drm_set_preferred_mode(connector, 1024, 768);
		return ret;
	}

	ret = drm_edid_connector_add_modes(connector);
	drm_edid_free(drm_edid);

	return ret;
}

static const struct drm_connector_helper_funcs simple_bridge_con_helper_funcs = {
	.get_modes	= simple_bridge_get_modes,
};

static enum drm_connector_status
simple_bridge_connector_detect(struct drm_connector *connector, bool force)
{
	struct simple_bridge *sbridge = drm_connector_to_simple_bridge(connector);

	return drm_bridge_detect(sbridge->bridge.next_bridge, connector);
}

static const struct drm_connector_funcs simple_bridge_con_funcs = {
	.detect			= simple_bridge_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int simple_bridge_attach(struct drm_bridge *bridge,
				struct drm_encoder *encoder,
				enum drm_bridge_attach_flags flags)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	struct drm_bridge *prev __free(drm_bridge_put) = drm_bridge_get_prev_bridge(bridge);
	int ret;

	ret = drm_bridge_attach(encoder, sbridge->bridge.next_bridge, bridge,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret < 0)
		return ret;

	/*
	 * Some transparent bridges need to stay powered while HPD monitoring
	 * is enabled, otherwise late hotplug events never reach the upstream
	 * source. Proxy HPD to the previous bridge so we can keep power
	 * applied only while HPD or video streaming is active.
	 */
	if ((sbridge->vdd || sbridge->enable) && prev &&
	    (prev->ops & DRM_BRIDGE_OP_HPD))
		sbridge->bridge.ops |= DRM_BRIDGE_OP_HPD;
	else
		sbridge->bridge.ops &= ~DRM_BRIDGE_OP_HPD;

	/*
	 * A device restarted to refresh its EDID copy has to be able to say the
	 * output is gone while the restart is owed, so take detection over from
	 * the source for as long as that is the case.
	 */
	if (sbridge->info->edid_at_power_up && prev &&
	    (prev->ops & DRM_BRIDGE_OP_DETECT))
		sbridge->bridge.ops |= DRM_BRIDGE_OP_DETECT;

	/*
	 * Watching what the sink reports is also how a stale copy is caught,
	 * so the queries have to come through here rather than go straight to
	 * the source.
	 */
	if (sbridge->info->edid_at_power_up && prev &&
	    (prev->ops & DRM_BRIDGE_OP_MODES))
		sbridge->bridge.ops |= DRM_BRIDGE_OP_MODES;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	drm_connector_helper_add(&sbridge->connector,
				 &simple_bridge_con_helper_funcs);
	ret = drm_connector_init_with_ddc(bridge->dev, &sbridge->connector,
					  &simple_bridge_con_funcs,
					  sbridge->info->connector_type,
					  sbridge->bridge.next_bridge->ddc);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_attach_encoder(&sbridge->connector, encoder);

	return 0;
}

static int simple_bridge_update_power_locked(struct simple_bridge *sbridge)
{
	bool enable = sbridge->hpd_enabled || sbridge->stream_enabled;
	int ret = 0;

	if (sbridge->powered == enable)
		return 0;

	if (enable) {
		if (sbridge->vdd) {
			ret = regulator_enable(sbridge->vdd);
			if (ret) {
				DRM_ERROR("Failed to enable vdd regulator: %d\n", ret);
				return ret;
			}
		}

		gpiod_set_value_cansleep(sbridge->enable, 1);
	} else {
		gpiod_set_value_cansleep(sbridge->enable, 0);

		if (sbridge->vdd) {
			ret = regulator_disable(sbridge->vdd);
			if (ret) {
				DRM_ERROR("Failed to disable vdd regulator: %d\n", ret);
				return ret;
			}
		}
	}

	sbridge->powered = enable;

	return 0;
}

static void simple_bridge_upstream_hpd_cb(void *data,
					  enum drm_connector_status status);

static void simple_bridge_reset_locked(struct simple_bridge *sbridge)
{
	struct drm_bridge *prev __free(drm_bridge_put) =
		drm_bridge_get_prev_bridge(&sbridge->bridge);
	bool proxying;
	int ret;

	if (!sbridge->powered || !sbridge->vdd)
		return;

	WRITE_ONCE(sbridge->reset_pending, false);
	WRITE_ONCE(sbridge->edid_sampled, false);

	/*
	 * Taking the supply away makes the source see the sink leave, and a
	 * source that acts on that from an interrupt can take the link apart
	 * underneath whoever is driving it. None of that is worth reporting:
	 * the device is coming straight back. Stop the source from watching
	 * for the duration, and let it look again once there is something to
	 * see.
	 */
	proxying = sbridge->hpd_enabled && prev &&
		   (prev->ops & DRM_BRIDGE_OP_HPD);
	if (proxying)
		drm_bridge_hpd_disable(prev);

	gpiod_set_value_cansleep(sbridge->enable, 0);

	ret = regulator_disable(sbridge->vdd);
	if (ret) {
		drm_err(sbridge->bridge.dev,
			"Failed to disable vdd regulator: %d\n", ret);
		goto watch_again;
	}

	/* the off time and the boot time are described by the supply */
	ret = regulator_enable(sbridge->vdd);
	if (ret) {
		drm_err(sbridge->bridge.dev,
			"Failed to enable vdd regulator: %d\n", ret);
		sbridge->powered = false;
		goto watch_again;
	}

	gpiod_set_value_cansleep(sbridge->enable, 1);

watch_again:
	if (proxying)
		drm_bridge_hpd_enable(prev, simple_bridge_upstream_hpd_cb,
				      sbridge);
}

static void simple_bridge_upstream_hpd_work(struct work_struct *work)
{
	struct simple_bridge *sbridge =
		container_of(work, struct simple_bridge, hpd_work);
	enum drm_connector_status status = READ_ONCE(sbridge->hpd_status);

	/*
	 * The source coalesces a sink that leaves and returns into a single
	 * run of this work, so take the departure from the flag the callback
	 * leaves behind rather than from the state that outlived it.
	 */
	if (READ_ONCE(sbridge->hpd_disconnected)) {
		WRITE_ONCE(sbridge->hpd_disconnected, false);
		status = connector_status_disconnected;
	}

	drm_bridge_hpd_notify(&sbridge->bridge, status);
}

static void simple_bridge_upstream_hpd_cb(void *data,
					  enum drm_connector_status status)
{
	struct simple_bridge *sbridge = data;

	if (status == connector_status_disconnected)
		WRITE_ONCE(sbridge->hpd_disconnected, true);

	WRITE_ONCE(sbridge->hpd_status, status);
	schedule_work(&sbridge->hpd_work);
}

static void simple_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);

	/*
	 * Transparent DP bridges need power before the upstream source starts
	 * its enable sequence, otherwise resume-time link training can race
	 * ahead of the bridge coming out of reset.
	 */
	mutex_lock(&sbridge->lock);
	WRITE_ONCE(sbridge->stream_enabled, true);
	simple_bridge_update_power_locked(sbridge);
	mutex_unlock(&sbridge->lock);
}

static void simple_bridge_post_disable(struct drm_bridge *bridge)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);

	mutex_lock(&sbridge->lock);
	WRITE_ONCE(sbridge->stream_enabled, false);
	simple_bridge_update_power_locked(sbridge);
	if (READ_ONCE(sbridge->reset_pending))
		simple_bridge_reset_locked(sbridge);
	mutex_unlock(&sbridge->lock);
}

static enum drm_connector_status
simple_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	struct drm_bridge *prev __free(drm_bridge_put) = drm_bridge_get_prev_bridge(bridge);

	/*
	 * The EDID this bridge is still handing out belongs to a sink that has
	 * gone away, and only a restart replaces it. Report the output as gone
	 * so the stream standing in the way of that restart comes down.
	 */
	if (READ_ONCE(sbridge->reset_pending) &&
	    READ_ONCE(sbridge->stream_enabled))
		return connector_status_disconnected;

	if (!prev)
		return connector_status_unknown;

	return drm_bridge_detect(prev, connector);
}

static int simple_bridge_bridge_get_modes(struct drm_bridge *bridge,
					  struct drm_connector *connector)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	struct drm_bridge *prev __free(drm_bridge_put) = drm_bridge_get_prev_bridge(bridge);
	struct drm_display_mode *mode, *tmp;
	bool stale;
	u64 epoch;
	int ret;

	if (!prev)
		return 0;

	ret = drm_bridge_get_modes(prev, connector);
	epoch = connector->epoch_counter;

	/*
	 * A device that samples the EDID while it boots hands out that one copy
	 * for as long as it runs, so a sink that starts describing itself
	 * differently is one the device went back for and failed to read.
	 *
	 * What the sink reports after a restart becomes the new reference, so
	 * one that stays unreadable settles rather than restarting the device
	 * over and over.
	 */
	stale = READ_ONCE(sbridge->edid_sampled) &&
		READ_ONCE(sbridge->edid_epoch) != epoch;
	WRITE_ONCE(sbridge->edid_epoch, epoch);
	WRITE_ONCE(sbridge->edid_sampled, true);

	if (!stale) {
		/*
		 * A sink that reads the same twice running has settled, and
		 * whatever it took to get there is done. Anything that goes
		 * wrong from here is worth restarting for again.
		 */
		WRITE_ONCE(sbridge->edid_restarts, 0);
		return ret;
	}

	/*
	 * Restarting has not helped if the sink is still describing itself
	 * differently several attempts later, so stop and leave what there is
	 * rather than keep taking the display away. A sink that comes and goes
	 * on its own hands back a fresh budget.
	 */
	if (READ_ONCE(sbridge->edid_restarts) >= SIMPLE_BRIDGE_EDID_RESTARTS) {
		drm_dbg_kms(bridge->dev,
			    "sink still changing after %u restarts, leaving it\n",
			    SIMPLE_BRIDGE_EDID_RESTARTS);
		return ret;
	}

	WRITE_ONCE(sbridge->edid_restarts,
		   READ_ONCE(sbridge->edid_restarts) + 1);

	/*
	 * These modes come from a copy the device failed to refresh and
	 * describe nothing that is really there, so take the sink away before
	 * anything is set from them. Detection has already run for this pass
	 * and said the output was there, so say otherwise here as well: an
	 * output still reported as present but left without modes has a
	 * default one invented for it, which is no better than the modes just
	 * thrown away.
	 */
	WRITE_ONCE(sbridge->reset_pending, true);

	list_for_each_entry_safe(mode, tmp, &connector->probed_modes, head) {
		list_del(&mode->head);
		drm_mode_destroy(connector->dev, mode);
	}

	connector->status = connector_status_disconnected;

	WRITE_ONCE(sbridge->hpd_disconnected, true);
	schedule_work(&sbridge->hpd_work);

	return 0;
}

static void simple_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	struct drm_bridge *prev __free(drm_bridge_put) = drm_bridge_get_prev_bridge(bridge);
	int ret;

	mutex_lock(&sbridge->lock);
	sbridge->hpd_enabled = true;
	ret = simple_bridge_update_power_locked(sbridge);
	if (ret)
		sbridge->hpd_enabled = false;
	mutex_unlock(&sbridge->lock);

	if (ret || !prev || !(prev->ops & DRM_BRIDGE_OP_HPD))
		return;

	drm_bridge_hpd_enable(prev, simple_bridge_upstream_hpd_cb, sbridge);
}

static void simple_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	struct drm_bridge *prev __free(drm_bridge_put) = drm_bridge_get_prev_bridge(bridge);
	bool was_enabled;

	mutex_lock(&sbridge->lock);
	was_enabled = sbridge->hpd_enabled;
	sbridge->hpd_enabled = false;
	mutex_unlock(&sbridge->lock);

	if (was_enabled && prev && (prev->ops & DRM_BRIDGE_OP_HPD))
		drm_bridge_hpd_disable(prev);

	mutex_lock(&sbridge->lock);
	simple_bridge_update_power_locked(sbridge);
	mutex_unlock(&sbridge->lock);
}

static void simple_bridge_detach(struct drm_bridge *bridge)
{
	struct simple_bridge *sbridge = drm_bridge_to_simple_bridge(bridge);
	struct drm_bridge *prev __free(drm_bridge_put) = drm_bridge_get_prev_bridge(bridge);

	if (sbridge->hpd_enabled && prev && (prev->ops & DRM_BRIDGE_OP_HPD))
		drm_bridge_hpd_disable(prev);

	cancel_work_sync(&sbridge->hpd_work);

	mutex_lock(&sbridge->lock);
	sbridge->hpd_enabled = false;
	WRITE_ONCE(sbridge->stream_enabled, false);
	simple_bridge_update_power_locked(sbridge);
	mutex_unlock(&sbridge->lock);
}

static const struct drm_bridge_funcs simple_bridge_bridge_funcs = {
	.attach		= simple_bridge_attach,
	.detach		= simple_bridge_detach,
	.detect		= simple_bridge_detect,
	.get_modes	= simple_bridge_bridge_get_modes,
	.pre_enable	= simple_bridge_pre_enable,
	.post_disable	= simple_bridge_post_disable,
	.hpd_enable	= simple_bridge_hpd_enable,
	.hpd_disable	= simple_bridge_hpd_disable,
};

static int simple_bridge_probe(struct platform_device *pdev)
{
	struct simple_bridge *sbridge;
	struct device_node *remote;

	sbridge = devm_drm_bridge_alloc(&pdev->dev, struct simple_bridge,
					bridge, &simple_bridge_bridge_funcs);
	if (IS_ERR(sbridge))
		return PTR_ERR(sbridge);

	sbridge->info = of_device_get_match_data(&pdev->dev);
	mutex_init(&sbridge->lock);
	INIT_WORK(&sbridge->hpd_work, simple_bridge_upstream_hpd_work);

	/* Get the next bridge in the pipeline. */
	remote = of_graph_get_remote_node(pdev->dev.of_node, 1, -1);
	if (!remote)
		return -EINVAL;

	sbridge->bridge.next_bridge = of_drm_find_and_get_bridge(remote);
	of_node_put(remote);

	if (!sbridge->bridge.next_bridge) {
		dev_dbg(&pdev->dev, "Next bridge not found, deferring probe\n");
		return -EPROBE_DEFER;
	}

	/* Get the regulator and GPIO resources. */
	sbridge->vdd = devm_regulator_get_optional(&pdev->dev, "vdd");
	if (IS_ERR(sbridge->vdd)) {
		int ret = PTR_ERR(sbridge->vdd);
		if (ret == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		sbridge->vdd = NULL;
		dev_dbg(&pdev->dev, "No vdd regulator found: %d\n", ret);
	}

	sbridge->enable = devm_gpiod_get_optional(&pdev->dev, "enable",
						  GPIOD_OUT_LOW);
	if (IS_ERR(sbridge->enable))
		return dev_err_probe(&pdev->dev, PTR_ERR(sbridge->enable),
				     "Unable to retrieve enable GPIO\n");

	/* Register the bridge. */
	sbridge->bridge.of_node = pdev->dev.of_node;
	sbridge->bridge.timings = sbridge->info->timings;

	return devm_drm_bridge_add(&pdev->dev, &sbridge->bridge);
}

/*
 * We assume the ADV7123 DAC is the "default" for historical reasons
 * Information taken from the ADV7123 datasheet, revision D.
 * NOTE: the ADV7123EP seems to have other timings and need a new timings
 * set if used.
 */
static const struct drm_bridge_timings default_bridge_timings = {
	/* Timing specifications, datasheet page 7 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	.setup_time_ps = 500,
	.hold_time_ps = 1500,
};

/*
 * Information taken from the THS8134, THS8134A, THS8134B datasheet named
 * "SLVS205D", dated May 1990, revised March 2000.
 */
static const struct drm_bridge_timings ti_ths8134_bridge_timings = {
	/* From timing diagram, datasheet page 9 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	/* From datasheet, page 12 */
	.setup_time_ps = 3000,
	/* I guess this means latched input */
	.hold_time_ps = 0,
};

/*
 * Information taken from the THS8135 datasheet named "SLAS343B", dated
 * May 2001, revised April 2013.
 */
static const struct drm_bridge_timings ti_ths8135_bridge_timings = {
	/* From timing diagram, datasheet page 14 */
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE,
	/* From datasheet, page 16 */
	.setup_time_ps = 2000,
	.hold_time_ps = 500,
};

static const struct of_device_id simple_bridge_match[] = {
	{
		.compatible = "dumb-vga-dac",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	}, {
		.compatible = "adi,adv7123",
		.data = &(const struct simple_bridge_info) {
			.timings = &default_bridge_timings,
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	}, {
		.compatible = "algoltek,ag6311",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_HDMIA,
		},
	}, {
		.compatible = "asl-tek,cs5263",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_HDMIA,
		},
	}, {
		.compatible = "chrontel,ch7218a",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_HDMIA,
			.edid_at_power_up = true,
		},
	}, {
		.compatible = "parade,ps185hdm",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_HDMIA,
		},
	}, {
		.compatible = "radxa,ra620",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_HDMIA,
		},
	}, {
		.compatible = "realtek,rtd2171",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_HDMIA,
		},
	}, {
		.compatible = "ti,opa362",
		.data = &(const struct simple_bridge_info) {
			.connector_type = DRM_MODE_CONNECTOR_Composite,
		},
	}, {
		.compatible = "ti,ths8135",
		.data = &(const struct simple_bridge_info) {
			.timings = &ti_ths8135_bridge_timings,
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	}, {
		.compatible = "ti,ths8134",
		.data = &(const struct simple_bridge_info) {
			.timings = &ti_ths8134_bridge_timings,
			.connector_type = DRM_MODE_CONNECTOR_VGA,
		},
	},
	{},
};
MODULE_DEVICE_TABLE(of, simple_bridge_match);

static struct platform_driver simple_bridge_driver = {
	.probe	= simple_bridge_probe,
	.driver		= {
		.name		= "simple-bridge",
		.of_match_table	= simple_bridge_match,
	},
};
module_platform_driver(simple_bridge_driver);

MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Simple DRM bridge driver");
MODULE_LICENSE("GPL");
