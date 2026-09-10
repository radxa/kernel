// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2018, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-resv.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/of_platform.h>
#include <linux/rpmsg.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <uapi/misc/fastrpc.h>
#include <linux/of_reserved_mem.h>
#include <linux/soc/qcom/pdr.h>

#define FASTRPC_IOCTL_INVOKEV2		_IOWR('R', 14, struct fastrpc_invoke_v2)

#define ADSP_DOMAIN_ID (0)
#define MDSP_DOMAIN_ID (1)
#define SDSP_DOMAIN_ID (2)
#define CDSP_DOMAIN_ID (3)
#define GDSP_DOMAIN_ID (4)
#define FASTRPC_MAX_SESSIONS	14
#define FASTRPC_MAX_SPD		4
#define FASTRPC_MAX_VMIDS	16
#define FASTRPC_ALIGN		128
#define FASTRPC_MAX_FDLIST	16
#define FASTRPC_MAX_CRCLIST	64
#define FASTRPC_PHYS(p)	((p) & 0xffffffff)
#define FASTRPC_CTX_MAX (256)
#define FASTRPC_INIT_HANDLE	1
#define FASTRPC_DSP_UTILITIES_HANDLE	2
#define FASTRPC_MAX_STATIC_HANDLE (20)
#define FASTRPC_CTXID_MASK (0xFF0000)
#define FASTRPC_CLIENTID_MASK (16)
#define INIT_FILELEN_MAX (5 * 1024 * 1024)
#define INIT_FILE_NAMELEN_MAX (128)
#define FASTRPC_DEVICE_NAME	"fastrpc"

/* Add memory to static PD pool, protection thru XPU */
#define ADSP_MMAP_HEAP_ADDR  4
/* MAP static DMA buffer on DSP User PD */
#define ADSP_MMAP_DMA_BUFFER  6
/* Add memory to static PD pool protection thru hypervisor */
#define ADSP_MMAP_REMOTE_HEAP_ADDR  8
/* Add memory to userPD pool, for user heap */
#define ADSP_MMAP_ADD_PAGES 0x1000
/* Add memory to userPD pool, for LLC heap */
#define ADSP_MMAP_ADD_PAGES_LLC 0x3000,

#define DSP_UNSUPPORTED_API (0x80000414)
/* MAX NUMBER of DSP ATTRIBUTES SUPPORTED */
#define FASTRPC_MAX_DSP_ATTRIBUTES (256)
#define FASTRPC_MAX_DSP_ATTRIBUTES_LEN (sizeof(u32) * FASTRPC_MAX_DSP_ATTRIBUTES)

/* Retrives number of input buffers from the scalars parameter */
#define REMOTE_SCALARS_INBUFS(sc)	(((sc) >> 16) & 0x0ff)

/* Retrives number of output buffers from the scalars parameter */
#define REMOTE_SCALARS_OUTBUFS(sc)	(((sc) >> 8) & 0x0ff)

/* Retrives number of input handles from the scalars parameter */
#define REMOTE_SCALARS_INHANDLES(sc)	(((sc) >> 4) & 0x0f)

/* Retrives number of output handles from the scalars parameter */
#define REMOTE_SCALARS_OUTHANDLES(sc)	((sc) & 0x0f)

#define REMOTE_SCALARS_LENGTH(sc)	(REMOTE_SCALARS_INBUFS(sc) +   \
					 REMOTE_SCALARS_OUTBUFS(sc) +  \
					 REMOTE_SCALARS_INHANDLES(sc)+ \
					 REMOTE_SCALARS_OUTHANDLES(sc))
#define FASTRPC_BUILD_SCALARS(attr, method, in, out, oin, oout)  \
				(((attr & 0x07) << 29) |		\
				((method & 0x1f) << 24) |	\
				((in & 0xff) << 16) |		\
				((out & 0xff) <<  8) |		\
				((oin & 0x0f) <<  4) |		\
				(oout & 0x0f))

#define FASTRPC_SCALARS(method, in, out) \
		FASTRPC_BUILD_SCALARS(0, method, in, out, 0, 0)

#define FASTRPC_CREATE_PROCESS_NARGS	6
#define FASTRPC_CREATE_STATIC_PROCESS_NARGS	3
/* Remote Method id table */
#define FASTRPC_RMID_INIT_ATTACH	0
#define FASTRPC_RMID_INIT_RELEASE	1
#define FASTRPC_RMID_INIT_MMAP		4
#define FASTRPC_RMID_INIT_MUNMAP	5
#define FASTRPC_RMID_INIT_CREATE	6
#define FASTRPC_RMID_INIT_CREATE_ATTR	7
#define FASTRPC_RMID_INIT_CREATE_STATIC	8
#define FASTRPC_RMID_INIT_MEM_MAP      10
#define FASTRPC_RMID_INIT_MEM_UNMAP    11

/* Fastrpc attribute for no mapping of fd  */
#define FASTRPC_ATTR_NOMAP (16)

/* Protection Domain(PD) ids */
#define ROOT_PD		(0)
#define USER_PD		(1)
#define SENSORS_PD	(2)

#define miscdev_to_fdevice(d) container_of(d, struct fastrpc_device, miscdev)

#define AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME   "audio_pdr_adsp"
#define AUDIO_PDR_ADSP_SERVICE_NAME              "avs/audio"
#define ADSP_AUDIOPD_NAME                        "msm/adsp/audio_pd"

#define SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME   "sensors_pdr_adsp"
#define SENSORS_PDR_ADSP_SERVICE_NAME              "tms/servreg"
#define ADSP_SENSORPD_NAME                       "msm/adsp/sensor_pd"

#define SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME "sensors_pdr_slpi"
#define SENSORS_PDR_SLPI_SERVICE_NAME            SENSORS_PDR_ADSP_SERVICE_NAME
#define SLPI_SENSORPD_NAME                       "msm/slpi/sensor_pd"

#define FASTRPC_KERNEL_PERF_LIST (PERF_KEY_MAX)
#define FASTRPC_DSP_PERF_LIST 12

/* Poll response number from remote processor for call completion */
#define FASTRPC_POLL_RESPONSE (0xdecaf)
/* timeout in us for polling until memory barrier */
#define FASTRPC_POLL_TIME_MEM_UPDATE (500)

/* Response types supported for RPC calls */
enum fastrpc_response_flags {
	/* normal job completion glink response */
	NORMAL_RESPONSE = 0,
	/* process updates poll memory instead of glink response */
	POLL_MODE = 1,
};

struct fastrpc_invoke_v2 {
	struct fastrpc_invoke inv;
	__u64 crc;
	__u64 perf_kernel;
	__u64 perf_dsp;
	__u64 poll_timeout;
	__u32 reserved[18];
};

struct fastrpc_phy_page {
	u64 addr;		/* physical address */
	u64 size;		/* size of contiguous region */
};

struct fastrpc_invoke_buf {
	u32 num;		/* number of contiguous regions */
	u32 pgidx;		/* index to start of contiguous region */
};

struct fastrpc_remote_dmahandle {
	s32 fd;		/* dma handle fd */
	u32 offset;	/* dma handle offset */
	u32 len;	/* dma handle length */
};

struct fastrpc_remote_buf {
	u64 pv;		/* buffer pointer */
	u64 len;	/* length of buffer */
};

union fastrpc_remote_arg {
	struct fastrpc_remote_buf buf;
	struct fastrpc_remote_dmahandle dma;
};

struct fastrpc_mmap_rsp_msg {
	u64 vaddr;
};

struct fastrpc_mmap_req_msg {
	s32 pgid;
	u32 flags;
	u64 vaddr;
	s32 num;
};

struct fastrpc_mem_map_req_msg {
	s32 pgid;
	s32 fd;
	s32 offset;
	u32 flags;
	u64 vaddrin;
	s32 num;
	s32 data_len;
};

struct fastrpc_munmap_req_msg {
	s32 pgid;
	u64 vaddr;
	u64 size;
};

struct fastrpc_mem_unmap_req_msg {
	s32 pgid;
	s32 fd;
	u64 vaddrin;
	u64 len;
};

struct fastrpc_msg {
	int pid;		/* process group id */
	int tid;		/* thread id */
	u64 ctx;		/* invoke caller context */
	u32 handle;	/* handle to invoke */
	u32 sc;		/* scalars structure describing the data */
	u64 addr;		/* physical address */
	u64 size;		/* size of contiguous region */
};

struct fastrpc_invoke_rsp {
	u64 ctx;		/* invoke caller context */
	int retval;		/* invoke return value */
};

struct fastrpc_buf_overlap {
	u64 start;
	u64 end;
	int raix;
	u64 mstart;
	u64 mend;
	u64 offset;
};

struct fastrpc_buf {
	struct fastrpc_user *fl;
	struct dma_buf *dmabuf;
	struct device *dev;
	void *virt;
	u32 flag;
	u64 phys;
	u64 size;
	/* Lock for dma buf attachments */
	struct mutex lock;
	struct list_head attachments;
	/* mmap support */
	struct list_head node; /* list of user requested mmaps */
	uintptr_t raddr;
};

struct fastrpc_dma_buf_attachment {
	struct device *dev;
	struct sg_table sgt;
	struct list_head node;
};

struct fastrpc_map {
	struct list_head node;
	struct fastrpc_user *fl;
	int fd;
	struct dma_buf *buf;
	struct sg_table *table;
	struct dma_buf_attachment *attach;
	u64 phys;
	u64 size;
	void *va;
	u64 len;
	u64 raddr;
	u32 attr;
	struct kref refcount;
};

struct fastrpc_perf {
	u64 count;
	u64 flush;
	u64 map;
	u64 copy;
	u64 link;
	u64 getargs;
	u64 putargs;
	u64 invargs;
	u64 invoke;
};

struct fastrpc_invoke_ctx {
	int nscalars;
	int nbufs;
	int retval;
	int pid;
	int tgid;
	u32 sc;
	u32 *crc;
	u64 *perf_kernel;
	u64 *perf_dsp;
	u64 ctxid;
	u64 msg_sz;
	/* Threads poll for specified timeout and fall back to glink wait */
	u64 poll_timeout;
	/* work done status flag */
	bool is_work_done;
	/* response flags from remote processor */
	enum fastrpc_response_flags rsp_flags;
	struct kref refcount;
	struct list_head node; /* list of ctxs */
	struct completion work;
	struct fastrpc_msg msg;
	struct fastrpc_user *fl;
	union fastrpc_remote_arg *rpra;
	struct fastrpc_map **maps;
	struct fastrpc_buf *buf;
	struct fastrpc_invoke_args *args;
	struct fastrpc_buf_overlap *olaps;
	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_perf *perf;
};

struct fastrpc_session_ctx {
	struct device *dev;
	int sid;
	bool used;
	bool valid;
	struct mutex map_mutex;
};

struct fastrpc_static_pd {
	char *servloc_name;
	char *spdname;
	void *pdrhandle;
	u64 pdrcount;
	u64 prevpdrcount;
	atomic_t ispdup;
	int domain;
	struct fastrpc_channel_ctx *cctx;
};

struct fastrpc_channel_ctx {
	int domain_id;
	int sesscount;
	int vmcount;
	struct qcom_scm_vmperm vmperms[FASTRPC_MAX_VMIDS];
	struct rpmsg_device *rpdev;
	struct fastrpc_session_ctx session[FASTRPC_MAX_SESSIONS];
	struct fastrpc_static_pd spd[FASTRPC_MAX_SPD];
	spinlock_t lock;
	struct idr ctx_idr;
	struct list_head users;
	struct kref refcount;
	/* Flag if dsp attributes are cached */
	bool valid_attributes;
	bool staticpd_status;
	u32 dsp_attributes[FASTRPC_MAX_DSP_ATTRIBUTES];
	struct fastrpc_device *secure_fdevice;
	struct fastrpc_device *fdevice;
	struct list_head rhmaps;
	bool secure;
	bool unsigned_support;
	u64 dma_mask;
};

struct fastrpc_device {
	struct fastrpc_channel_ctx *cctx;
	struct miscdevice miscdev;
	bool secure;
};

struct fastrpc_user {
	struct list_head user;
	struct list_head maps;
	struct list_head pending;
	struct list_head mmaps;

	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_session_ctx *sctx;
	struct fastrpc_buf *init_mem;

	int client_id;
	int pd;
	bool is_secure_dev;
	bool is_unsigned_pd;
	char *servloc_name;
	/* Lock for lists */
	spinlock_t lock;
	/* lock for allocations */
	struct mutex mutex;
};

enum fastrpc_perfkeys {
	PERF_COUNT = 0,
	PERF_FLUSH = 1,
	PERF_MAP = 2,
	PERF_COPY = 3,
	PERF_LINK = 4,
	PERF_GETARGS = 5,
	PERF_PUTARGS = 6,
	PERF_INVARGS = 7,
	PERF_INVOKE = 8,
	PERF_KEY_MAX = 9,
};

#define PERF_END ((void)0)

#define PERF(enb, cnt, ff) \
	{\
		struct timespec64 startT = {0};\
		u64 *counter = cnt;\
		if (enb && counter) {\
			ktime_get_boottime_ts64(&startT);\
		} \
		ff ;\
		if (enb && counter) {\
			*counter += getnstimediff(&startT);\
		} \
	}

