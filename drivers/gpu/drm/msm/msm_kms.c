// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/aperture.h>
#include <linux/kthread.h>
#include <linux/sched/mm.h>
#include <uapi/linux/sched/types.h>

#include <drm/drm_drv.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_mode_config.h>
#include <drm/drm_vblank.h>
#include <drm/drm_vblank_work.h>
#include <drm/clients/drm_client_setup.h>

#include "disp/msm_disp_snapshot.h"
#include "msm_drv.h"
#include "msm_gem.h"
#include "msm_kms.h"
#include "msm_mmu.h"

static const struct drm_mode_config_funcs mode_config_funcs = {
	.fb_create = msm_framebuffer_create,
	.atomic_check = msm_atomic_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static const struct drm_mode_config_helper_funcs mode_config_helper_funcs = {
	.atomic_commit_tail = msm_atomic_commit_tail,
	.atomic_commit_setup = msm_atomic_commit_setup,
};

static irqreturn_t msm_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	BUG_ON(!kms);

	return kms->funcs->irq(kms);
}

static void msm_irq_preinstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	BUG_ON(!kms);

	kms->funcs->irq_preinstall(kms);
}

static int msm_irq_postinstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	BUG_ON(!kms);

	if (kms->funcs->irq_postinstall)
		return kms->funcs->irq_postinstall(kms);

	return 0;
}

static int msm_irq_install(struct drm_device *dev, unsigned int irq)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	int ret;

	if (irq == IRQ_NOTCONNECTED)
		return -ENOTCONN;

	msm_irq_preinstall(dev);

	ret = request_irq(irq, msm_irq, 0, dev->driver->name, dev);
	if (ret)
		return ret;

	kms->irq_requested = true;

	ret = msm_irq_postinstall(dev);
	if (ret) {
		free_irq(irq, dev);
		return ret;
	}

	return 0;
}

static void msm_irq_uninstall(struct drm_device *dev)
{
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;

	kms->funcs->irq_uninstall(kms);
	if (kms->irq_requested)
		free_irq(kms->irq, dev);
}

struct msm_vblank_work {
	struct work_struct work;
	struct drm_crtc *crtc;
	bool enable;
	struct msm_drm_private *priv;
};

static void vblank_ctrl_worker(struct work_struct *work)
{
	struct msm_vblank_work *vbl_work = container_of(work,
						struct msm_vblank_work, work);
	struct msm_drm_private *priv = vbl_work->priv;
	struct msm_kms *kms = priv->kms;

	if (vbl_work->enable)
		kms->funcs->enable_vblank(kms, vbl_work->crtc);
	else
		kms->funcs->disable_vblank(kms,	vbl_work->crtc);

	kfree(vbl_work);
}

static int vblank_ctrl_queue_work(struct msm_drm_private *priv,
				  struct drm_crtc *crtc, bool enable)
{
	struct msm_vblank_work *vbl_work;

	vbl_work = kzalloc_obj(*vbl_work, GFP_ATOMIC);
	if (!vbl_work)
		return -ENOMEM;

	INIT_WORK(&vbl_work->work, vblank_ctrl_worker);

	vbl_work->crtc = crtc;
	vbl_work->enable = enable;
	vbl_work->priv = priv;

	queue_work(priv->kms->wq, &vbl_work->work);

	return 0;
}

int msm_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	if (!kms)
		return -ENXIO;
	drm_dbg_vbl(dev, "crtc=%u\n", crtc->base.id);
	return vblank_ctrl_queue_work(priv, crtc, true);
}

void msm_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	if (!kms)
		return;
	drm_dbg_vbl(dev, "crtc=%u\n", crtc->base.id);
	vblank_ctrl_queue_work(priv, crtc, false);
}

struct msm_fb_unpin_work {
	struct drm_vblank_work base;
	struct list_head node;
	struct msm_kms_fb_unpin *pending;
	struct drm_framebuffer *fb;
};

static void msm_kms_fb_unpin_release(struct msm_fb_unpin_work *unpin)
{
	msm_framebuffer_unpin(unpin->fb);
	drm_framebuffer_put(unpin->fb);
	kfree(unpin);
}

static void msm_kms_fb_unpin_work(struct kthread_work *work)
{
	struct msm_fb_unpin_work *unpin =
		container_of(to_drm_vblank_work(work), struct msm_fb_unpin_work,
			     base);
	struct msm_kms_fb_unpin *pending = unpin->pending;

	scoped_guard(spinlock, &pending->lock) {
		if (list_empty(&unpin->node))
			return;
		list_del_init(&unpin->node);
	}

	msm_kms_fb_unpin_release(unpin);
}

