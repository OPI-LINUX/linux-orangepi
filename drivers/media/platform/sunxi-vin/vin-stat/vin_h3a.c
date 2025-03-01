/*
 * linux-5.4/drivers/media/platform/sunxi-vin/vin-stat/vin_h3a.c
 *
 * Copyright (c) 2007-2017 Allwinnertech Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/compat.h>

#include "vin_h3a.h"

#include "../vin-isp/sunxi_isp.h"
#include "../vin-video/vin_video.h"

static struct ispstat_buffer *__isp_stat_buf_find(struct isp_stat *stat, int look_empty)
{
	struct ispstat_buffer *found = NULL;
	int i;

	for (i = 0; i < stat->buf_cnt; i++) {
		struct ispstat_buffer *curr = &stat->buf[i];

		/*
		 * Don't select the buffer which is being copied to
		 * userspace or used by the module.
		 */
		if (curr == stat->locked_buf || curr == stat->active_buf)
			continue;

		/* Don't select uninitialised buffers if it's not required */
		if (!look_empty && curr->empty)
			continue;

		if (curr->empty) {
			found = curr;
			break;
		}

		if (!found ||
		    (s32)curr->frame_number - (s32)found->frame_number < 0)
			found = curr;
	}

	return found;
}

static inline struct ispstat_buffer *isp_stat_buf_find_oldest(struct isp_stat *stat)
{
	return __isp_stat_buf_find(stat, 0);
}

static inline struct ispstat_buffer *isp_stat_buf_find_oldest_or_empty(struct isp_stat *stat)
{
	return __isp_stat_buf_find(stat, 1);
}

static int isp_stat_buf_queue(struct isp_stat *stat)
{
	if (!stat->active_buf)
		return STAT_NO_BUF;

	stat->active_buf->buf_size = stat->buf_size;

	stat->active_buf->frame_number = stat->frame_number;
	stat->active_buf->empty = 0;
	stat->active_buf = NULL;

	return STAT_BUF_DONE;
}

/* Get next free buffer to write the statistics to and mark it active. */
static void isp_stat_buf_next(struct isp_stat *stat)
{
	if (unlikely(stat->active_buf)) {
		/* Overwriting unused active buffer */
		vin_log(VIN_LOG_STAT, "%s: new buffer requested without queuing active one.\n",	stat->sd.name);
	} else {
		stat->active_buf = isp_stat_buf_find_oldest_or_empty(stat);
	}
}

static void isp_stat_buf_release(struct isp_stat *stat)
{
	unsigned long flags;

	spin_lock_irqsave(&stat->isp->slock, flags);
	stat->locked_buf = NULL;
	spin_unlock_irqrestore(&stat->isp->slock, flags);
}

/* Get buffer to userspace. */
static struct ispstat_buffer *isp_stat_buf_get(struct isp_stat *stat, struct vin_isp_stat_data *data)
{
	int rval = 0;
	unsigned long flags;
	struct ispstat_buffer *buf;

	spin_lock_irqsave(&stat->isp->slock, flags);

	while (1) {
		buf = isp_stat_buf_find_oldest(stat);
		if (!buf) {
			spin_unlock_irqrestore(&stat->isp->slock, flags);
			vin_log(VIN_LOG_STAT, "%s: cannot find a buffer.\n", stat->sd.name);
			return ERR_PTR(-EBUSY);
		}
		break;
	}

	stat->locked_buf = buf;

	spin_unlock_irqrestore(&stat->isp->slock, flags);
	if (NULL != data) {
		if (buf->buf_size > data->buf_size) {
			vin_warn("%s: userspace's buffer size is not enough.\n", stat->sd.name);
			isp_stat_buf_release(stat);
			return ERR_PTR(-EINVAL);
		}
		rval = copy_to_user(data->buf, buf->virt_addr, buf->buf_size);
		if (rval) {
			vin_warn("%s: failed copying %d bytes of stat data\n", stat->sd.name, rval);
			buf = ERR_PTR(-EFAULT);
			isp_stat_buf_release(stat);
		}
	}
	return buf;
}