#define GET_COUNTER(perf_ptr, offset)  \
	(perf_ptr != NULL ?\
		(((offset >= 0) && (offset < PERF_KEY_MAX)) ?\
			(u64 *)(perf_ptr + offset)\
				: (u64 *)NULL) : (u64 *)NULL)

static inline int64_t getnstimediff(struct timespec64 *start)
{
	int64_t ns;
	struct timespec64 ts, b;

	ktime_get_boottime_ts64(&ts);
	b = timespec64_sub(ts, *start);
	ns = timespec64_to_ns(&b);
	return ns;
}

static void fastrpc_free_map(struct kref *ref)
{
	struct fastrpc_map *map;
	struct fastrpc_user *fl;

	map = container_of(ref, struct fastrpc_map, refcount);

	fl = map->fl;
	if (!fl)
		return;

	if (map->table) {
		if (map->attr & FASTRPC_ATTR_SECUREMAP) {
			struct qcom_scm_vmperm perm;
			int vmid = fl->cctx->vmperms[0].vmid;
			u64 src_perms = BIT(QCOM_SCM_VMID_HLOS) | BIT(vmid);
			int err = 0;

			perm.vmid = QCOM_SCM_VMID_HLOS;
			perm.perm = QCOM_SCM_PERM_RWX;
			err = qcom_scm_assign_mem(map->phys, map->len,
				&src_perms, &perm, 1);
			if (err) {
				dev_err(fl->sctx->dev, "Failed to assign memory phys 0x%llx size 0x%llx err %d\n",
						map->phys, map->len, err);
				return;
			}
		}
		mutex_lock(&fl->sctx->map_mutex);
		if (!fl->sctx->dev) {
			mutex_unlock(&fl->sctx->map_mutex);
			return;
		}
		dma_buf_unmap_attachment_unlocked(map->attach, map->table,
						  DMA_BIDIRECTIONAL);
		dma_buf_detach(map->buf, map->attach);
		dma_buf_put(map->buf);
		mutex_unlock(&fl->sctx->map_mutex);
	}

	if (map->fl) {
		spin_lock(&map->fl->lock);
		list_del(&map->node);
		spin_unlock(&map->fl->lock);
		map->fl = NULL;
	}

	kfree(map);
}

static void fastrpc_map_put(struct fastrpc_map *map)
{
	if (map)
		kref_put(&map->refcount, fastrpc_free_map);
}

static int fastrpc_map_get(struct fastrpc_map *map)
{
	if (!map)
		return -ENOENT;

	return kref_get_unless_zero(&map->refcount) ? 0 : -ENOENT;
}


static int fastrpc_map_lookup(struct fastrpc_user *fl, int fd,
			u64 va, u64 len, struct fastrpc_map **ppmap, bool take_ref)
{
	struct fastrpc_session_ctx *sess = fl->sctx;
	struct fastrpc_map *map = NULL;
	int ret = -ENOENT;

	spin_lock(&fl->lock);
	list_for_each_entry(map, &fl->maps, node) {
		if (map->fd != fd || va < (u64)map->va ||
				va + len > (u64)map->va + map->size)
			continue;

		if (take_ref) {
			ret = fastrpc_map_get(map);
			if (ret) {
				dev_dbg(sess->dev, "%s: Failed to get map fd=%d ret=%d\n",
					__func__, fd, ret);
				break;
			}
		}

		*ppmap = map;
		ret = 0;
		break;
	}
	spin_unlock(&fl->lock);

	return ret;
}

static void __fastrpc_buf_free(struct fastrpc_buf *buf)
{
	dma_free_coherent(buf->dev, buf->size, buf->virt,
			  FASTRPC_PHYS(buf->phys));
	kfree(buf);
}

static void fastrpc_buf_free(struct fastrpc_buf *buf)
{
	struct fastrpc_user *fl = buf->fl;

	if (!fl)
		return;

	mutex_lock(&fl->sctx->map_mutex);
	if (fl->sctx->dev)
		__fastrpc_buf_free(buf);
	mutex_unlock(&fl->sctx->map_mutex);
}

static int __fastrpc_buf_alloc(struct fastrpc_user *fl, struct device *dev,
			     u64 size, struct fastrpc_buf **obuf)
{
	struct fastrpc_buf *buf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	INIT_LIST_HEAD(&buf->attachments);
	INIT_LIST_HEAD(&buf->node);
	mutex_init(&buf->lock);

	buf->fl = fl;
	buf->virt = NULL;
	buf->phys = 0;
	buf->size = size;
	buf->dev = dev;
	buf->raddr = 0;

	mutex_lock(&fl->sctx->map_mutex);
	if (fl->sctx->dev)
		buf->virt = dma_alloc_coherent(dev, buf->size, (dma_addr_t *)&buf->phys,
						   GFP_KERNEL);
	mutex_unlock(&fl->sctx->map_mutex);
	if (!buf->virt) {
		mutex_destroy(&buf->lock);
		kfree(buf);
		return -ENOMEM;
	}

	*obuf = buf;

	return 0;
}

static int fastrpc_buf_alloc(struct fastrpc_user *fl, struct device *dev,
			     u64 size, struct fastrpc_buf **obuf)
{
	int ret;
	struct fastrpc_buf *buf;

	ret = __fastrpc_buf_alloc(fl, dev, size, obuf);
	if (ret)
		return ret;

	buf = *obuf;

	if (fl->sctx && fl->sctx->sid)
		buf->phys += ((u64)fl->sctx->sid << 32);

	return 0;
}

static int fastrpc_remote_heap_alloc(struct fastrpc_user *fl, struct device *dev,
				     u64 size, struct fastrpc_buf **obuf)
{
	struct device *rdev = &fl->cctx->rpdev->dev;

	return  __fastrpc_buf_alloc(fl, rdev, size, obuf);
}

static void fastrpc_channel_ctx_free(struct kref *ref)
{
	struct fastrpc_channel_ctx *cctx;
	int i;

	cctx = container_of(ref, struct fastrpc_channel_ctx, refcount);
	for (i = 0; i < FASTRPC_MAX_SESSIONS; i++)
		mutex_destroy(&cctx->session[i].map_mutex);

	kfree(cctx);
}

static void fastrpc_channel_ctx_get(struct fastrpc_channel_ctx *cctx)
{
	kref_get(&cctx->refcount);
}

static void fastrpc_channel_ctx_put(struct fastrpc_channel_ctx *cctx)
{
	kref_put(&cctx->refcount, fastrpc_channel_ctx_free);
}

static void fastrpc_context_free(struct kref *ref)
{
	struct fastrpc_invoke_ctx *ctx;
	struct fastrpc_channel_ctx *cctx;
	unsigned long flags;
	int i;

	ctx = container_of(ref, struct fastrpc_invoke_ctx, refcount);
	cctx = ctx->cctx;

	for (i = 0; i < ctx->nbufs; i++)
		fastrpc_map_put(ctx->maps[i]);

	if (ctx->buf)
		fastrpc_buf_free(ctx->buf);

	spin_lock_irqsave(&cctx->lock, flags);
	idr_remove(&cctx->ctx_idr, ctx->ctxid >> 16);
	spin_unlock_irqrestore(&cctx->lock, flags);

	kfree(ctx->perf);
	kfree(ctx->maps);
	kfree(ctx->olaps);
	kfree(ctx);

	fastrpc_channel_ctx_put(cctx);
}

static void fastrpc_context_put(struct fastrpc_invoke_ctx *ctx)
{
	kref_put(&ctx->refcount, fastrpc_context_free);
}

#define CMP(aa, bb) ((aa) == (bb) ? 0 : (aa) < (bb) ? -1 : 1)
static int olaps_cmp(const void *a, const void *b)
{
	struct fastrpc_buf_overlap *pa = (struct fastrpc_buf_overlap *)a;
	struct fastrpc_buf_overlap *pb = (struct fastrpc_buf_overlap *)b;
	/* sort with lowest starting buffer first */
	int st = CMP(pa->start, pb->start);
	/* sort with highest ending buffer first */
	int ed = CMP(pb->end, pa->end);

	return st == 0 ? ed : st;
}

static void fastrpc_get_buff_overlaps(struct fastrpc_invoke_ctx *ctx)
{
	u64 max_end = 0;
	int i;

	for (i = 0; i < ctx->nbufs; ++i) {
		ctx->olaps[i].start = ctx->args[i].ptr;
		ctx->olaps[i].end = ctx->olaps[i].start + ctx->args[i].length;
		ctx->olaps[i].raix = i;
	}

	sort(ctx->olaps, ctx->nbufs, sizeof(*ctx->olaps), olaps_cmp, NULL);

	for (i = 0; i < ctx->nbufs; ++i) {
		/* Falling inside previous range */
		if (ctx->olaps[i].start < max_end) {
			ctx->olaps[i].mstart = max_end;
			ctx->olaps[i].mend = ctx->olaps[i].end;
			ctx->olaps[i].offset = max_end - ctx->olaps[i].start;

			if (ctx->olaps[i].end > max_end) {
				max_end = ctx->olaps[i].end;
			} else {
				ctx->olaps[i].mend = 0;
				ctx->olaps[i].mstart = 0;
			}

		} else  {
			ctx->olaps[i].mend = ctx->olaps[i].end;
			ctx->olaps[i].mstart = ctx->olaps[i].start;
			ctx->olaps[i].offset = 0;
			max_end = ctx->olaps[i].end;
		}
	}
}

static struct fastrpc_invoke_ctx *fastrpc_context_alloc(
			struct fastrpc_user *user, u32 kernel, u32 sc,
			struct fastrpc_invoke_v2 *inv2)
{
	struct fastrpc_channel_ctx *cctx = user->cctx;
	struct fastrpc_invoke_ctx *ctx = NULL;
	struct fastrpc_invoke_args *args = NULL;
	unsigned long flags;
	int ret;

	args = (struct fastrpc_invoke_args *)inv2->inv.args;
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&ctx->node);
	ctx->fl = user;
	ctx->nscalars = REMOTE_SCALARS_LENGTH(sc);
	ctx->nbufs = REMOTE_SCALARS_INBUFS(sc) +
		     REMOTE_SCALARS_OUTBUFS(sc);

	if (ctx->nscalars) {
		ctx->maps = kcalloc(ctx->nscalars,
				    sizeof(*ctx->maps), GFP_KERNEL);
		if (!ctx->maps) {
			kfree(ctx);
			return ERR_PTR(-ENOMEM);
		}
		ctx->olaps = kcalloc(ctx->nscalars,
				    sizeof(*ctx->olaps), GFP_KERNEL);
		if (!ctx->olaps) {
			kfree(ctx->maps);
			kfree(ctx);
			return ERR_PTR(-ENOMEM);
		}
		ctx->args = args;
		fastrpc_get_buff_overlaps(ctx);
	}

	/* Released in fastrpc_context_put() */
	fastrpc_channel_ctx_get(cctx);

	ctx->crc = (u32 *)(uintptr_t)inv2->crc;
	ctx->perf_dsp = (u64 *)(uintptr_t)inv2->perf_dsp;
	ctx->perf_kernel = (u64 *)(uintptr_t)inv2->perf_kernel;
	ctx->poll_timeout = (u64)inv2->poll_timeout;
	if (ctx->perf_kernel) {
		ctx->perf = kzalloc(sizeof(*(ctx->perf)), GFP_KERNEL);
		if (!ctx->perf)
			return ERR_PTR(-ENOMEM);
	}
	ctx->sc = sc;
	ctx->retval = -1;
	ctx->pid = current->pid;
	ctx->tgid = user->client_id;
	ctx->cctx = cctx;
	ctx->rsp_flags = NORMAL_RESPONSE;
	ctx->is_work_done = false;
	init_completion(&ctx->work);

	spin_lock(&user->lock);
	list_add_tail(&ctx->node, &user->pending);
	spin_unlock(&user->lock);

	spin_lock_irqsave(&cctx->lock, flags);
	ret = idr_alloc_cyclic(&cctx->ctx_idr, ctx, 1,
			       FASTRPC_CTX_MAX, GFP_ATOMIC);
	if (ret < 0) {
		spin_unlock_irqrestore(&cctx->lock, flags);
		goto err_idr;
	}
	ctx->ctxid = ret << 16;
	spin_unlock_irqrestore(&cctx->lock, flags);

	kref_init(&ctx->refcount);

	return ctx;
err_idr:
	spin_lock(&user->lock);
	list_del(&ctx->node);
	spin_unlock(&user->lock);
	fastrpc_channel_ctx_put(cctx);
	kfree(ctx->maps);
	kfree(ctx->olaps);
	kfree(ctx);

	return ERR_PTR(ret);
}

static struct sg_table *
fastrpc_map_dma_buf(struct dma_buf_attachment *attachment,
		    enum dma_data_direction dir)
{
	struct fastrpc_dma_buf_attachment *a = attachment->priv;
	struct sg_table *table;
	int ret;

	table = &a->sgt;

	ret = dma_map_sgtable(attachment->dev, table, dir, 0);
	if (ret)
		table = ERR_PTR(ret);
	return table;
}