void msm_crtc_vblank_on(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	unsigned int idx = drm_crtc_index(crtc);

	drm_crtc_vblank_on(crtc);

	if (!kms || idx >= ARRAY_SIZE(kms->fb_unpin))
		return;

	scoped_guard(spinlock, &kms->fb_unpin[idx].lock)
		kms->fb_unpin[idx].vblank_off = false;
}

/*
 * The crtc stops fetching here, and with it the vblanks the pending works are
 * waiting for, so release the framebuffers directly.
 */
static void msm_kms_fb_unpin_release_all(struct msm_kms_fb_unpin *pending)
{
	struct msm_fb_unpin_work *unpin;

	scoped_guard(spinlock, &pending->lock)
		pending->vblank_off = true;

	for (;;) {
		scoped_guard(spinlock, &pending->lock) {
			unpin = list_first_entry_or_null(&pending->fbs,
							 typeof(*unpin), node);
			if (unpin)
				list_del_init(&unpin->node);
		}

		if (!unpin)
			break;

		drm_vblank_work_cancel_sync(&unpin->base);
		msm_kms_fb_unpin_release(unpin);
	}
}

void msm_crtc_vblank_off(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	unsigned int idx = drm_crtc_index(crtc);

	if (kms && idx < ARRAY_SIZE(kms->fb_unpin))
		msm_kms_fb_unpin_release_all(&kms->fb_unpin[idx]);

	drm_crtc_vblank_off(crtc);
}

/*
 * Publish and schedule the work in one go under the lock, as
 * msm_crtc_vblank_off() frees whatever it finds on the list.
 */
static bool msm_kms_fb_unpin_schedule(struct msm_kms_fb_unpin *pending,
				      struct msm_fb_unpin_work *unpin,
				      struct drm_crtc *crtc)
{
	guard(spinlock)(&pending->lock);

	if (pending->vblank_off)
		return false;

	list_add_tail(&unpin->node, &pending->fbs);

	if (drm_vblank_work_schedule(&unpin->base,
				     drm_crtc_vblank_count(crtc) + 1, true) == 1)
		return true;

	list_del_init(&unpin->node);

	return false;
}

bool msm_crtc_queue_fb_unpin(struct drm_crtc *crtc, struct drm_framebuffer *fb)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;
	struct msm_kms *kms = priv->kms;
	unsigned int idx = drm_crtc_index(crtc);
	struct msm_kms_fb_unpin *pending;
	struct msm_fb_unpin_work *unpin;

	if (!kms || idx >= ARRAY_SIZE(kms->fb_unpin))
		return false;

	pending = &kms->fb_unpin[idx];

	unpin = kzalloc_obj(*unpin);
	if (!unpin)
		return false;

	unpin->fb = fb;
	unpin->pending = pending;
	drm_framebuffer_get(fb);

	drm_vblank_work_init(&unpin->base, crtc, msm_kms_fb_unpin_work);

	if (msm_kms_fb_unpin_schedule(pending, unpin, crtc))
		return true;

	drm_framebuffer_put(fb);
	kfree(unpin);

	return false;
}

static int msm_kms_fault_handler(void *arg, unsigned long iova, int flags, void *data)
{
	struct msm_kms *kms = arg;

	if (atomic_read(&kms->fault_snapshot_capture) == 0) {
		msm_disp_snapshot_state(kms->dev);
		atomic_inc(&kms->fault_snapshot_capture);
	}

	return -ENOSYS;
}

struct drm_gpuvm *msm_kms_init_vm(struct drm_device *dev, struct device *mdss_dev)
{
	struct drm_gpuvm *vm;
	struct msm_mmu *mmu;
	struct device *mdp_dev = dev->dev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_kms *kms = priv->kms;
	struct device *iommu_dev;

	/*
	 * IOMMUs can be a part of MDSS device tree binding, or the
	 * MDP/DPU device.
	 */
	if (device_iommu_mapped(mdp_dev))
		iommu_dev = mdp_dev;
	else if (mdss_dev && device_iommu_mapped(mdss_dev))
		iommu_dev = mdss_dev;
	else {
		drm_info(dev, "no IOMMU, bailing out\n");
		return ERR_PTR(-ENODEV);
	}

	mmu = msm_iommu_disp_new(iommu_dev, 0);
	if (IS_ERR(mmu))
		return ERR_CAST(mmu);

	vm = msm_gem_vm_create(dev, mmu, "mdp_kms",
			       0x1000, 0x100000000 - 0x1000, true);
	if (IS_ERR(vm)) {
		dev_err(mdp_dev, "vm create, error %pe\n", vm);
		mmu->funcs->destroy(mmu);
		return vm;
	}

	msm_mmu_set_fault_handler(to_msm_vm(vm)->mmu, kms, msm_kms_fault_handler);

	return vm;
}

void msm_drm_kms_unregister(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct drm_device *ddev = priv->dev;

	drm_atomic_helper_shutdown(ddev);
}