static void isp_stat_bufs_free(struct isp_stat *stat)
{
	int i;

	for (i = 1; i < stat->buf_cnt; i++) {
		struct ispstat_buffer *buf = &stat->buf[i];
		struct vin_mm *mm = &stat->ion_man[i];
		mm->size = stat->buf_size;

		if (!buf->virt_addr)
			continue;

		mm->vir_addr = buf->virt_addr;
		mm->dma_addr = buf->dma_addr;
		os_mem_free(&stat->isp->pdev->dev, mm);

		buf->dma_addr = NULL;
		buf->virt_addr = NULL;
		buf->empty = 1;
	}

	vin_log(VIN_LOG_STAT, "%s: all buffers were freed.\n", stat->sd.name);

	stat->buf_size = 0;
	stat->active_buf = NULL;
}

static int isp_stat_bufs_alloc(struct isp_stat *stat, u32 size, u32 count)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&stat->isp->slock, flags);

	BUG_ON(stat->locked_buf != NULL);

	for (i = 1; i < stat->buf_cnt; i++)
		stat->buf[i].empty = 1;

	/* Are the old buffers big enough? */
	if ((stat->buf_size >= size) && (stat->buf_cnt == count)) {
		spin_unlock_irqrestore(&stat->isp->slock, flags);
		vin_log(VIN_LOG_STAT, "%s: old stat buffers are enough.\n", stat->sd.name);
		return 0;
	}

	spin_unlock_irqrestore(&stat->isp->slock, flags);

	isp_stat_bufs_free(stat);

	stat->buf_size = size;
	stat->buf_cnt = count;

	for (i = 1; i < stat->buf_cnt; i++) {
		struct ispstat_buffer *buf = &stat->buf[i];
		struct vin_mm *mm = &stat->ion_man[i];
		mm->size = size;
		if (!os_mem_alloc(&stat->isp->pdev->dev, mm)) {
			buf->virt_addr = mm->vir_addr;
			buf->dma_addr = mm->dma_addr;
		}
		if (!buf->virt_addr || !buf->dma_addr) {
			vin_err("%s: Can't acquire memory for DMA buffer %d\n",	stat->sd.name, i);
			isp_stat_bufs_free(stat);
			return -ENOMEM;
		}
		buf->empty = 1;
	}
	return 0;
}

static void isp_stat_queue_event(struct isp_stat *stat, int err)
{
	struct video_device *vdev = stat->sd.devnode;
	struct v4l2_event event;
	struct vin_isp_stat_event_status *status = (void *)event.u.data;

	memset(&event, 0, sizeof(event));
	if (!err)
		status->frame_number = stat->frame_number;
	else
		status->buf_err = 1;

	event.type = stat->event_type;
	v4l2_event_queue(vdev, &event);
}

int isp_stat_request_statistics(struct isp_stat *stat,
				     struct vin_isp_stat_data *data)
{
	struct ispstat_buffer *buf;

	if (stat->state != ISPSTAT_ENABLED) {
		vin_log(VIN_LOG_STAT, "%s: engine not enabled.\n", stat->sd.name);
		return -EINVAL;
	}
	vin_log(VIN_LOG_STAT, "user wants to request statistics.\n");

	mutex_lock(&stat->ioctl_lock);
	buf = isp_stat_buf_get(stat, data);
	if (IS_ERR(buf)) {
		mutex_unlock(&stat->ioctl_lock);
		return PTR_ERR(buf);
	}

	data->frame_number = buf->frame_number;
	data->buf_size = buf->buf_size;

	buf->empty = 1;
	isp_stat_buf_release(stat);
	mutex_unlock(&stat->ioctl_lock);

	return 0;
}