static void fastrpc_unmap_dma_buf(struct dma_buf_attachment *attach,
				  struct sg_table *table,
				  enum dma_data_direction dir)
{
	dma_unmap_sgtable(attach->dev, table, dir, 0);
}

static void fastrpc_release(struct dma_buf *dmabuf)
{
	struct fastrpc_buf *buffer = dmabuf->priv;

	fastrpc_buf_free(buffer);
}

static int fastrpc_dma_buf_attach(struct dma_buf *dmabuf,
				  struct dma_buf_attachment *attachment)
{
	struct fastrpc_dma_buf_attachment *a;
	struct fastrpc_buf *buffer = dmabuf->priv;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = dma_get_sgtable(buffer->dev, &a->sgt, buffer->virt,
			      FASTRPC_PHYS(buffer->phys), buffer->size);
	if (ret < 0) {
		dev_err(buffer->dev, "failed to get scatterlist from DMA API\n");
		kfree(a);
		return -EINVAL;
	}

	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->node);
	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->node, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void fastrpc_dma_buf_detatch(struct dma_buf *dmabuf,
				    struct dma_buf_attachment *attachment)
{
	struct fastrpc_dma_buf_attachment *a = attachment->priv;
	struct fastrpc_buf *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->node);
	mutex_unlock(&buffer->lock);
	sg_free_table(&a->sgt);
	kfree(a);
}

static int fastrpc_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct fastrpc_buf *buf = dmabuf->priv;

	iosys_map_set_vaddr(map, buf->virt);

	return 0;
}

static int fastrpc_mmap(struct dma_buf *dmabuf,
			struct vm_area_struct *vma)
{
	struct fastrpc_buf *buf = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;

	dma_resv_assert_held(dmabuf->resv);

	return dma_mmap_coherent(buf->dev, vma, buf->virt,
				 FASTRPC_PHYS(buf->phys), size);
}

static const struct dma_buf_ops fastrpc_dma_buf_ops = {
	.attach = fastrpc_dma_buf_attach,
	.detach = fastrpc_dma_buf_detatch,
	.map_dma_buf = fastrpc_map_dma_buf,
	.unmap_dma_buf = fastrpc_unmap_dma_buf,
	.mmap = fastrpc_mmap,
	.vmap = fastrpc_vmap,
	.release = fastrpc_release,
};

static int fastrpc_map_create(struct fastrpc_user *fl, int fd,
				u64 va, u64 len, u32 attr,
				struct fastrpc_map **ppmap, bool take_ref)
{
	struct fastrpc_session_ctx *sess = fl->sctx;
	struct fastrpc_map *map = NULL;
	struct sg_table *table;
	struct scatterlist *sgl = NULL;
	struct device *dev = NULL;
	int err = 0, sgl_index = 0;

	if (!fastrpc_map_lookup(fl, fd, va, len, ppmap, take_ref))
		return 0;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	INIT_LIST_HEAD(&map->node);
	kref_init(&map->refcount);

	map->fl = fl;
	map->fd = fd;
	map->buf = dma_buf_get(fd);
	if (IS_ERR(map->buf)) {
		err = PTR_ERR(map->buf);
		goto get_err;
	}

	if (attr & FASTRPC_ATTR_NOMAP)
		dev = &fl->cctx->rpdev->dev;
	else
		dev = sess->dev;

	mutex_lock(&fl->sctx->map_mutex);
	if (!fl->sctx->dev) {
		err = -ENODEV;
		mutex_unlock(&fl->sctx->map_mutex);
		goto attach_err;
	}

	map->attach = dma_buf_attach(map->buf, dev);
	if (IS_ERR(map->attach)) {
		dev_err(dev, "Failed to attach dmabuf\n");
		err = PTR_ERR(map->attach);
		mutex_unlock(&fl->sctx->map_mutex);
		goto attach_err;
	}

	table = dma_buf_map_attachment_unlocked(map->attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(table)) {
		err = PTR_ERR(table);
		mutex_unlock(&fl->sctx->map_mutex);
		goto map_err;
	}
	map->table = table;
	mutex_unlock(&fl->sctx->map_mutex);

	if (attr & FASTRPC_ATTR_SECUREMAP) {
		map->phys = sg_phys(map->table->sgl);
	} else {
		map->phys = sg_dma_address(map->table->sgl);
		if (!(attr & FASTRPC_ATTR_NOMAP))
			map->phys += ((u64)fl->sctx->sid << 32);
	}
	for_each_sg(map->table->sgl, sgl, map->table->nents,
		sgl_index)
		map->size += sg_dma_len(sgl);
	map->va = (void *)(uintptr_t)va;
	map->len = len;

	if (attr & FASTRPC_ATTR_SECUREMAP) {
		/*
		 * If subsystem VMIDs are defined in DTSI, then do
		 * hyp_assign from HLOS to those VM(s)
		 */
		u64 src_perms = BIT(QCOM_SCM_VMID_HLOS);
		struct qcom_scm_vmperm dst_perms[2] = {0};

		dst_perms[0].vmid = QCOM_SCM_VMID_HLOS;
		dst_perms[0].perm = QCOM_SCM_PERM_RW;
		dst_perms[1].vmid = fl->cctx->vmperms[0].vmid;
		dst_perms[1].perm = QCOM_SCM_PERM_RWX;
		map->attr = attr;
		err = qcom_scm_assign_mem(map->phys, (u64)map->len, &src_perms, dst_perms, 2);
		if (err) {
			dev_err(dev, "Failed to assign memory with phys 0x%llx size 0x%llx err %d\n",
					map->phys, map->len, err);
			goto map_err;
		}
	}
	spin_lock(&fl->lock);
	list_add_tail(&map->node, &fl->maps);
	spin_unlock(&fl->lock);
	*ppmap = map;

	return 0;

map_err:
	dma_buf_detach(map->buf, map->attach);
attach_err:
	dma_buf_put(map->buf);
get_err:
	fastrpc_map_put(map);

	return err;
}

/*
 * Fastrpc payload buffer with metadata looks like:
 *
 * >>>>>>  START of METADATA <<<<<<<<<
 * +---------------------------------+
 * |           Arguments             |
 * | type:(union fastrpc_remote_arg)|
 * |             (0 - N)             |
 * +---------------------------------+
 * |         Invoke Buffer list      |
 * | type:(struct fastrpc_invoke_buf)|
 * |           (0 - N)               |
 * +---------------------------------+
 * |         Page info list          |
 * | type:(struct fastrpc_phy_page)  |
 * |             (0 - N)             |
 * +---------------------------------+
 * |         Optional info           |
 * |(can be specific to SoC/Firmware)|
 * +---------------------------------+
 * >>>>>>>>  END of METADATA <<<<<<<<<
 * +---------------------------------+
 * |         Inline ARGS             |
 * |            (0-N)                |
 * +---------------------------------+
 */

static int fastrpc_get_meta_size(struct fastrpc_invoke_ctx *ctx)
{
	int size = 0;

	size = (sizeof(struct fastrpc_remote_buf) +
		sizeof(struct fastrpc_invoke_buf) +
		sizeof(struct fastrpc_phy_page)) * ctx->nscalars +
		sizeof(u64) * FASTRPC_MAX_FDLIST +
		sizeof(u32) * FASTRPC_MAX_CRCLIST +
		sizeof(u32) + sizeof(u64) * FASTRPC_DSP_PERF_LIST;

	return size;
}

static u64 fastrpc_get_payload_size(struct fastrpc_invoke_ctx *ctx, int metalen)
{
	u64 size = 0;
	int oix;

	size = ALIGN(metalen, FASTRPC_ALIGN);
	for (oix = 0; oix < ctx->nbufs; oix++) {
		int i = ctx->olaps[oix].raix;

		if (ctx->args[i].fd == 0 || ctx->args[i].fd == -1) {

			if (ctx->olaps[oix].offset == 0)
				size = ALIGN(size, FASTRPC_ALIGN);

			size += (ctx->olaps[oix].mend - ctx->olaps[oix].mstart);
		}
	}

	return size;
}

static int fastrpc_create_maps(struct fastrpc_invoke_ctx *ctx)
{
	struct device *dev = ctx->fl->sctx->dev;
	int i, err;

	for (i = 0; i < ctx->nscalars; ++i) {
		bool take_ref = true;

		if (ctx->args[i].fd == 0 || ctx->args[i].fd == -1 ||
		    ctx->args[i].length == 0)
			continue;

		if (i >= ctx->nbufs)
			take_ref = false;
		err = fastrpc_map_create(ctx->fl, ctx->args[i].fd,
			 (u64)ctx->args[i].ptr, ctx->args[i].length,
			 ctx->args[i].attr, &ctx->maps[i], take_ref);
		if (err) {
			dev_err(dev, "Error Creating map %d\n", err);
			return -EINVAL;
		}

	}
	return 0;
}

static struct fastrpc_invoke_buf *fastrpc_invoke_buf_start(union fastrpc_remote_arg *pra, int len)
{
	return (struct fastrpc_invoke_buf *)(&pra[len]);
}

static struct fastrpc_phy_page *fastrpc_phy_page_start(struct fastrpc_invoke_buf *buf, int len)
{
	return (struct fastrpc_phy_page *)(&buf[len]);
}

static int fastrpc_get_args(u32 kernel, struct fastrpc_invoke_ctx *ctx)
{
	struct device *dev = ctx->fl->sctx->dev;
	union fastrpc_remote_arg *rpra;
	struct fastrpc_invoke_buf *list;
	struct fastrpc_phy_page *pages;
	int inbufs, i, oix, err = 0;
	u64 len, rlen, pkt_size;
	u64 pg_start, pg_end;
	uintptr_t args;
	int metalen;

	inbufs = REMOTE_SCALARS_INBUFS(ctx->sc);
	metalen = fastrpc_get_meta_size(ctx);
	pkt_size = fastrpc_get_payload_size(ctx, metalen);

	PERF(ctx->perf_kernel, GET_COUNTER((u64 *)ctx->perf, PERF_MAP),
	err = fastrpc_create_maps(ctx);
	PERF_END);
	if (err)
		return err;

	ctx->msg_sz = pkt_size;

	err = fastrpc_buf_alloc(ctx->fl, dev, pkt_size, &ctx->buf);
	if (err)
		return err;

	memset(ctx->buf->virt, 0, pkt_size);
	rpra = ctx->buf->virt;
	list = fastrpc_invoke_buf_start(rpra, ctx->nscalars);
	pages = fastrpc_phy_page_start(list, ctx->nscalars);
	args = (uintptr_t)ctx->buf->virt + metalen;
	rlen = pkt_size - metalen;
	ctx->rpra = rpra;

	for (oix = 0; oix < ctx->nbufs; ++oix) {
		int mlen;

		i = ctx->olaps[oix].raix;
		len = ctx->args[i].length;

		rpra[i].buf.pv = 0;
		rpra[i].buf.len = len;
		list[i].num = len ? 1 : 0;
		list[i].pgidx = i;

		if (!len)
			continue;

		if (ctx->maps[i]) {
			PERF(ctx->perf_kernel, GET_COUNTER((u64 *)ctx->perf, PERF_MAP),
			struct vm_area_struct *vma = NULL;

			rpra[i].buf.pv = (u64) ctx->args[i].ptr;
			pages[i].addr = ctx->maps[i]->phys;

			mmap_read_lock(current->mm);
			vma = find_vma(current->mm, ctx->args[i].ptr);
			if (vma)
				pages[i].addr += (ctx->args[i].ptr & PAGE_MASK) -
						 vma->vm_start;
			mmap_read_unlock(current->mm);

			pg_start = (ctx->args[i].ptr & PAGE_MASK) >> PAGE_SHIFT;
			pg_end = ((ctx->args[i].ptr + len - 1) & PAGE_MASK) >>
				  PAGE_SHIFT;
			pages[i].size = (pg_end - pg_start + 1) * PAGE_SIZE;
			PERF_END);
		} else {
			PERF(ctx->perf_kernel, GET_COUNTER((u64 *)ctx->perf, PERF_COPY),
			if (ctx->olaps[oix].offset == 0) {
				rlen -= ALIGN(args, FASTRPC_ALIGN) - args;
				args = ALIGN(args, FASTRPC_ALIGN);
			}

			mlen = ctx->olaps[oix].mend - ctx->olaps[oix].mstart;

			if (rlen < mlen)
				goto bail;

			rpra[i].buf.pv = args - ctx->olaps[oix].offset;
			pages[i].addr = ctx->buf->phys -
					ctx->olaps[oix].offset +
					(pkt_size - rlen);
			pages[i].addr = pages[i].addr &	PAGE_MASK;

			pg_start = (rpra[i].buf.pv & PAGE_MASK) >> PAGE_SHIFT;
			pg_end = ((rpra[i].buf.pv + len - 1) & PAGE_MASK) >> PAGE_SHIFT;
			pages[i].size = (pg_end - pg_start + 1) * PAGE_SIZE;
			args = args + mlen;
			rlen -= mlen;
			PERF_END);
		}

		if (i < inbufs && !ctx->maps[i]) {
			void *dst = (void *)(uintptr_t)rpra[i].buf.pv;
			void *src = (void *)(uintptr_t)ctx->args[i].ptr;

			PERF(ctx->perf_kernel, GET_COUNTER((u64 *)ctx->perf, PERF_COPY),
			if (!kernel) {
				if (copy_from_user(dst, (void __user *)src,
						   len)) {
					err = -EFAULT;
					goto bail;
				}
			} else {
				memcpy(dst, src, len);
			}
			PERF_END);
		}
	}

	for (i = ctx->nbufs; i < ctx->nscalars; ++i) {
		list[i].num = ctx->args[i].length ? 1 : 0;
		list[i].pgidx = i;
		if (ctx->maps[i]) {
			pages[i].addr = ctx->maps[i]->phys;
			pages[i].size = ctx->maps[i]->size;
		}
		rpra[i].dma.fd = ctx->args[i].fd;
		rpra[i].dma.len = ctx->args[i].length;
		rpra[i].dma.offset = (u64) ctx->args[i].ptr;
	}

bail:
	if (err)
		dev_err(dev, "Error: get invoke args failed:%d\n", err);

	return err;
}