void msm_drm_kms_uninit(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct drm_device *ddev = priv->dev;
	struct msm_kms *kms = priv->kms;
	int i;

	BUG_ON(!kms);

	/* We must cancel and cleanup any pending vblank enable/disable
	 * work before msm_irq_uninstall() to avoid work re-enabling an
	 * irq after uninstall has disabled it.
	 */

	flush_workqueue(kms->wq);

	/* clean up event worker threads */
	for (i = 0; i < MAX_CRTCS; i++) {
		if (kms->event_thread[i].worker)
			kthread_destroy_worker(kms->event_thread[i].worker);
	}

	drm_kms_helper_poll_fini(ddev);

	msm_disp_snapshot_destroy(ddev);

	pm_runtime_get_sync(dev);
	msm_irq_uninstall(ddev);
	pm_runtime_put_sync(dev);

	if (kms && kms->funcs)
		kms->funcs->destroy(kms);
}

int msm_drm_kms_init(struct device *dev, const struct drm_driver *drv)
{
	struct msm_drm_private *priv = dev_get_drvdata(dev);
	struct drm_device *ddev = priv->dev;
	struct msm_kms *kms = priv->kms;
	struct drm_crtc *crtc;
	int ret;

	/* the fw fb could be anywhere in memory */
	ret = aperture_remove_all_conflicting_devices(drv->name);
	if (ret)
		return ret;

	ret = msm_disp_snapshot_init(ddev);
	if (ret) {
		DRM_DEV_ERROR(dev, "msm_disp_snapshot_init failed ret = %d\n", ret);
		return ret;
	}

	ret = priv->kms_init(ddev);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to load kms\n");
		goto err_msm_uninit;
	}

	/* Enable normalization of plane zpos */
	ddev->mode_config.normalize_zpos = true;

	ddev->mode_config.funcs = &mode_config_funcs;
	ddev->mode_config.helper_private = &mode_config_helper_funcs;

	kms->dev = ddev;
	ret = kms->funcs->hw_init(kms);
	if (ret) {
		DRM_DEV_ERROR(dev, "kms hw init failed: %d\n", ret);
		goto err_msm_uninit;
	}

	drm_helper_move_panel_connectors_to_head(ddev);

	drm_for_each_crtc(crtc, ddev) {
		struct msm_drm_thread *ev_thread;

		/* initialize event thread */
		ev_thread = &kms->event_thread[drm_crtc_index(crtc)];
		ev_thread->dev = ddev;
		ev_thread->worker = kthread_run_worker(0, "crtc_event:%d", crtc->base.id);
		if (IS_ERR(ev_thread->worker)) {
			ret = PTR_ERR(ev_thread->worker);
			DRM_DEV_ERROR(dev, "failed to create crtc_event kthread\n");
			ev_thread->worker = NULL;
			goto err_msm_uninit;
		}

		sched_set_fifo(ev_thread->worker->task);
	}

	ret = drm_vblank_init(ddev, ddev->mode_config.num_crtc);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "failed to initialize vblank\n");
		goto err_msm_uninit;
	}

	pm_runtime_get_sync(dev);
	ret = msm_irq_install(ddev, kms->irq);
	pm_runtime_put_sync(dev);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "failed to install IRQ handler\n");
		goto err_msm_uninit;
	}

	drm_mode_config_reset(ddev);

	return 0;

err_msm_uninit:
	return ret;
}

int msm_kms_pm_prepare(struct device *dev)
{
	struct msm_drm_private *priv = dev_get_drvdata(dev);
	struct drm_device *ddev = priv ? priv->dev : NULL;

	if (!priv || !priv->kms)
		return 0;

	return drm_mode_config_helper_suspend(ddev);
}

void msm_kms_pm_complete(struct device *dev)
{
	struct msm_drm_private *priv = dev_get_drvdata(dev);
	struct drm_device *ddev = priv ? priv->dev : NULL;

	if (!priv || !priv->kms)
		return;

	drm_mode_config_helper_resume(ddev);
}

void msm_kms_shutdown(struct platform_device *pdev)
{
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct drm_device *drm = priv ? priv->dev : NULL;

	/*
	 * Shutdown the hw if we're far enough along where things might be on.
	 * If we run this too early, we'll end up panicking in any variety of
	 * places. Since we don't register the drm device until late in
	 * msm_drm_init, drm_dev->registered is used as an indicator that the
	 * shutdown will be successful.
	 */
	if (drm && drm->registered && priv->kms)
		drm_atomic_helper_shutdown(drm);
}

void msm_drm_kms_post_init(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_drm_private *priv = platform_get_drvdata(pdev);
	struct drm_device *ddev = priv->dev;

	drm_kms_helper_poll_init(ddev);
	drm_client_setup(ddev, NULL);
}