int isp_stat_config(struct isp_stat *stat, void *new_conf)
{
	int ret;
	u32 count;
	struct vin_isp_h3a_config *user_cfg = new_conf;

	if (!new_conf) {
		vin_log(VIN_LOG_STAT, "%s: configuration is NULL\n", stat->sd.name);
		return -EINVAL;
	}

	mutex_lock(&stat->ioctl_lock);

	user_cfg->buf_size = ISP_STAT_TOTAL_SIZE;

	if (stat->sensor_fps <= 30)
		count = 2;
	else if (stat->sensor_fps <= 60)
		count = 3;
	else if (stat->sensor_fps <= 120)
		count = 4;
	else
		count = 5;

	ret = isp_stat_bufs_alloc(stat, user_cfg->buf_size, count);
	if (ret) {
		mutex_unlock(&stat->ioctl_lock);
		return ret;
	}

	/* Module has a valid configuration. */
	stat->configured = 1;

	mutex_unlock(&stat->ioctl_lock);

	return 0;
}

static int isp_stat_buf_process(struct isp_stat *stat, int buf_state)
{
	int ret = STAT_NO_BUF;
	dma_addr_t dma_addr;

	if (buf_state == STAT_BUF_DONE && stat->state == ISPSTAT_ENABLED) {
		ret = isp_stat_buf_queue(stat);
		isp_stat_buf_next(stat);

		if (!stat->active_buf)
			return STAT_NO_BUF;

		dma_addr = (dma_addr_t)(stat->active_buf->dma_addr);
		bsp_isp_set_statistics_addr(stat->isp->id, dma_addr);
	}

	return ret;
}

int isp_stat_enable(struct isp_stat *stat, u8 enable)
{
	unsigned long irqflags;

	vin_log(VIN_LOG_STAT, "%s: user wants to %s module.\n", stat->sd.name, enable ? "enable" : "disable");

	/* Prevent enabling while configuring */
	mutex_lock(&stat->ioctl_lock);

	spin_lock_irqsave(&stat->isp->slock, irqflags);

	if (!stat->configured && enable) {
		spin_unlock_irqrestore(&stat->isp->slock, irqflags);
		mutex_unlock(&stat->ioctl_lock);
		vin_log(VIN_LOG_STAT, "%s: cannot enable module as it's never been successfully configured so far.\n", stat->sd.name);
		return -EINVAL;
	}
	stat->stat_en_flag = enable;
	stat->isp->f1_after_librun = 0;

	if (enable)
		stat->state = ISPSTAT_ENABLED;
	else
		stat->state = ISPSTAT_DISABLED;

	isp_stat_buf_next(stat);

	spin_unlock_irqrestore(&stat->isp->slock, irqflags);
	mutex_unlock(&stat->ioctl_lock);

	return 0;
}

void isp_stat_isr(struct isp_stat *stat)
{
	int ret = STAT_BUF_DONE;
	unsigned long irqflags;

	vin_log(VIN_LOG_STAT, "buf state is %d, frame number is %d 0x%x %d\n",
		stat->state, stat->frame_number, stat->buf_size, stat->configured);

	spin_lock_irqsave(&stat->isp->slock, irqflags);
	if (stat->state == ISPSTAT_DISABLED) {
		spin_unlock_irqrestore(&stat->isp->slock, irqflags);
		return;
	}
	stat->isp->h3a_stat.frame_number++;

	ret = isp_stat_buf_process(stat, ret);

	spin_unlock_irqrestore(&stat->isp->slock, irqflags);

	isp_stat_queue_event(stat, ret != STAT_BUF_DONE);
}