static int fastrpc_put_args(struct fastrpc_invoke_ctx *ctx,
			    u32 kernel)
{
	union fastrpc_remote_arg *rpra = ctx->rpra;
	struct fastrpc_user *fl = ctx->fl;
	struct fastrpc_map *mmap = NULL;
	struct fastrpc_invoke_buf *list;
	struct fastrpc_phy_page *pages;
	u64 *fdlist, *perf_dsp_list;
	u32 *crclist, *poll;
	int i, inbufs, outbufs, handles, perferr;

	inbufs = REMOTE_SCALARS_INBUFS(ctx->sc);
	outbufs = REMOTE_SCALARS_OUTBUFS(ctx->sc);
	handles = REMOTE_SCALARS_INHANDLES(ctx->sc) + REMOTE_SCALARS_OUTHANDLES(ctx->sc);
	list = fastrpc_invoke_buf_start(rpra, ctx->nscalars);
	pages = fastrpc_phy_page_start(list, ctx->nscalars);
	fdlist = (uint64_t *)(pages + inbufs + outbufs + handles);
	crclist = (u32 *)(fdlist + FASTRPC_MAX_FDLIST);
	poll = (u32 *)(crclist + FASTRPC_MAX_CRCLIST);
	perf_dsp_list = (u64 *)(poll + 1);

	for (i = inbufs; i < ctx->nbufs; ++i) {
		if (!ctx->maps[i]) {
			void *src = (void *)(uintptr_t)rpra[i].buf.pv;
			void *dst = (void *)(uintptr_t)ctx->args[i].ptr;
			u64 len = rpra[i].buf.len;

			if (!kernel) {
				if (copy_to_user((void __user *)dst, src, len))
					return -EFAULT;
			} else {
				memcpy(dst, src, len);
			}
		}
	}

	/* Clean up fdlist which is updated by DSP */
	for (i = 0; i < FASTRPC_MAX_FDLIST; i++) {
		if (!fdlist[i])
			break;
		if (!fastrpc_map_lookup(fl, (int)fdlist[i], 0, 0, &mmap, false))
			fastrpc_map_put(mmap);
	}

	if (ctx->crc && crclist && rpra) {
		if (copy_to_user((void __user *)ctx->crc, crclist,
				FASTRPC_MAX_CRCLIST * sizeof(u32)))
			return -EFAULT;
	}

	if (ctx->perf_dsp && perf_dsp_list) {
		perferr = copy_to_user((void __user *)ctx->perf_dsp,
				perf_dsp_list, FASTRPC_DSP_PERF_LIST * sizeof(u64));
		if (perferr)
			dev_info(fl->sctx->dev, "Warning: failed to copy perf data %d\n", perferr);
	}

	return 0;
}

static int fastrpc_invoke_send(struct fastrpc_session_ctx *sctx,
			       struct fastrpc_invoke_ctx *ctx,
			       u32 kernel, uint32_t handle)
{
	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_user *fl = ctx->fl;
	struct fastrpc_msg *msg = &ctx->msg;
	int ret;

	cctx = fl->cctx;
	msg->pid = fl->client_id;
	msg->tid = current->pid;

	if (kernel)
		msg->pid = 0;

	msg->ctx = ctx->ctxid | fl->pd;
	msg->handle = handle;
	msg->sc = ctx->sc;
	msg->addr = ctx->buf ? ctx->buf->phys : 0;
	msg->size = roundup(ctx->msg_sz, PAGE_SIZE);

	if (cctx->rpdev == NULL)
		return -EPIPE;

	ret = rpmsg_send(cctx->rpdev->ept, (void *)msg, sizeof(*msg));

	return ret;

}

static int fastrpc_get_spd_session(struct fastrpc_channel_ctx *cctx,
				char *servloc_name)
{
	int i, session = -1;

	for (i = 0; i < FASTRPC_MAX_SPD ; i++) {
		if (!cctx->spd[i].servloc_name)
			continue;
		if (!strcmp(servloc_name, cctx->spd[i].servloc_name)) {
			session = i;
			break;
		}
	}

	return session;
}

static void fastrpc_update_invoke_count(u32 handle, u64 *perf_counter,
					struct timespec64 *invoket)
{
	/* update invoke count for dynamic handles */
	u64 *invcount, *count;

	if (handle <= FASTRPC_MAX_STATIC_HANDLE)
		return;

	invcount = GET_COUNTER(perf_counter, PERF_INVOKE);
	if (invcount)
		*invcount += getnstimediff(invoket);

	count = GET_COUNTER(perf_counter, PERF_COUNT);
	if (count)
		(*count)++;
}

static int poll_for_remote_response(struct fastrpc_invoke_ctx *ctx, u64 timeout)
{
	int err = -EIO, i, j;
	u32 sc = ctx->sc;
	struct fastrpc_invoke_buf *list;
	struct fastrpc_phy_page *pages;
	u64 *fdlist = NULL;
	u32 *crclist = NULL, *poll = NULL;
	unsigned int inbufs, outbufs, handles;

	/* calculate poll memory location */
	inbufs = REMOTE_SCALARS_INBUFS(sc);
	outbufs = REMOTE_SCALARS_OUTBUFS(sc);
	handles = REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc);
	list = fastrpc_invoke_buf_start(ctx->rpra, ctx->nscalars);
	pages = fastrpc_phy_page_start(list, ctx->nscalars);
	fdlist = (u64 *)(pages + inbufs + outbufs + handles);
	crclist = (u32 *)(fdlist + FASTRPC_MAX_FDLIST);
	poll = (u32 *)(crclist + FASTRPC_MAX_CRCLIST);

	/* poll on memory for DSP response. Return failure on timeout */
	for (i = 0, j = 0; i < timeout; i++, j++) {
		if (*poll == FASTRPC_POLL_RESPONSE) {
			err = 0;
			ctx->is_work_done = true;
			ctx->retval = 0;
			break;
		}
		if (j == FASTRPC_POLL_TIME_MEM_UPDATE) {
			/* make sure that all poll memory writes by DSP are seen by CPU */
			dma_rmb();
			j = 0;
		}
		udelay(1);
	}
	return err;
}

static inline int fastrpc_wait_for_response(struct fastrpc_invoke_ctx *ctx,
						u32 kernel)
{
	int err = 0;

	if (kernel) {
		if (!wait_for_completion_timeout(&ctx->work, 10 * HZ))
			err = -ETIMEDOUT;
	} else {
		err = wait_for_completion_interruptible(&ctx->work);
	}

	return err;
}

static int fastrpc_wait_for_completion(struct fastrpc_invoke_ctx *ctx,
										u32 kernel)
{
	int err;

	do {
		switch (ctx->rsp_flags) {
		case NORMAL_RESPONSE:
			err = fastrpc_wait_for_response(ctx, kernel);
			if (err || ctx->is_work_done)
				return err;
			break;
		case POLL_MODE:
			err = poll_for_remote_response(ctx, ctx->poll_timeout);
			/* If polling timed out, move to normal response mode */
			if (err)
				ctx->rsp_flags = NORMAL_RESPONSE;
			break;
		default:
			err = -EBADR;
			dev_dbg(ctx->fl->sctx->dev,
				"unsupported response type:0x%x\n", ctx->rsp_flags);
			break;
		}
	} while (!ctx->is_work_done);

	return err;
}

static int fastrpc_internal_invoke(struct fastrpc_user *fl,
			u32 kernel, struct fastrpc_invoke_v2 *inv2)
{
	struct fastrpc_invoke_ctx *ctx = NULL;
	struct fastrpc_invoke inv;
	u32 handle, sc;
	int err = 0, perferr = 0;
	struct timespec64 invoket = {0};

	if (inv2->perf_kernel)
		ktime_get_boottime_ts64(&invoket);

	if (!fl->sctx)
		return -EINVAL;

	if (!fl->cctx->rpdev)
		return -EPIPE;

	inv = inv2->inv;
	handle = inv.handle;
	sc = inv.sc;
	if (handle == FASTRPC_INIT_HANDLE && !kernel) {
		dev_warn_ratelimited(fl->sctx->dev, "user app trying to send a kernel RPC message (%d)\n",  handle);
		return -EPERM;
	}

	ctx = fastrpc_context_alloc(fl, kernel, sc, inv2);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	PERF(ctx->perf_kernel, GET_COUNTER((u64 *)ctx->perf, PERF_GETARGS),
	err = fastrpc_get_args(kernel, ctx);
	PERF_END);
	if (err)
		goto bail;

	/* make sure that all CPU memory writes are seen by DSP */
	dma_wmb();
	PERF(ctx->perf_kernel, GET_COUNTER((u64 *)ctx->perf, PERF_LINK),
	/* Send invoke buffer to remote dsp */
	err = fastrpc_invoke_send(fl->sctx, ctx, kernel, handle);
	PERF_END);
	if (err)
		goto bail;

	if (ctx->poll_timeout != 0 && handle > FASTRPC_MAX_STATIC_HANDLE &&
		fl->cctx->domain_id == CDSP_DOMAIN_ID &&
		fl->pd == USER_PD)
		ctx->rsp_flags = POLL_MODE;

	err = fastrpc_wait_for_completion(ctx, kernel);
	if (err)
		goto bail;

	if (!ctx->is_work_done) {
		err = -ETIMEDOUT;
		dev_dbg(fl->sctx->dev, "Invalid workdone state for handle 0x%x, sc 0x%x\n",
			handle, sc);
		goto bail;
	}

	/* make sure that all memory writes by DSP are seen by CPU */
	dma_rmb();
	PERF(ctx->perf_kernel, GET_COUNTER((u64 *)ctx->perf, PERF_PUTARGS),
	/* populate all the output buffers with results */
	err = fastrpc_put_args(ctx, kernel);
	PERF_END);
	if (err)
		goto bail;

	/* Check the response from remote dsp */
	err = ctx->retval;
	if (err)
		goto bail;

bail:
	if (err != -ERESTARTSYS && err != -ETIMEDOUT) {
		if (ctx) {
			if (ctx->perf_kernel && !err)
				fastrpc_update_invoke_count(handle, (u64 *)ctx->perf, &invoket);
			if (ctx->perf_kernel && ctx->perf) {
				perferr = copy_to_user((void __user *)ctx->perf_kernel,
						ctx->perf, FASTRPC_KERNEL_PERF_LIST * sizeof(u64));
				if (perferr)
					dev_info(fl->sctx->dev,
						"Warning: failed to copy perf data %d\n", perferr);
			}
		}
		/* We are done with this compute context */
		spin_lock(&fl->lock);
		list_del(&ctx->node);
		spin_unlock(&fl->lock);
		fastrpc_context_put(ctx);
	}

	if (err)
		dev_dbg(fl->sctx->dev, "Error: Invoke Failed %d\n", err);

	return err;
}

static bool is_session_rejected(struct fastrpc_user *fl, bool unsigned_pd_request)
{
	/* Check if the device node is non-secure and channel is secure*/
	if (!fl->is_secure_dev && fl->cctx->secure) {
		/*
		 * Allow untrusted applications to offload only to Unsigned PD when
		 * channel is configured as secure and block untrusted apps on channel
		 * that does not support unsigned PD offload
		 */
		if (!fl->cctx->unsigned_support || !unsigned_pd_request) {
			dev_err(&fl->cctx->rpdev->dev, "Error: Untrusted application trying to offload to signed PD");
			return true;
		}
	}

	return false;
}

static int fastrpc_mmap_remove_ssr(struct fastrpc_channel_ctx *cctx)
{
	struct fastrpc_buf *buf, *b;
	int err = 0;

	list_for_each_entry_safe(buf, b, &cctx->rhmaps, node) {
		if (cctx->vmcount) {
			u64 src_perms = 0;
			struct qcom_scm_vmperm dst_perms;
			u32 i;

			for (i = 0; i < cctx->vmcount; i++)
				src_perms |= BIT(cctx->vmperms[i].vmid);

			dst_perms.vmid = QCOM_SCM_VMID_HLOS;
			dst_perms.perm = QCOM_SCM_PERM_RWX;
			err = qcom_scm_assign_mem(buf->phys, (u64)buf->size,
				&src_perms, &dst_perms, 1);
			if (err) {
				pr_err("%s: Failed to assign memory phys 0x%llx size 0x%llx err %d\n",
					__func__, buf->phys, buf->size, err);
				return err;
			}
		}
		list_del(&buf->node);
		__fastrpc_buf_free(buf);
	}

	return 0;
}

static int fastrpc_mmap_remove_pdr(struct fastrpc_user *fl)
{
	int err = 0, session = -1;

	if (!fl)
		return -EBADF;

	session = fastrpc_get_spd_session(fl->cctx, fl->servloc_name);
	if (session < 0)
		return -EUSERS;

	/* The service cannot be up before its initial static PD is created. */
	if (fl->cctx->spd[session].pdrcount &&
	    atomic_read(&fl->cctx->spd[session].ispdup) == 0)
		return -ENOTCONN;

	if (fl->cctx->spd[session].pdrcount !=
		fl->cctx->spd[session].prevpdrcount) {
		err = fastrpc_mmap_remove_ssr(fl->cctx);
		if (err)
			dev_info(&fl->cctx->rpdev->dev,
				"failed to unmap remote heap (err %d)\n", err);
		fl->cctx->spd[session].prevpdrcount =
				fl->cctx->spd[session].pdrcount;
	}

	return err;
}

static int fastrpc_init_create_static_process(struct fastrpc_user *fl,
					      char __user *argp)
{
	struct fastrpc_init_create_static init;
	struct fastrpc_invoke_args *args;
	struct fastrpc_invoke_v2 ioctl = {0};
	struct fastrpc_phy_page pages[1];
	struct fastrpc_buf *buf = NULL;
	u64 phys = 0, size = 0;
	char *name;
	int err;
	bool scm_done = false;
	struct {
		int pgid;
		u32 namelen;
		u32 pageslen;
	} inbuf;

	args = kcalloc(FASTRPC_CREATE_STATIC_PROCESS_NARGS, sizeof(*args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	if (copy_from_user(&init, argp, sizeof(init))) {
		err = -EFAULT;
		goto err;
	}

	if (init.namelen > INIT_FILE_NAMELEN_MAX) {
		err = -EINVAL;
		goto err;
	}

	name = kzalloc(init.namelen, GFP_KERNEL);
	if (!name) {
		err = -ENOMEM;
		goto err;
	}

	if (copy_from_user(name, (void __user *)(uintptr_t)init.name, init.namelen)) {
		err = -EFAULT;
		goto err_name;
	}

	fl->servloc_name = AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME;

	if (!strcmp(name, "audiopd")) {
		/*
		 * Remove any previous mappings in case process is trying
		 * to reconnect after a PD restart on remote subsystem.
		 */
		err = fastrpc_mmap_remove_pdr(fl);
		if (err)
			goto err_name;
	}

	if (!fl->cctx->staticpd_status) {
		err = fastrpc_remote_heap_alloc(fl, fl->sctx->dev, init.memlen, &buf);
		if (err)
			goto err_name;

		phys = buf->phys;
		size = buf->size;
		/* Map if we have any heap VMIDs associated with this ADSP Static Process. */
		if (fl->cctx->vmcount) {
			u64 src_perms = BIT(QCOM_SCM_VMID_HLOS);

			err = qcom_scm_assign_mem(phys, (u64)size,
							&src_perms,
							fl->cctx->vmperms, fl->cctx->vmcount);
			if (err) {
				dev_err(fl->sctx->dev, "Failed to assign memory with phys 0x%llx size 0x%llx err %d",
					phys, size, err);
				goto err_map;
			}
			scm_done = true;
		}
		fl->cctx->staticpd_status = true;
		spin_lock(&fl->lock);
		list_add_tail(&buf->node, &fl->cctx->rhmaps);
		spin_unlock(&fl->lock);
	}

	inbuf.pgid = fl->client_id;
	inbuf.namelen = init.namelen;
	inbuf.pageslen = 0;
	fl->pd = USER_PD;

	args[0].ptr = (u64)(uintptr_t)&inbuf;
	args[0].length = sizeof(inbuf);
	args[0].fd = -1;

	args[1].ptr = (u64)(uintptr_t)name;
	args[1].length = inbuf.namelen;
	args[1].fd = -1;

	pages[0].addr = phys;
	pages[0].size = size;

	args[2].ptr = (u64)(uintptr_t) pages;
	args[2].length = sizeof(*pages);
	args[2].fd = -1;

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_CREATE_STATIC, 3, 0);
	ioctl.inv.args = (u64)args;
	err = fastrpc_internal_invoke(fl, true, &ioctl);
	if (err)
		goto err_invoke;

	kfree(args);
	kfree(name);

	return 0;
err_invoke:
	if (fl->cctx->vmcount && scm_done) {
		u64 src_perms = 0;
		struct qcom_scm_vmperm dst_perms;
		u32 i;

		for (i = 0; i < fl->cctx->vmcount; i++)
			src_perms |= BIT(fl->cctx->vmperms[i].vmid);

		dst_perms.vmid = QCOM_SCM_VMID_HLOS;
		dst_perms.perm = QCOM_SCM_PERM_RWX;
		err = qcom_scm_assign_mem(phys, (u64)size,
						&src_perms, &dst_perms, 1);
		if (err)
			dev_err(fl->sctx->dev, "Failed to assign memory phys 0x%llx size 0x%llx err %d",
				phys, size, err);
	}
err_map:
	fl->cctx->staticpd_status = false;
	spin_lock(&fl->lock);
	list_del(&buf->node);
	spin_unlock(&fl->lock);
	__fastrpc_buf_free(buf);
err_name:
	kfree(name);
err:
	kfree(args);

	return err;
}

static int fastrpc_init_create_process(struct fastrpc_user *fl,
					char __user *argp)
{
	struct fastrpc_init_create init;
	struct fastrpc_invoke_args *args;
	struct fastrpc_invoke_v2 ioctl = {0};
	struct fastrpc_phy_page pages[1];
	struct fastrpc_map *map = NULL;
	struct fastrpc_buf *imem = NULL;
	int memlen;
	int err;
	struct {
		int pgid;
		u32 namelen;
		u32 filelen;
		u32 pageslen;
		u32 attrs;
		u32 siglen;
	} inbuf;

	args = kcalloc(FASTRPC_CREATE_PROCESS_NARGS, sizeof(*args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	if (copy_from_user(&init, argp, sizeof(init))) {
		err = -EFAULT;
		goto err;
	}

	if (init.attrs & FASTRPC_MODE_UNSIGNED_MODULE)
		fl->is_unsigned_pd = true;

	if (is_session_rejected(fl, fl->is_unsigned_pd)) {
		err = -ECONNREFUSED;
		goto err;
	}

	if (init.filelen > INIT_FILELEN_MAX) {
		err = -EINVAL;
		goto err;
	}

	inbuf.pgid = fl->client_id;
	inbuf.namelen = strlen(current->comm) + 1;
	inbuf.filelen = init.filelen;
	inbuf.pageslen = 1;
	inbuf.attrs = init.attrs;
	inbuf.siglen = init.siglen;
	fl->pd = USER_PD;

	if (init.filelen && init.filefd) {
		err = fastrpc_map_create(fl, init.filefd, init.file,
				init.filelen, 0, &map, true);
		if (err)
			goto err;
	}

	memlen = ALIGN(max(INIT_FILELEN_MAX, (int)init.filelen * 4),
		       1024 * 1024);
	err = fastrpc_buf_alloc(fl, fl->sctx->dev, memlen,
				&imem);
	if (err)
		goto err_alloc;

	fl->init_mem = imem;
	args[0].ptr = (u64)(uintptr_t)&inbuf;
	args[0].length = sizeof(inbuf);
	args[0].fd = -1;

	args[1].ptr = (u64)(uintptr_t)current->comm;
	args[1].length = inbuf.namelen;
	args[1].fd = -1;

	args[2].ptr = (u64) init.file;
	args[2].length = inbuf.filelen;
	args[2].fd = init.filefd;

	pages[0].addr = imem->phys;
	pages[0].size = imem->size;

	args[3].ptr = (u64)(uintptr_t) pages;
	args[3].length = 1 * sizeof(*pages);
	args[3].fd = -1;

	args[4].ptr = (u64)(uintptr_t)&inbuf.attrs;
	args[4].length = sizeof(inbuf.attrs);
	args[4].fd = -1;

	args[5].ptr = (u64)(uintptr_t) &inbuf.siglen;
	args[5].length = sizeof(inbuf.siglen);
	args[5].fd = -1;

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_CREATE, 4, 0);
	if (init.attrs)
		ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_CREATE_ATTR, 4, 0);
	ioctl.inv.args = (u64)args;
	err = fastrpc_internal_invoke(fl, true, &ioctl);
	if (err)
		goto err_invoke;

	kfree(args);
	fastrpc_map_put(map);

	return 0;

err_invoke:
	fl->init_mem = NULL;
	fastrpc_buf_free(imem);
err_alloc:
	fastrpc_map_put(map);
err:
	kfree(args);

	return err;
}

static struct fastrpc_session_ctx *fastrpc_session_alloc(
					struct fastrpc_user *fl)
{
	struct fastrpc_channel_ctx *cctx = fl->cctx;
	struct fastrpc_session_ctx *session = NULL;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&cctx->lock, flags);
	for (i = 0; i < cctx->sesscount; i++) {
		if (!cctx->session[i].used && cctx->session[i].valid) {
			cctx->session[i].used = true;
			session = &cctx->session[i];
			fl->client_id = FASTRPC_CLIENTID_MASK | i;
			break;
		}
	}
	spin_unlock_irqrestore(&cctx->lock, flags);

	return session;
}

static void fastrpc_session_free(struct fastrpc_channel_ctx *cctx,
				 struct fastrpc_session_ctx *session)
{
	unsigned long flags;

	spin_lock_irqsave(&cctx->lock, flags);
	session->used = false;
	spin_unlock_irqrestore(&cctx->lock, flags);
}

static int fastrpc_release_current_dsp_process(struct fastrpc_user *fl)
{
	struct fastrpc_invoke_args args[1];
	struct fastrpc_invoke_v2 ioctl = {0};
	int tgid = 0;

	tgid = fl->client_id;
	args[0].ptr = (u64)(uintptr_t) &tgid;
	args[0].length = sizeof(tgid);
	args[0].fd = -1;

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_RELEASE, 1, 0);
	ioctl.inv.args = (u64)args;
	return fastrpc_internal_invoke(fl, true, &ioctl);
}

static int fastrpc_device_release(struct inode *inode, struct file *file)
{
	struct fastrpc_user *fl = (struct fastrpc_user *)file->private_data;
	struct fastrpc_channel_ctx *cctx = fl->cctx;
	struct fastrpc_invoke_ctx *ctx, *n;
	struct fastrpc_map *map, *m;
	struct fastrpc_buf *buf, *b;
	unsigned long flags;

	fastrpc_release_current_dsp_process(fl);

	spin_lock_irqsave(&cctx->lock, flags);
	list_del(&fl->user);
	spin_unlock_irqrestore(&cctx->lock, flags);

	if (fl->init_mem)
		fastrpc_buf_free(fl->init_mem);

	list_for_each_entry_safe(ctx, n, &fl->pending, node) {
		list_del(&ctx->node);
		fastrpc_context_put(ctx);
	}

	list_for_each_entry_safe(map, m, &fl->maps, node)
		fastrpc_map_put(map);

	list_for_each_entry_safe(buf, b, &fl->mmaps, node) {
		list_del(&buf->node);
		fastrpc_buf_free(buf);
	}

	fastrpc_session_free(cctx, fl->sctx);
	fastrpc_channel_ctx_put(cctx);

	mutex_destroy(&fl->mutex);
	kfree(fl);
	file->private_data = NULL;

	return 0;
}

static int fastrpc_device_open(struct inode *inode, struct file *filp)
{
	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_device *fdevice;
	struct fastrpc_user *fl = NULL;
	unsigned long flags;

	fdevice = miscdev_to_fdevice(filp->private_data);
	cctx = fdevice->cctx;

	if (!cctx->rpdev)
		return -ENODEV;

	fl = kzalloc(sizeof(*fl), GFP_KERNEL);
	if (!fl)
		return -ENOMEM;

	/* Released in fastrpc_device_release() */
	fastrpc_channel_ctx_get(cctx);

	filp->private_data = fl;
	spin_lock_init(&fl->lock);
	mutex_init(&fl->mutex);
	INIT_LIST_HEAD(&fl->pending);
	INIT_LIST_HEAD(&fl->maps);
	INIT_LIST_HEAD(&fl->mmaps);
	INIT_LIST_HEAD(&fl->user);
	fl->cctx = cctx;
	fl->is_secure_dev = fdevice->secure;

	fl->sctx = fastrpc_session_alloc(fl);
	if (!fl->sctx) {
		dev_err(&cctx->rpdev->dev, "No session available\n");
		mutex_destroy(&fl->mutex);
		kfree(fl);

		return -EBUSY;
	}

	spin_lock_irqsave(&cctx->lock, flags);
	list_add_tail(&fl->user, &cctx->users);
	spin_unlock_irqrestore(&cctx->lock, flags);

	return 0;
}