static long h3a_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct isp_stat *stat = v4l2_get_subdevdata(sd);
	vin_log(VIN_LOG_STAT, "%s cmd is 0x%x\n", __func__, cmd);
	switch (cmd) {
	case VIDIOC_VIN_ISP_H3A_CFG:
		return isp_stat_config(stat, arg);
	case VIDIOC_VIN_ISP_STAT_REQ:
		return isp_stat_request_statistics(stat, arg);
	case VIDIOC_VIN_ISP_STAT_EN:
		return isp_stat_enable(stat, *(u8 *)arg);
	}

	return -ENOIOCTLCMD;
}

#ifdef CONFIG_COMPAT
struct vin_isp_stat_data32 {
	compat_caddr_t buf;
	__u32 buf_size;
	__u32 frame_number;
	__u32 config_counter;
};
struct vin_isp_h3a_config32 {
	__u32 buf_size;
	__u32 config_counter;
};

static int get_isp_statistics_buf32(struct vin_isp_stat_data *kp,
			      struct vin_isp_stat_data32 __user *up)
{
	u32 tmp;

	if (!access_ok(up, sizeof(struct vin_isp_stat_data32)) ||
	    get_user(kp->buf_size, &up->buf_size) ||
	    get_user(kp->frame_number, &up->frame_number) ||
	    get_user(kp->config_counter, &up->config_counter) ||
	    get_user(tmp, &up->buf))
		return -EFAULT;
	kp->buf = compat_ptr(tmp);
	return 0;
}

static int put_isp_statistics_buf32(struct vin_isp_stat_data *kp,
			      struct vin_isp_stat_data32 __user *up)
{
	u32 tmp = (u32) ((unsigned long)kp->buf);

	if (!access_ok(up, sizeof(struct vin_isp_stat_data32)) ||
	    put_user(kp->buf_size, &up->buf_size) ||
	    put_user(kp->frame_number, &up->frame_number) ||
	    put_user(kp->config_counter, &up->config_counter) ||
	    put_user(tmp, &up->buf))
		return -EFAULT;
	return 0;
}

static int get_isp_statistics_config32(struct vin_isp_h3a_config *kp,
			      struct vin_isp_h3a_config32 __user *up)
{
	if (!access_ok(up, sizeof(struct vin_isp_h3a_config32)) ||
	    get_user(kp->buf_size, &up->buf_size) ||
	    get_user(kp->config_counter, &up->config_counter))
		return -EFAULT;
	return 0;
}

static int put_isp_statistics_config32(struct vin_isp_h3a_config *kp,
			      struct vin_isp_h3a_config32 __user *up)
{
	if (!access_ok(up, sizeof(struct vin_isp_h3a_config32)) ||
	    put_user(kp->buf_size, &up->buf_size) ||
	    put_user(kp->config_counter, &up->config_counter))
		return -EFAULT;
	return 0;
}

static int get_isp_statistics_enable32(unsigned int *kp,
			      unsigned int __user *up)
{
	if (!access_ok(up, sizeof(struct vin_isp_h3a_config32)) ||
	    get_user(*kp, up))
		return -EFAULT;
	return 0;
}

static int put_isp_statistics_enable32(unsigned int *kp,
			      unsigned int __user *up)
{
	if (!access_ok(up, sizeof(struct vin_isp_h3a_config32)) ||
	    put_user(*kp, up))
		return -EFAULT;
	return 0;
}

#define VIDIOC_VIN_ISP_H3A_CFG32 _IOWR('V', BASE_VIDIOC_PRIVATE + 31, struct vin_isp_h3a_config32)
#define VIDIOC_VIN_ISP_STAT_REQ32 _IOWR('V', BASE_VIDIOC_PRIVATE + 32, struct vin_isp_stat_data32)
#define VIDIOC_VIN_ISP_STAT_EN32 _IOWR('V', BASE_VIDIOC_PRIVATE + 33, unsigned int)