static int fastrpc_dmabuf_alloc(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_alloc_dma_buf bp;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct fastrpc_buf *buf = NULL;
	int err;

	if (copy_from_user(&bp, argp, sizeof(bp)))
		return -EFAULT;

	err = fastrpc_buf_alloc(fl, fl->sctx->dev, bp.size, &buf);
	if (err)
		return err;
	exp_info.ops = &fastrpc_dma_buf_ops;
	exp_info.size = bp.size;
	exp_info.flags = O_RDWR;
	exp_info.priv = buf;
	buf->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(buf->dmabuf)) {
		err = PTR_ERR(buf->dmabuf);
		fastrpc_buf_free(buf);
		return err;
	}

	bp.fd = dma_buf_fd(buf->dmabuf, O_ACCMODE);
	if (bp.fd < 0) {
		dma_buf_put(buf->dmabuf);
		return -EINVAL;
	}

	if (copy_to_user(argp, &bp, sizeof(bp))) {
		/*
		 * The usercopy failed, but we can't do much about it, as
		 * dma_buf_fd() already called fd_install() and made the
		 * file descriptor accessible for the current process. It
		 * might already be closed and dmabuf no longer valid when
		 * we reach this point. Therefore "leak" the fd and rely on
		 * the process exit path to do any required cleanup.
		 */
		return -EFAULT;
	}

	return 0;
}

static int fastrpc_init_attach(struct fastrpc_user *fl, int pd)
{
	struct fastrpc_invoke_args args[1];
	struct fastrpc_invoke_v2 ioctl = {0};
	int tgid = fl->client_id;

	args[0].ptr = (u64)(uintptr_t) &tgid;
	args[0].length = sizeof(tgid);
	args[0].fd = -1;
	fl->pd = pd;
	if (pd == SENSORS_PD) {
		if (fl->cctx->domain_id == ADSP_DOMAIN_ID)
			fl->servloc_name = SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME;
		else if (fl->cctx->domain_id == SDSP_DOMAIN_ID)
			fl->servloc_name = SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME;
	}

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_ATTACH, 1, 0);
	ioctl.inv.args = (u64)args;
	return fastrpc_internal_invoke(fl, true, &ioctl);
}

static int fastrpc_copy_args(struct fastrpc_invoke *inv)
{
	struct fastrpc_invoke_args *args = NULL;
	u32 nscalars;

	/* nscalars is truncated here to max supported value */
	nscalars = REMOTE_SCALARS_LENGTH(inv->sc);
	if (nscalars) {
		args = kcalloc(nscalars, sizeof(*args), GFP_KERNEL);
		if (!args)
			return -ENOMEM;

		if (copy_from_user(args, (void __user *)(uintptr_t)inv->args,
				   nscalars * sizeof(*args))) {
			kfree(args);
			return -EFAULT;
		}
	}
	inv->args = (u64)args;

	return 0;
}

static int fastrpc_invoke(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_invoke_v2 ioctl = {0};
	struct fastrpc_invoke inv;
	int err;

	if (copy_from_user(&inv, argp, sizeof(inv)))
		return -EFAULT;

	err = fastrpc_copy_args(&inv);
	if (err)
		return err;

	ioctl.inv = inv;
	err = fastrpc_internal_invoke(fl, false, &ioctl);
	kfree((void *)inv.args);

	return err;
}

static int fastrpc_invokev2(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_invoke_v2 inv2 = {0};
	int err;

	if (copy_from_user(&inv2, argp, sizeof(inv2)))
		return -EFAULT;

	err = fastrpc_copy_args(&inv2.inv);
	if (err)
		return err;

	err = fastrpc_internal_invoke(fl, false, &inv2);
	kfree((void *)inv2.inv.args);

	return err;
}

static int fastrpc_get_info_from_dsp(struct fastrpc_user *fl, uint32_t *dsp_attr_buf,
				     uint32_t dsp_attr_buf_len)
{
	struct fastrpc_invoke_args args[2] = { 0 };
	struct fastrpc_invoke_v2 ioctl = {0};

	/*
	 * Capability filled in userspace. This carries the information
	 * about the remoteproc support which is fetched from the remoteproc
	 * sysfs node by userspace.
	 */
	dsp_attr_buf[0] = 0;
	dsp_attr_buf_len -= 1;

	args[0].ptr = (u64)(uintptr_t)&dsp_attr_buf_len;
	args[0].length = sizeof(dsp_attr_buf_len);
	args[0].fd = -1;
	args[1].ptr = (u64)(uintptr_t)&dsp_attr_buf[1];
	args[1].length = dsp_attr_buf_len * sizeof(u32);
	args[1].fd = -1;

	ioctl.inv.handle = FASTRPC_DSP_UTILITIES_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(0, 1, 1);
	ioctl.inv.args = (u64)args;
	return fastrpc_internal_invoke(fl, true, &ioctl);
}

static int fastrpc_get_info_from_kernel(struct fastrpc_ioctl_capability *cap,
					struct fastrpc_user *fl)
{
	struct fastrpc_channel_ctx *cctx = fl->cctx;
	uint32_t attribute_id = cap->attribute_id;
	uint32_t *dsp_attributes;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&cctx->lock, flags);
	/* check if we already have queried dsp for attributes */
	if (cctx->valid_attributes) {
		spin_unlock_irqrestore(&cctx->lock, flags);
		goto done;
	}
	spin_unlock_irqrestore(&cctx->lock, flags);

	dsp_attributes = kzalloc(FASTRPC_MAX_DSP_ATTRIBUTES_LEN, GFP_KERNEL);
	if (!dsp_attributes)
		return -ENOMEM;

	err = fastrpc_get_info_from_dsp(fl, dsp_attributes, FASTRPC_MAX_DSP_ATTRIBUTES);
	if (err == DSP_UNSUPPORTED_API) {
		dev_info(&cctx->rpdev->dev,
			 "Warning: DSP capabilities not supported\n");
		kfree(dsp_attributes);
		return -EOPNOTSUPP;
	} else if (err) {
		dev_err(&cctx->rpdev->dev, "Error: dsp information is incorrect err: %d\n", err);
		kfree(dsp_attributes);
		return err;
	}

	spin_lock_irqsave(&cctx->lock, flags);
	memcpy(cctx->dsp_attributes, dsp_attributes, FASTRPC_MAX_DSP_ATTRIBUTES_LEN);
	cctx->valid_attributes = true;
	spin_unlock_irqrestore(&cctx->lock, flags);
	kfree(dsp_attributes);
done:
	cap->capability = cctx->dsp_attributes[attribute_id];
	return 0;
}

static int fastrpc_get_dsp_info(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_ioctl_capability cap = {0};
	int err = 0;

	if (copy_from_user(&cap, argp, sizeof(cap)))
		return  -EFAULT;

	cap.capability = 0;

	if (cap.attribute_id >= FASTRPC_MAX_DSP_ATTRIBUTES) {
		dev_err(&fl->cctx->rpdev->dev, "Error: invalid attribute: %d, err: %d\n",
			cap.attribute_id, err);
		return -EOVERFLOW;
	}

	err = fastrpc_get_info_from_kernel(&cap, fl);
	if (err)
		return err;

	if (copy_to_user(argp, &cap, sizeof(cap)))
		return -EFAULT;

	return 0;
}

static int fastrpc_req_munmap_impl(struct fastrpc_user *fl, struct fastrpc_buf *buf)
{
	struct fastrpc_invoke_args args[1] = { [0] = { 0 } };
	struct fastrpc_invoke_v2 ioctl = {0};
	struct fastrpc_munmap_req_msg req_msg;
	struct device *dev = fl->sctx->dev;
	int err;

	req_msg.pgid = fl->client_id;
	req_msg.size = buf->size;
	req_msg.vaddr = buf->raddr;

	args[0].ptr = (u64) (uintptr_t) &req_msg;
	args[0].length = sizeof(req_msg);

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MUNMAP, 1, 0);
	ioctl.inv.args = (u64)args;
	err = fastrpc_internal_invoke(fl, true, &ioctl);
	if (!err) {
		if (buf->flag == ADSP_MMAP_REMOTE_HEAP_ADDR) {
			if (fl->cctx->vmcount) {
				u64 src_perms = 0;
				struct qcom_scm_vmperm dst_perms;
				u32 i;

				for (i = 0; i < fl->cctx->vmcount; i++)
					src_perms |= BIT(fl->cctx->vmperms[i].vmid);

				dst_perms.vmid = QCOM_SCM_VMID_HLOS;
				dst_perms.perm = QCOM_SCM_PERM_RWX;
				err = qcom_scm_assign_mem(buf->phys, (u64)buf->size,
								&src_perms, &dst_perms, 1);
				if (err) {
					dev_err(fl->sctx->dev, "Failed to assign memory phys 0x%llx size 0x%llx err %d\n",
						buf->phys, buf->size, err);
					return err;
				}
			}
		}
		dev_dbg(dev, "unmmap\tpt 0x%09lx OK\n", buf->raddr);
		spin_lock(&fl->lock);
		list_del(&buf->node);
		spin_unlock(&fl->lock);
		fastrpc_buf_free(buf);
	} else {
		dev_err(dev, "unmmap\tpt 0x%09lx ERROR\n", buf->raddr);
	}

	return err;
}

static int fastrpc_req_munmap(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_buf *buf = NULL, *iter, *b;
	struct fastrpc_req_munmap req;
	struct fastrpc_munmap_req_msg req_msg;
	struct fastrpc_map *map = NULL, *iterm, *m;
	struct device *dev = fl->sctx->dev;
	struct fastrpc_invoke_args args[1] = { [0] = { 0 } };
	struct fastrpc_invoke_v2 ioctl = {0};
	int err = 0;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	spin_lock(&fl->lock);
	list_for_each_entry_safe(iter, b, &fl->mmaps, node) {
		if ((iter->raddr == req.vaddrout) && (iter->size == req.size)) {
			buf = iter;
			break;
		}
	}
	spin_unlock(&fl->lock);

	if (buf) {
		err = fastrpc_req_munmap_impl(fl, buf);
		return err;
	}

	spin_lock(&fl->lock);
	list_for_each_entry_safe(iter, b, &fl->cctx->rhmaps, node) {
		if ((iter->raddr == req.vaddrout) && (iter->size == req.size)) {
			buf = iter;
			break;
		}
	}
	spin_unlock(&fl->lock);
	if (buf) {
		err = fastrpc_req_munmap_impl(fl, buf);
		return err;
	}
	spin_lock(&fl->lock);
	list_for_each_entry_safe(iterm, m, &fl->maps, node) {
		if (iterm->raddr == req.vaddrout) {
			map = iterm;
			break;
		}
	}
	spin_unlock(&fl->lock);
	if (!map) {
		dev_err(dev, "map not in list\n");
		return -EINVAL;
	}

	req_msg.pgid = fl->client_id;
	req_msg.size = map->len;
	req_msg.vaddr = map->raddr;

	args[0].ptr = (u64) (uintptr_t) &req_msg;
	args[0].length = sizeof(req_msg);

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MUNMAP, 1, 0);
	ioctl.inv.args = (u64)args;
	err = fastrpc_internal_invoke(fl, true, &ioctl);
	if (err)
		dev_err(dev, "unmmap\tpt fd = %d, 0x%09llx error\n",  map->fd, map->raddr);
	else
		fastrpc_map_put(map);

	return err;
}

static int fastrpc_req_mmap(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_invoke_args args[3] = { [0 ... 2] = { 0 } };
	struct fastrpc_invoke_v2 ioctl = {0};
	struct fastrpc_buf *buf = NULL;
	struct fastrpc_mmap_req_msg req_msg;
	struct fastrpc_mmap_rsp_msg rsp_msg;
	struct fastrpc_phy_page pages;
	struct fastrpc_req_mmap req;
	struct fastrpc_map *map = NULL;
	struct device *dev = fl->sctx->dev;
	int err;
	unsigned long flags;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	if (req.flags == ADSP_MMAP_ADD_PAGES || req.flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
		if (req.flags == ADSP_MMAP_REMOTE_HEAP_ADDR && fl->is_unsigned_pd) {
			dev_err(dev, "secure memory allocation is not supported in unsigned PD\n");
			return -EINVAL;
		}
		if (req.vaddrin && !fl->is_unsigned_pd) {
			dev_err(dev, "adding user allocated pages is not supported\n");
			return -EINVAL;
		}

		if (req.flags == ADSP_MMAP_REMOTE_HEAP_ADDR)
			err = fastrpc_remote_heap_alloc(fl, dev, req.size, &buf);
		else
			err = fastrpc_buf_alloc(fl, dev, req.size, &buf);

		if (err) {
			dev_err(dev, "failed to allocate buffer\n");
			return err;
		}

		req_msg.pgid = fl->client_id;
		req_msg.flags = req.flags;
		req_msg.vaddr = req.vaddrin;
		req_msg.num = sizeof(pages);

		args[0].ptr = (u64) (uintptr_t) &req_msg;
		args[0].length = sizeof(req_msg);

		pages.addr = buf->phys;
		pages.size = buf->size;

		args[1].ptr = (u64) (uintptr_t) &pages;
		args[1].length = sizeof(pages);

		args[2].ptr = (u64) (uintptr_t) &rsp_msg;
		args[2].length = sizeof(rsp_msg);

		ioctl.inv.handle = FASTRPC_INIT_HANDLE;
		ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MMAP, 2, 1);
		ioctl.inv.args = (u64)args;
		err = fastrpc_internal_invoke(fl, true, &ioctl);
		if (err) {
			dev_err(dev, "mmap error (len 0x%08llx)\n", buf->size);
			fastrpc_buf_free(buf);
			return err;
		}

		/* update the buffer to be able to deallocate the memory on the DSP */
		buf->raddr = (uintptr_t) rsp_msg.vaddr;
		buf->flag = req.flags;

		/* let the client know the address to use */
		req.vaddrout = rsp_msg.vaddr;

		/* Add memory to static PD pool, protection thru hypervisor */
		if (req.flags == ADSP_MMAP_REMOTE_HEAP_ADDR && fl->cctx->vmcount) {
			u64 src_perms = BIT(QCOM_SCM_VMID_HLOS);

			err = qcom_scm_assign_mem(buf->phys, (u64)buf->size,
				&src_perms, fl->cctx->vmperms, fl->cctx->vmcount);
			if (err) {
				dev_err(fl->sctx->dev, "Failed to assign memory phys 0x%llx size 0x%llx err %d\n",
						buf->phys, buf->size, err);
				goto err_assign;
			}
		}

		if (req.flags == ADSP_MMAP_REMOTE_HEAP_ADDR) {
			spin_lock_irqsave(&fl->cctx->lock, flags);
			list_add_tail(&buf->node, &fl->cctx->rhmaps);
			spin_unlock_irqrestore(&fl->cctx->lock, flags);
		} else {
			spin_lock(&fl->lock);
			list_add_tail(&buf->node, &fl->mmaps);
			spin_unlock(&fl->lock);
		}

		if (copy_to_user((void __user *)argp, &req, sizeof(req)))
			/*
			 * The usercopy failed, but buf is already mapped
			 * in the DSP and accessible for the current process.
			 * Rely on the process exit path to do required cleanup.
			 */
			return -EFAULT;
	} else {
		err = fastrpc_map_create(fl, req.fd, req.vaddrin, req.size,
				0, &map, true);
		if (err) {
			dev_err(dev, "failed to map buffer, fd = %d\n", req.fd);
			return err;
		}

		req_msg.pgid = fl->client_id;
		req_msg.flags = req.flags;
		req_msg.vaddr = req.vaddrin;
		req_msg.num = sizeof(pages);

		args[0].ptr = (u64) (uintptr_t) &req_msg;
		args[0].length = sizeof(req_msg);

		pages.addr = map->phys;
		pages.size = map->len;

		args[1].ptr = (u64) (uintptr_t) &pages;
		args[1].length = sizeof(pages);

		args[2].ptr = (u64) (uintptr_t) &rsp_msg;
		args[2].length = sizeof(rsp_msg);

		ioctl.inv.handle = FASTRPC_INIT_HANDLE;
		ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MMAP, 2, 1);
		ioctl.inv.args = (u64)args;
		err = fastrpc_internal_invoke(fl, true, &ioctl);
		if (err) {
			dev_err(dev, "mmap error (len 0x%08llx)\n", map->len);
			goto err_invoke;
		}

		/* update the buffer to be able to deallocate the memory on the DSP */
		map->raddr = (uintptr_t) rsp_msg.vaddr;

		/* let the client know the address to use */
		req.vaddrout = rsp_msg.vaddr;

		if (copy_to_user((void __user *)argp, &req, sizeof(req)))
			/*
			 * The usercopy failed, but buf is already mapped
			 * in the DSP and accessible for the current process.
			 * Rely on the process exit path to do required cleanup.
			 */
			return -EFAULT;
	}
	return 0;

err_assign:
	if (req.flags != ADSP_MMAP_ADD_PAGES && req.flags != ADSP_MMAP_REMOTE_HEAP_ADDR)
		fastrpc_map_put(map);
	else
		fastrpc_req_munmap_impl(fl, buf);

err_invoke:
	if (map)
		fastrpc_map_put(map);

	return err;
}

static int fastrpc_req_mem_unmap_impl(struct fastrpc_user *fl, struct fastrpc_mem_unmap *req)
{
	struct fastrpc_invoke_args args[1] = { [0] = { 0 } };
	struct fastrpc_invoke_v2 ioctl = {0};
	struct fastrpc_map *map = NULL, *iter, *m;
	struct fastrpc_mem_unmap_req_msg req_msg = { 0 };
	int err = 0;
	struct device *dev = fl->sctx->dev;

	spin_lock(&fl->lock);
	list_for_each_entry_safe(iter, m, &fl->maps, node) {
		if ((req->fd < 0 || iter->fd == req->fd) && (iter->raddr == req->vaddr)) {
			map = iter;
			break;
		}
	}

	spin_unlock(&fl->lock);

	if (!map) {
		dev_err(dev, "map not in list\n");
		return -EINVAL;
	}

	req_msg.pgid = fl->client_id;
	req_msg.len = map->len;
	req_msg.vaddrin = map->raddr;
	req_msg.fd = map->fd;

	args[0].ptr = (u64) (uintptr_t) &req_msg;
	args[0].length = sizeof(req_msg);

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MEM_UNMAP, 1, 0);
	ioctl.inv.args = (u64)args;
	err = fastrpc_internal_invoke(fl, true, &ioctl);
	if (err) {
		dev_err(dev, "unmmap\tpt fd = %d, 0x%09llx error\n",  map->fd, map->raddr);
		return err;
	}
	fastrpc_map_put(map);

	return 0;
}

static int fastrpc_req_mem_unmap(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_mem_unmap req;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	return fastrpc_req_mem_unmap_impl(fl, &req);
}

static int fastrpc_req_mem_map(struct fastrpc_user *fl, char __user *argp)
{
	struct fastrpc_invoke_args args[4] = { [0 ... 3] = { 0 } };
	struct fastrpc_invoke_v2 ioctl = {0};
	struct fastrpc_mem_map_req_msg req_msg = { 0 };
	struct fastrpc_mmap_rsp_msg rsp_msg = { 0 };
	struct fastrpc_phy_page pages = { 0 };
	struct fastrpc_mem_map req;
	struct device *dev = fl->sctx->dev;
	struct fastrpc_map *map = NULL;
	int err;

	if (copy_from_user(&req, argp, sizeof(req)))
		return -EFAULT;

	/* create SMMU mapping */
	err = fastrpc_map_create(fl, req.fd, req.vaddrin, req.length,
			0, &map, true);
	if (err) {
		dev_err(dev, "failed to map buffer, fd = %d\n", req.fd);
		return err;
	}

	req_msg.pgid = fl->client_id;
	req_msg.fd = req.fd;
	req_msg.offset = req.offset;
	req_msg.vaddrin = req.vaddrin;
	map->va = (void *) (uintptr_t) req.vaddrin;
	req_msg.flags = req.flags;
	req_msg.num = sizeof(pages);
	req_msg.data_len = 0;

	args[0].ptr = (u64) (uintptr_t) &req_msg;
	args[0].length = sizeof(req_msg);

	pages.addr = map->phys;
	pages.size = map->len;

	args[1].ptr = (u64) (uintptr_t) &pages;
	args[1].length = sizeof(pages);

	args[2].ptr = (u64) (uintptr_t) &pages;
	args[2].length = 0;

	args[3].ptr = (u64) (uintptr_t) &rsp_msg;
	args[3].length = sizeof(rsp_msg);

	ioctl.inv.handle = FASTRPC_INIT_HANDLE;
	ioctl.inv.sc = FASTRPC_SCALARS(FASTRPC_RMID_INIT_MEM_MAP, 3, 1);
	ioctl.inv.args = (u64)args;
	err = fastrpc_internal_invoke(fl, true, &ioctl);
	if (err) {
		dev_err(dev, "mem mmap error, fd %d, vaddr %llx, size %lld\n",
			req.fd, req.vaddrin, map->len);
		goto err_invoke;
	}

	/* update the buffer to be able to deallocate the memory on the DSP */
	map->raddr = rsp_msg.vaddr;

	/* let the client know the address to use */
	req.vaddrout = rsp_msg.vaddr;

	if (copy_to_user((void __user *)argp, &req, sizeof(req)))
		/*
		 * The usercopy failed, but map is already mapped
		 * in the DSP and accessible for the current process.
		 * Rely on the process exit path to do required cleanup.
		 */
		return -EFAULT;

	return 0;

err_invoke:
	fastrpc_map_put(map);

	return err;
}

static long fastrpc_device_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	struct fastrpc_user *fl = (struct fastrpc_user *)file->private_data;
	char __user *argp = (char __user *)arg;
	int err;

	fastrpc_channel_ctx_get(fl->cctx);
	switch (cmd) {
	case FASTRPC_IOCTL_INVOKE:
		err = fastrpc_invoke(fl, argp);
		break;
	case FASTRPC_IOCTL_INVOKEV2:
		err = fastrpc_invokev2(fl, argp);
		break;
	case FASTRPC_IOCTL_INIT_ATTACH:
		err = fastrpc_init_attach(fl, ROOT_PD);
		break;
	case FASTRPC_IOCTL_INIT_ATTACH_SNS:
		err = fastrpc_init_attach(fl, SENSORS_PD);
		break;
	case FASTRPC_IOCTL_INIT_CREATE_STATIC:
		err = fastrpc_init_create_static_process(fl, argp);
		break;
	case FASTRPC_IOCTL_INIT_CREATE:
		err = fastrpc_init_create_process(fl, argp);
		break;
	case FASTRPC_IOCTL_ALLOC_DMA_BUFF:
		err = fastrpc_dmabuf_alloc(fl, argp);
		break;
	case FASTRPC_IOCTL_MMAP:
		err = fastrpc_req_mmap(fl, argp);
		break;
	case FASTRPC_IOCTL_MUNMAP:
		mutex_lock(&fl->mutex);
		err = fastrpc_req_munmap(fl, argp);
		mutex_unlock(&fl->mutex);
		break;
	case FASTRPC_IOCTL_MEM_MAP:
		err = fastrpc_req_mem_map(fl, argp);
		break;
	case FASTRPC_IOCTL_MEM_UNMAP:
		err = fastrpc_req_mem_unmap(fl, argp);
		break;
	case FASTRPC_IOCTL_GET_DSP_INFO:
		err = fastrpc_get_dsp_info(fl, argp);
		break;
	default:
		err = -ENOTTY;
		break;
	}
	fastrpc_channel_ctx_put(fl->cctx);

	return err;
}

static void fastrpc_notify_users(struct fastrpc_user *user)
{
	struct fastrpc_invoke_ctx *ctx;

	spin_lock(&user->lock);
	list_for_each_entry(ctx, &user->pending, node) {
		ctx->retval = -EPIPE;
		ctx->is_work_done = true;
		complete(&ctx->work);
	}
	spin_unlock(&user->lock);
}

static void fastrpc_notify_pdr_drivers(struct fastrpc_channel_ctx *cctx,
		char *servloc_name)
{
	struct fastrpc_user *fl;
	unsigned long flags;

	spin_lock_irqsave(&cctx->lock, flags);
	list_for_each_entry(fl, &cctx->users, user) {
		if (fl->servloc_name && !strcmp(servloc_name, fl->servloc_name))
			fastrpc_notify_users(fl);
	}
	spin_unlock_irqrestore(&cctx->lock, flags);
}

static void fastrpc_pdr_cb(int state, char *service_path, void *priv)
{
	struct fastrpc_static_pd *spd = (struct fastrpc_static_pd *)priv;
	unsigned long flags;

	if (!spd)
		return;

	switch (state) {
	case SERVREG_SERVICE_STATE_DOWN:
		dev_info(&spd->cctx->rpdev->dev,
			"%s: %s (%s) is down for PDR on %d\n",
			__func__, spd->spdname,
			spd->servloc_name,
			spd->domain);
		spin_lock_irqsave(&spd->cctx->lock, flags);
		spd->pdrcount++;
		atomic_set(&spd->ispdup, 0);
		spin_unlock_irqrestore(&spd->cctx->lock, flags);
		if (!strcmp(spd->servloc_name,
				AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME))
			spd->cctx->staticpd_status = false;

		fastrpc_notify_pdr_drivers(spd->cctx, spd->servloc_name);
		break;
	case SERVREG_SERVICE_STATE_UP:
		dev_info(&spd->cctx->rpdev->dev,
			"%s: %s (%s) is up for PDR on %d\n",
			__func__, spd->spdname,
			spd->servloc_name,
			spd->domain);
		atomic_set(&spd->ispdup, 1);
		break;
	default:
		break;
	}
}

static const struct file_operations fastrpc_fops = {
	.open = fastrpc_device_open,
	.release = fastrpc_device_release,
	.unlocked_ioctl = fastrpc_device_ioctl,
	.compat_ioctl = fastrpc_device_ioctl,
};