static long h3a_compat_ioctl32(struct v4l2_subdev *sd,
		unsigned int cmd, unsigned long arg)
{
	union {
		struct vin_isp_h3a_config isb;
		struct vin_isp_stat_data isd;
		unsigned int isu;
	} karg;
	void __user *up = compat_ptr(arg);
	int compatible_arg = 1;
	long err = 0;

	vin_log(VIN_LOG_STAT, "%s cmd is 0x%x\n", __func__, cmd);

	switch (cmd) {
	case VIDIOC_VIN_ISP_STAT_REQ32:
		cmd = VIDIOC_VIN_ISP_STAT_REQ;
		break;
	case VIDIOC_VIN_ISP_H3A_CFG32:
		cmd = VIDIOC_VIN_ISP_H3A_CFG;
		break;
	case VIDIOC_VIN_ISP_STAT_EN32:
		cmd = VIDIOC_VIN_ISP_STAT_EN;
		break;
	}

	switch (cmd) {
	case VIDIOC_VIN_ISP_STAT_REQ:
		err = get_isp_statistics_buf32(&karg.isd, up);
		compatible_arg = 0;
		break;
	case VIDIOC_VIN_ISP_H3A_CFG:
		err = get_isp_statistics_config32(&karg.isb, up);
		compatible_arg = 0;
		break;
	case VIDIOC_VIN_ISP_STAT_EN:
		err = get_isp_statistics_enable32(&karg.isu, up);
		compatible_arg = 0;
		break;
	}

	if (err)
		return err;

	if (compatible_arg)
		err = h3a_ioctl(sd, cmd, up);
	else {
		err = h3a_ioctl(sd, cmd, &karg);
	}

	switch (cmd) {
	case VIDIOC_VIN_ISP_STAT_REQ:
		err = put_isp_statistics_buf32(&karg.isd, up);
		break;
	case VIDIOC_VIN_ISP_H3A_CFG:
		err = put_isp_statistics_config32(&karg.isb, up);
		break;
	case VIDIOC_VIN_ISP_STAT_EN:
		err = put_isp_statistics_enable32(&karg.isu, up);
		compatible_arg = 0;
		break;
	}

	return err;
}
#endif

int isp_stat_subscribe_event(struct v4l2_subdev *subdev,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	struct isp_stat *stat = v4l2_get_subdevdata(subdev);

	if (sub->type != stat->event_type)
		return -EINVAL;

	return v4l2_event_subscribe(fh, sub, STAT_NEVENTS, NULL);
}

static const struct v4l2_subdev_core_ops h3a_subdev_core_ops = {
	.ioctl = h3a_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = h3a_compat_ioctl32,
#endif
	.subscribe_event = isp_stat_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops h3a_subdev_ops = {
	.core = &h3a_subdev_core_ops,
};

int vin_isp_h3a_init(struct isp_dev *isp)
{
	struct isp_stat *stat = &isp->h3a_stat;

	vin_log(VIN_LOG_STAT, "%s\n", __func__);

	stat->isp = isp;
	stat->event_type = V4L2_EVENT_VIN_H3A;

	mutex_init(&stat->ioctl_lock);

	v4l2_subdev_init(&stat->sd, &h3a_subdev_ops);
	snprintf(stat->sd.name, V4L2_SUBDEV_NAME_SIZE, "sunxi_h3a.%u", isp->id);
	stat->sd.grp_id = VIN_GRP_ID_STAT;
	stat->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;
	v4l2_set_subdevdata(&stat->sd, stat);

	stat->pad.flags = MEDIA_PAD_FL_SINK;
	stat->sd.entity.function = MEDIA_ENT_F_PROC_VIDEO_STATISTICS;

	return media_entity_pads_init(&stat->sd.entity, 1, &stat->pad);
}

void vin_isp_h3a_cleanup(struct isp_dev *isp)
{
	struct isp_stat *stat = &isp->h3a_stat;

	vin_log(VIN_LOG_STAT, "%s\n", __func__);

	media_entity_cleanup(&stat->sd.entity);
	mutex_destroy(&stat->ioctl_lock);
	isp_stat_bufs_free(stat);
}