static int fastrpc_cb_probe(struct platform_device *pdev)
{
	struct fastrpc_channel_ctx *cctx;
	struct fastrpc_session_ctx *sess;
	struct device *dev = &pdev->dev;
	int i, sessions = 0;
	unsigned long flags;
	int rc;

	cctx = dev_get_drvdata(dev->parent);
	if (!cctx)
		return -EINVAL;

	of_property_read_u32(dev->of_node, "qcom,nsessions", &sessions);

	spin_lock_irqsave(&cctx->lock, flags);
	if (cctx->sesscount >= FASTRPC_MAX_SESSIONS) {
		dev_err(&pdev->dev, "too many sessions\n");
		spin_unlock_irqrestore(&cctx->lock, flags);
		return -ENOSPC;
	}
	sess = &cctx->session[cctx->sesscount++];
	sess->used = false;
	sess->valid = true;
	sess->dev = dev;
	mutex_init(&sess->map_mutex);
	dev_set_drvdata(dev, sess);

	if (of_property_read_u32(dev->of_node, "reg", &sess->sid))
		dev_info(dev, "FastRPC Session ID not specified in DT\n");

	if (sessions > 0) {
		struct fastrpc_session_ctx *dup_sess;

		for (i = 1; i < sessions; i++) {
			if (cctx->sesscount >= FASTRPC_MAX_SESSIONS)
				break;
			dup_sess = &cctx->session[cctx->sesscount++];
			memcpy(dup_sess, sess, sizeof(*dup_sess));
			mutex_init(&dup_sess->map_mutex);
		}
	}
	spin_unlock_irqrestore(&cctx->lock, flags);
	rc = dma_set_mask(dev, DMA_BIT_MASK(32));
	if (rc) {
		dev_err(dev, "32-bit DMA enable failed\n");
		return rc;
	}

	return 0;
}

static int fastrpc_cb_remove(struct platform_device *pdev)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(pdev->dev.parent);
	struct fastrpc_session_ctx *sess = dev_get_drvdata(&pdev->dev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&cctx->lock, flags);
	for (i = 0; i < FASTRPC_MAX_SESSIONS; i++) {
		if (cctx->session[i].sid == sess->sid) {
			spin_unlock_irqrestore(&cctx->lock, flags);
			mutex_lock(&cctx->session[i].map_mutex);
			cctx->session[i].dev = NULL;
			mutex_unlock(&cctx->session[i].map_mutex);
			spin_lock_irqsave(&cctx->lock, flags);
			cctx->session[i].valid = false;
			cctx->sesscount--;
		}
	}
	spin_unlock_irqrestore(&cctx->lock, flags);

	return 0;
}

static const struct of_device_id fastrpc_match_table[] = {
	{ .compatible = "qcom,fastrpc-compute-cb", },
	{}
};

static struct platform_driver fastrpc_cb_driver = {
	.probe = fastrpc_cb_probe,
	.remove = fastrpc_cb_remove,
	.driver = {
		.name = "qcom,fastrpc-cb",
		.of_match_table = fastrpc_match_table,
		.suppress_bind_attrs = true,
	},
};

static int fastrpc_device_register(struct device *dev, struct fastrpc_channel_ctx *cctx,
				   bool is_secured, const char *domain)
{
	struct fastrpc_device *fdev;
	int err;

	fdev = devm_kzalloc(dev, sizeof(*fdev), GFP_KERNEL);
	if (!fdev)
		return -ENOMEM;

	fdev->secure = is_secured;
	fdev->cctx = cctx;
	fdev->miscdev.minor = MISC_DYNAMIC_MINOR;
	fdev->miscdev.fops = &fastrpc_fops;
	fdev->miscdev.name = devm_kasprintf(dev, GFP_KERNEL, "fastrpc-%s%s",
					    domain, is_secured ? "-secure" : "");
	if (!fdev->miscdev.name)
		return -ENOMEM;

	err = misc_register(&fdev->miscdev);
	if (!err) {
		if (is_secured)
			cctx->secure_fdevice = fdev;
		else
			cctx->fdevice = fdev;
	}

	return err;
}

static int fastrpc_get_domain_id(const char *domain)
{
	if (!strncmp(domain, "adsp", 4))
		return ADSP_DOMAIN_ID;
	else if (!strncmp(domain, "cdsp", 4))
		return CDSP_DOMAIN_ID;
	else if (!strncmp(domain, "mdsp", 4))
		return MDSP_DOMAIN_ID;
	else if (!strncmp(domain, "sdsp", 4))
		return SDSP_DOMAIN_ID;
	else if (!strncmp(domain, "gdsp", 4))
		return GDSP_DOMAIN_ID;

	return -EINVAL;
}

static int fastrpc_setup_service_locator(struct fastrpc_channel_ctx *cctx, char *client_name,
			char *service_name, char *service_path, int domain, int spd_session)
{
	int err = 0;
	struct pdr_handle *handle = NULL;
	struct pdr_service *service = NULL;

	/* Register the service locator's callback function */
	handle = pdr_handle_alloc(fastrpc_pdr_cb, &cctx->spd[spd_session]);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		goto bail;
	}
	cctx->spd[spd_session].domain = domain;
	cctx->spd[spd_session].pdrhandle = handle;
	cctx->spd[spd_session].servloc_name = client_name;
	cctx->spd[spd_session].spdname = service_path;
	cctx->spd[spd_session].cctx = cctx;
	service = pdr_add_lookup(handle, service_name, service_path);
	if (IS_ERR(service)) {
		err = PTR_ERR(service);
		goto bail;
	}
	pr_info("fastrpc: %s: pdr_add_lookup enabled for %s (%s, %s)\n",
		__func__, service_name, client_name, service_path);

bail:
	if (err) {
		pr_warn("fastrpc: %s: failed for %s (%s, %s)with err %d\n",
				__func__, service_name, client_name, service_path, err);
	}
	return err;
}

static int fastrpc_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct device *rdev = &rpdev->dev;
	struct fastrpc_channel_ctx *data;
	int i, err, domain_id = -1, vmcount;
	const char *domain;
	bool secure_dsp;
	unsigned int vmids[FASTRPC_MAX_VMIDS];

	err = of_property_read_string(rdev->of_node, "label", &domain);
	if (err) {
		dev_info(rdev, "FastRPC Domain not specified in DT\n");
		return err;
	}

	domain_id = fastrpc_get_domain_id(domain);

	if (domain_id < 0) {
		dev_info(rdev, "FastRPC Domain %s not supported\n", domain);
		return -EINVAL;
	}

	if (of_reserved_mem_device_init_by_idx(rdev, rdev->of_node, 0))
		dev_info(rdev, "no reserved DMA memory for FASTRPC\n");

	vmcount = of_property_read_variable_u32_array(rdev->of_node,
				"qcom,vmids", &vmids[0], 0, FASTRPC_MAX_VMIDS);
	if (vmcount < 0)
		vmcount = 0;
	else if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (vmcount) {
		data->vmcount = vmcount;
		for (i = 0; i < data->vmcount; i++) {
			data->vmperms[i].vmid = vmids[i];
			data->vmperms[i].perm = QCOM_SCM_PERM_RWX;
		}
	}

	secure_dsp = !(of_property_read_bool(rdev->of_node, "qcom,non-secure-domain"));
	data->secure = secure_dsp;

	switch (domain_id) {
	case ADSP_DOMAIN_ID:
	case MDSP_DOMAIN_ID:
	case SDSP_DOMAIN_ID:
		/* Unsigned PD offloading is only supported on CDSP and GDSP */
		data->unsigned_support = false;
		err = fastrpc_device_register(rdev, data, secure_dsp, domain);
		if (err)
			goto fdev_error;
		break;
	case CDSP_DOMAIN_ID:
	case GDSP_DOMAIN_ID:
		data->unsigned_support = true;
		/* Create both device nodes so that we can allow both Signed and Unsigned PD */
		err = fastrpc_device_register(rdev, data, true, domain);
		if (err)
			goto fdev_error;

		err = fastrpc_device_register(rdev, data, false, domain);
		if (err)
			goto populate_error;
		break;
	default:
		err = -EINVAL;
		goto fdev_error;
	}

	if (domain_id == ADSP_DOMAIN_ID) {
		err = fastrpc_setup_service_locator(data, AUDIO_PDR_SERVICE_LOCATION_CLIENT_NAME,
			AUDIO_PDR_ADSP_SERVICE_NAME, ADSP_AUDIOPD_NAME, domain_id, 0);
		if (err)
			return err;

		err = fastrpc_setup_service_locator(data,
			SENSORS_PDR_ADSP_SERVICE_LOCATION_CLIENT_NAME,
			SENSORS_PDR_ADSP_SERVICE_NAME, ADSP_SENSORPD_NAME, domain_id, 1);
		if (err)
			return err;
	} else if (domain_id == SDSP_DOMAIN_ID) {
		err = fastrpc_setup_service_locator(data,
			SENSORS_PDR_SLPI_SERVICE_LOCATION_CLIENT_NAME,
			SENSORS_PDR_SLPI_SERVICE_NAME, SLPI_SENSORPD_NAME, domain_id, 0);
		if (err)
			return err;
	}

	kref_init(&data->refcount);

	dev_set_drvdata(&rpdev->dev, data);
	rdev->dma_mask = &data->dma_mask;
	dma_set_mask_and_coherent(rdev, DMA_BIT_MASK(32));
	INIT_LIST_HEAD(&data->users);
	INIT_LIST_HEAD(&data->rhmaps);
	spin_lock_init(&data->lock);
	idr_init(&data->ctx_idr);
	data->domain_id = domain_id;

	err = of_platform_populate(rdev->of_node, NULL, NULL, rdev);
	if (err)
		goto populate_error;

	data->rpdev = rpdev;

	return 0;

populate_error:
	if (data->fdevice)
		misc_deregister(&data->fdevice->miscdev);
	if (data->secure_fdevice)
		misc_deregister(&data->secure_fdevice->miscdev);

fdev_error:
	kfree(data);
	return err;
}

static void fastrpc_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(&rpdev->dev);
	struct fastrpc_user *user;
	unsigned long flags;

	/* No invocations past this point */
	spin_lock_irqsave(&cctx->lock, flags);
	cctx->rpdev = NULL;
	cctx->staticpd_status = false;
	list_for_each_entry(user, &cctx->users, user)
		fastrpc_notify_users(user);
	spin_unlock_irqrestore(&cctx->lock, flags);

	if (cctx->domain_id == ADSP_DOMAIN_ID) {
		pdr_handle_release(cctx->spd[0].pdrhandle);
		pdr_handle_release(cctx->spd[1].pdrhandle);
	} else if (cctx->domain_id == SDSP_DOMAIN_ID) {
		pdr_handle_release(cctx->spd[0].pdrhandle);
	}

	if (cctx->fdevice)
		misc_deregister(&cctx->fdevice->miscdev);

	if (cctx->secure_fdevice)
		misc_deregister(&cctx->secure_fdevice->miscdev);

	of_platform_depopulate(&rpdev->dev);
	fastrpc_mmap_remove_ssr(cctx);
	fastrpc_channel_ctx_put(cctx);
}

static int fastrpc_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				  int len, void *priv, u32 addr)
{
	struct fastrpc_channel_ctx *cctx = dev_get_drvdata(&rpdev->dev);
	struct fastrpc_invoke_rsp *rsp = data;
	struct fastrpc_invoke_ctx *ctx;
	unsigned long flags;
	unsigned long ctxid;

	if (len < sizeof(*rsp))
		return -EINVAL;

	ctxid = ((rsp->ctx & FASTRPC_CTXID_MASK) >> 16);

	spin_lock_irqsave(&cctx->lock, flags);
	ctx = idr_find(&cctx->ctx_idr, ctxid);

	if (!ctx) {
		dev_dbg(&rpdev->dev, "No context ID matches response\n");
		spin_unlock_irqrestore(&cctx->lock, flags);
		return 0;
	}

	ctx->retval = rsp->retval;
	ctx->is_work_done = true;
	complete(&ctx->work);
	spin_unlock_irqrestore(&cctx->lock, flags);

	return 0;
}

static const struct of_device_id fastrpc_rpmsg_of_match[] = {
	{ .compatible = "qcom,fastrpc" },
	{ },
};
MODULE_DEVICE_TABLE(of, fastrpc_rpmsg_of_match);

static struct rpmsg_driver fastrpc_driver = {
	.probe = fastrpc_rpmsg_probe,
	.remove = fastrpc_rpmsg_remove,
	.callback = fastrpc_rpmsg_callback,
	.drv = {
		.name = "qcom,fastrpc",
		.of_match_table = fastrpc_rpmsg_of_match,
	},
};

static int fastrpc_init(void)
{
	int ret;

	ret = platform_driver_register(&fastrpc_cb_driver);
	if (ret < 0) {
		pr_err("fastrpc: failed to register cb driver\n");
		return ret;
	}

	ret = register_rpmsg_driver(&fastrpc_driver);
	if (ret < 0) {
		pr_err("fastrpc: failed to register rpmsg driver\n");
		platform_driver_unregister(&fastrpc_cb_driver);
		return ret;
	}

	return 0;
}
module_init(fastrpc_init);

static void fastrpc_exit(void)
{
	platform_driver_unregister(&fastrpc_cb_driver);
	unregister_rpmsg_driver(&fastrpc_driver);
}
module_exit(fastrpc_exit);

MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);
