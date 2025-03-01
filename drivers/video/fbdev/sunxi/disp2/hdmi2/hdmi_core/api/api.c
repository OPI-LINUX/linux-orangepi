/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/delay.h>
#include <linux/workqueue.h>
#include "log.h"
#include "general_ops.h"

#include "core/main_controller.h"
#include "core/video.h"
#include "core/audio.h"
#include "core/packets.h"
#include "core/irq.h"

#include "hdcp.h"

#include "core/fc_video.h"
#include "core/fc_audio.h"

#include "edid.h"

#include "access.h"

#ifdef CONFIG_AW_PHY
#include "aw_phy.h"
#else
#include "phy.h"
#endif

#include "scdc.h"

#include "hdmitx_dev.h"
#include "identification.h"
#include "core_api.h"

#include "hdcp22_tx.h"
#include "api.h"

static hdmi_tx_dev_t				*hdmi_api;

static int api_phy_write(u8 addr, u32 data)
{
#ifdef CONFIG_AW_PHY
	aw_phy_write(addr, data);
	return 0;
#else
	return phy_i2c_write(hdmi_api, addr, (u16)data);
#endif
}

static int api_phy_read(u8 addr, u32 *value)
{
#ifdef CONFIG_AW_PHY
	aw_phy_read(addr, value);
	return 0;
#else
	return phy_i2c_read(hdmi_api, addr, (u16 *)value);
#endif
}

#ifdef CONFIG_AW_PHY
static void api_phy_reset(void)
{
	return phy_reset();
}

static int api_phy_config_resume(void)
{
	return phy_config_resume();
}
#endif

#ifndef SUPPORT_ONLY_HDMI14
static int api_scdc_read(u8 address, u8 size, u8 *data)
{
	return scdc_read(hdmi_api, address, size, data);
}

static int api_scdc_write(u8 address, u8 size, u8 *data)
{
	return scdc_write(hdmi_api, address, size, data);
}

static u32 api_get_scramble_state(void)
{
	return scrambling_state(hdmi_api);
}
#endif

static void resistor_calibration(u32 reg, u32 data)
{
	dev_write(hdmi_api, reg * 4, data);
	dev_write(hdmi_api, (reg + 1) * 4, data >> 8);
	dev_write(hdmi_api, (reg + 2) * 4, data >> 16);
	dev_write(hdmi_api, (reg + 3) * 4, data >> 24);
}

static void api_set_hdmi_ctrl(hdmi_tx_dev_t *dev, videoParams_t *video,
						  audioParams_t *audio,
						  hdcpParams_t *hdcp)
{
	video_mode_t hdmi_on = 0;
	struct hdmi_tx_ctrl *tx_ctrl = &dev->snps_hdmi_ctrl;

	hdmi_on = video->mHdmi;
	tx_ctrl->hdmi_on = (hdmi_on == HDMI) ? 1 : 0;
	tx_ctrl->hdcp_on = hdcp->hdcp_on;
	tx_ctrl->audio_on = (hdmi_on == HDMI) ? 1 : 0;
	tx_ctrl->use_hdcp = hdcp->use_hdcp;
	tx_ctrl->use_hdcp22 = hdcp->use_hdcp22;
	/*tx_ctrl->audio_on = 1;*/
	tx_ctrl->pixel_clock = videoParams_GetPixelClock(dev, video);
	tx_ctrl->color_resolution = video->mColorResolution;
	tx_ctrl->pixel_repetition = video->mDtd.mPixelRepetitionInput;
}

static void api_avmute(hdmi_tx_dev_t *dev, int enable)
{
	packets_AvMute(dev, enable);
#ifdef CONFIG_HDMI2_HDCP_SUNXI
	hdcp_av_mute(dev, enable);
#endif
}

u32 api_get_avmute(void)
{
	return packets_get_AvMute(hdmi_api);
}

static int api_audio_configure(audioParams_t *audio, videoParams_t *video)
{
	int success = true;
	struct hdmi_tx_ctrl *tx_ctrl = &hdmi_api->snps_hdmi_ctrl;
	u32 tmds_clk = 0;
	video_mode_t hdmi_on;

	/*api_set_hdmi_ctrl(hdmi_api, video);*/
	hdmi_on = video->mHdmi;
	tx_ctrl->hdmi_on = (hdmi_on == HDMI) ? 1 : 0;
	tx_ctrl->audio_on = (hdmi_on == HDMI) ? 1 : 0;
	tx_ctrl->pixel_clock = videoParams_GetPixelClock(hdmi_api, video);
	tx_ctrl->color_resolution = video->mColorResolution;
	tx_ctrl->pixel_repetition = video->mDtd.mPixelRepetitionInput;

	switch (video->mColorResolution) {
	case COLOR_DEPTH_8:
		tmds_clk = tx_ctrl->pixel_clock;
		break;
	case COLOR_DEPTH_10:
		if (video->mEncodingOut != YCC422)
			tmds_clk = tx_ctrl->pixel_clock * 125 / 100;
		else
			tmds_clk = tx_ctrl->pixel_clock;

		break;
	case COLOR_DEPTH_12:
		if (video->mEncodingOut != YCC422)
			tmds_clk =  tx_ctrl->pixel_clock * 3 / 2;
		else
			tmds_clk =  tx_ctrl->pixel_clock;

		break;
	default:
		break;
	}
	tx_ctrl->tmds_clk = tmds_clk;
	/* Audio - Workaround */
	audio_Initialize(hdmi_api);
	success = audio_Configure(hdmi_api, audio);
	if (success == false)
		pr_err("ERROR:Audio not configured\n");
	mc_audio_sampler_clock_enable(hdmi_api,
				      hdmi_api->snps_hdmi_ctrl.audio_on ? 0 : 1);
	fc_force_audio(hdmi_api, 0);

	return success;
}

static void api_fc_drm_up(fc_drm_pb_t *pb)
{
	fc_drm_up(hdmi_api, pb);
}

static void api_fc_drm_disable(void)
{
	fc_drm_disable(hdmi_api);
}

static void api_set_colorimetry(u8 metry, u8 ex_metry)
{
	fc_set_colorimetry(hdmi_api, metry, ex_metry);
}

static void api_set_qt_range(u8 range)
{
	fc_QuantizationRange(hdmi_api, range);
}

static void set_scaninfo(u8 left)
{
	fc_ScanInfo(hdmi_api, left);
}

static void set_aspect_ratio(u8 left)
{
	fc_set_aspect_ratio(hdmi_api, left);
}

static int api_Standby(void)
{
	phy_standby(hdmi_api);
	mc_clocks_standby(hdmi_api);

	hdmi_api->snps_hdmi_ctrl.hdmi_on = 1;
	hdmi_api->snps_hdmi_ctrl.pixel_clock = 0;
	hdmi_api->snps_hdmi_ctrl.color_resolution = 0;
	hdmi_api->snps_hdmi_ctrl.pixel_repetition = 0;
	hdmi_api->snps_hdmi_ctrl.audio_on = 1;

	return true;
}

static int api_close(void)
{
	phy_standby(hdmi_api);
	mc_disable_all_clocks(hdmi_api);

	hdmi_api->snps_hdmi_ctrl.hdmi_on = 1;
	hdmi_api->snps_hdmi_ctrl.pixel_clock = 0;
	hdmi_api->snps_hdmi_ctrl.color_resolution = 0;
	hdmi_api->snps_hdmi_ctrl.pixel_repetition = 0;
	hdmi_api->snps_hdmi_ctrl.audio_on = 1;

	return true;
}

static void api_hpd_enable(u8 enable)
{
	irq_hpd_sense_enable(hdmi_api, enable);
}

static u8 api_dev_hpd_status(void)
{
	return phy_hot_plug_state(hdmi_api);
}

static int api_dtd_fill(dtd_t *dtd, u32 code, u32 refreshRate)
{
	return dtd_fill(hdmi_api, dtd, code, refreshRate);

}

static int api_edid_parser_cea_ext_reset(sink_edid_t *edidExt)
{
	return edid_parser_CeaExtReset(hdmi_api, edidExt);
}
static int api_edid_read(struct edid *edid)
{
	return edid_read(hdmi_api, edid);
}

int api_edid_extension_read(int block, u8 *edid_ext)
{
	return edid_extension_read(hdmi_api, block, edid_ext);
}

static int api_edid_parser(u8 *buffer, sink_edid_t *edidExt,
							u16 edid_size)
{
	return edid_parser(hdmi_api, buffer, edidExt, edid_size);
}

#ifdef CONFIG_HDMI2_HDCP_SUNXI
int hdcp_configure(hdmi_tx_dev_t *dev, hdcpParams_t *hdcp, videoParams_t *video)
{
	if ((!hdcp) && (!video)) {
		pr_err("ERROR:There is NULL value arguments: hdcp=%lx\n",
						(uintptr_t)hdcp);
		return false;
	}

	dev->snps_hdmi_ctrl.use_hdcp = hdcp->use_hdcp;
	dev->snps_hdmi_ctrl.use_hdcp22 = hdcp->use_hdcp22;

	hdcp_av_mute(dev, true);
	mc_hdcp_clock_enable(dev, 1);/*disable it*/
	if ((hdcp->use_hdcp) && (hdcp->hdcp_on))
		hdcp_configure_new(dev, hdcp, video);
	mc_hdcp_clock_enable(dev, 0);/*enable it*/
	hdcp_av_mute(dev, false);

	return 0;
}
#endif

static int api_Configure(videoParams_t *video,
				audioParams_t *audio, productParams_t *product,
				hdcpParams_t *hdcp, u16 phy_model)
{
	int success = true;
	unsigned int tmds_clk = 0;
	hdmi_tx_dev_t				*dev = hdmi_api;

	LOG_TRACE();

	if (!video || !audio || !product || !hdcp) {
		pr_err("ERROR:There is NULL value arguments: video=%lx; audio=%lx; product=%lx; hdcp=%lx\n",
					(uintptr_t)video, (uintptr_t)audio,
					(uintptr_t)product, (uintptr_t)hdcp);
		return false;
	}

	api_set_hdmi_ctrl(dev, video, audio, hdcp);

	switch (video->mColorResolution) {
	case COLOR_DEPTH_8:
		tmds_clk = dev->snps_hdmi_ctrl.pixel_clock;
		break;
	case COLOR_DEPTH_10:
		if (video->mEncodingOut != YCC422)
			tmds_clk = dev->snps_hdmi_ctrl.pixel_clock * 125 / 100;
		else
			tmds_clk = dev->snps_hdmi_ctrl.pixel_clock;

		break;
	case COLOR_DEPTH_12:
		if (video->mEncodingOut != YCC422)
			tmds_clk = dev->snps_hdmi_ctrl.pixel_clock * 3 / 2;
		else
			tmds_clk = dev->snps_hdmi_ctrl.pixel_clock;

		break;

	default:
		pr_err("unvalid color depth\n");
		break;
	}
	dev->snps_hdmi_ctrl.tmds_clk = tmds_clk;

	if (video->mEncodingIn == YCC420) {
		dev->snps_hdmi_ctrl.pixel_clock = dev->snps_hdmi_ctrl.pixel_clock / 2;
		dev->snps_hdmi_ctrl.tmds_clk /= 2;
	}
	if (video->mEncodingIn == YCC422)
		dev->snps_hdmi_ctrl.color_resolution = 8;

	api_avmute(dev, true);

	phy_standby(dev);

	/* Disable interrupts */
	irq_mute(dev);

	success = video_Configure(dev, video);
	if (success == false)
		pr_err("Could not configure video\n");

	/* Audio - Workaround */
	audio_Initialize(dev);
	success = audio_Configure(dev, audio);
	if (success == false)
		pr_err("ERROR:Audio not configured\n");

	/* Packets */
	success = packets_Configure(dev, video, product);
	if (success == false)
		pr_err("ERROR:Could not configure packets\n");

	mc_enable_all_clocks(dev);
	snps_sleep(10000);

#ifndef SUPPORT_ONLY_HDMI14
	if ((dev->snps_hdmi_ctrl.tmds_clk  > 340000)
		/* && (video->scdc_ability)*/) {
		scrambling(dev, 1);
		if (!video->scdc_ability) {
			pr_info("HDMI20 WARN: This sink do NOT support scdc, can NOT scremble\n");
			pr_info("HDMI20 WARN: Please set this video format to ycbcr420 so that tmds clock is lower than 340MHz\n");
		}

		VIDEO_INF("enable scrambling\n");
	} else if (video->scdc_ability || scrambling_state(hdmi_api)) {
		scrambling(dev, 0);
		VIDEO_INF("disable scrambling\n");
	}
#endif

#ifndef __FPGA_PLAT__
	/*add calibrated resistor configuration for all video resolution*/
	dev_write(dev, 0x40018, 0xc0);
	dev_write(dev, 0x4001c, 0x80);

#ifdef CONFIG_AW_PHY
	success = phy_configure(dev, phy_model, video->mEncodingOut);
#else
	success = phy_configure(dev, phy_model);
#endif
	if (success == false)
		pr_err("ERROR:Could not configure PHY\n");
#endif

	/* Disable blue screen transmission
	after turning on all necessary blocks (e.g. HDCP) */
	fc_force_output(dev, false);
	irq_mask_all(dev);

	snps_sleep(100000);

	/* enable interrupts */
	irq_unmute(dev);
	api_avmute(dev, false);

#ifdef CONFIG_HDMI2_HDCP_SUNXI
	hdcp_init(dev);
	if (hdcp->use_hdcp && hdcp->hdcp_on)
		hdcp_configure_new(dev, hdcp, video);
#endif
	return success;
}



static u32 api_get_audio_n(void)
{
	return _audio_clock_n_get(hdmi_api);
}


static u32 api_get_audio_layout(void)
{
	return fc_packet_layout_get(hdmi_api);

}

static u32 api_get_sample_freq(void)
{
	return audio_iec_sampling_freq_get(hdmi_api);
}


static u32 api_get_audio_sample_size(void)
{
	return audio_iec_word_length_get(hdmi_api);

}


static u32 api_get_audio_channel_count(void)
{
	return fc_channel_count_get(hdmi_api);

}


static u32 api_get_phy_power_state(void)
{
	return phy_power_state(hdmi_api);
}


static u32 api_get_phy_pll_lock_state(void)
{
	return phy_pll_lock_state(hdmi_api);

}

static u32 api_get_phy_rxsense_state(void)
{
	return  phy_rxsense_state(hdmi_api);

}

static u32 api_get_tmds_mode(void)
{
	return fc_video_tmdsMode_get(hdmi_api);

}

static u32 api_get_pixelrepetion(void)
{
	return vp_PixelRepetitionFactor_get(hdmi_api);

}


static u32 api_get_colorimetry(void)
{
	return fc_Colorimetry_get(hdmi_api);

}


static u32 api_get_pixel_format(void)
{
	return fc_RgbYcc_get(hdmi_api);
}


static u32 api_get_video_code(void)
{
	return fc_VideoCode_get(hdmi_api);

}

void api_set_video_code(u8 data)
{
	fc_VideoCode_set(hdmi_api, data);
}

static void api_fc_vsif_get(u8 *data)
{
	fc_vsif_get(hdmi_api, data);
}

static void api_fc_vsif_set(u8 *data)
{
	fc_vsif_set(hdmi_api, data);
}

static void api_get_vsd_payload(u8 *video_format, u32 *code)
{
	fc_get_vsd_vendor_payload(hdmi_api, video_format, code);
}

static u32 api_get_color_depth(void)
{

	return vp_ColorDepth_get(hdmi_api);

}


static void api_avmute_enable(u8 enable)
{
	return api_avmute(hdmi_api, enable);

}

static void api_phy_power_enable(u8 enable)
{
	return phy_power_enable(hdmi_api, enable);

}

static void api_dvimode_enable(u8 enable)
{
	return fc_video_DviOrHdmi(hdmi_api, !enable);

}

static int api_set_vsif_config(void *config, videoParams_t *video,
							   productParams_t *product,
							   struct disp_device_dynamic_config *scfg)
{
	return hdr10p_Configure(hdmi_api, config, video, product, scfg);
}

void hdmitx_api_init(hdmi_tx_dev_t *dev,
			videoParams_t *video,
			audioParams_t *audio,
			hdcpParams_t *hdcp)
{
	struct hdmi_dev_func func;
	struct hdmi_tx_ctrl *tx_ctrl;

	hdmi_api = dev;
	memset(hdmi_api, 0, sizeof(hdmi_tx_dev_t));
	memset(&func, 0, sizeof(struct hdmi_dev_func));

	tx_ctrl = &hdmi_api->snps_hdmi_ctrl;
	tx_ctrl->csc_on = 1;
	tx_ctrl->phy_access = PHY_I2C;
	tx_ctrl->data_enable_polarity = 1;
	tx_ctrl->phy_access = 1;

	mutex_init(&dev->i2c_lock);
#ifdef CONFIG_HDMI2_HDCP_SUNXI
	hdcp_api_init(hdmi_api, hdcp, &func);
#endif
	func.main_config = api_Configure;
	func.audio_config = api_audio_configure;
	/*func.set_audio_on = api_set_audio_on;*/

	func.hpd_enable = api_hpd_enable;
	func.dev_hpd_status = api_dev_hpd_status;
	func.dtd_fill = api_dtd_fill;

	func.edid_parser_cea_ext_reset = api_edid_parser_cea_ext_reset;
	func.edid_read = api_edid_read;
	func.edid_parser = api_edid_parser;
	func.edid_extension_read = api_edid_extension_read;

	func.fc_drm_up = api_fc_drm_up;
	func.fc_drm_disable = api_fc_drm_disable;
	func.set_colorimetry = api_set_colorimetry;
	func.set_qt_range = api_set_qt_range;
	func.set_scaninfo = set_scaninfo;
	func.set_aspect_ratio = set_aspect_ratio;

	func.device_standby = api_Standby;
	func.device_close = api_close;
	func.resistor_calibration = resistor_calibration;

	func.phy_write = api_phy_write;
	func.phy_read = api_phy_read;

#ifndef SUPPORT_ONLY_HDMI14
	func.scdc_write = api_scdc_write;
	func.scdc_read = api_scdc_read;
	func.get_scramble_state       = api_get_scramble_state;
#endif

	func.get_audio_n              = api_get_audio_n;
	func.get_audio_layout         = api_get_audio_layout;
	func.get_audio_sample_freq    = api_get_sample_freq;
	func.get_audio_sample_size    = api_get_audio_sample_size;
	func.get_audio_channel_count  = api_get_audio_channel_count;

	func.get_phy_rxsense_state    = api_get_phy_rxsense_state;
	func.get_phy_pll_lock_state   = api_get_phy_pll_lock_state;
	func.get_phy_power_state      = api_get_phy_power_state;
	func.get_tmds_mode            = api_get_tmds_mode;
	func.get_pixelrepetion        = api_get_pixelrepetion;
	func.get_colorimetry		  = api_get_colorimetry;
	func.get_pixel_format		  = api_get_pixel_format;
	func.get_video_code			  = api_get_video_code;
	func.set_video_code           = api_set_video_code;
	func.get_color_depth          = api_get_color_depth;
	func.get_vsif                 = api_fc_vsif_get;
	func.set_vsif                 = api_fc_vsif_set;
	func.get_vsd_payload          = api_get_vsd_payload;

	func.get_avmute_state         = api_get_avmute;
	func.avmute_enable		      = api_avmute_enable;
	func.phy_power_enable		  = api_phy_power_enable;
	func.dvimode_enable			  = api_dvimode_enable;
	func.set_vsif_config          = api_set_vsif_config;

#ifdef CONFIG_AW_PHY
	func.phy_reset                = api_phy_reset;
	func.phy_config_resume        = api_phy_config_resume;
#endif
	register_func_to_hdmi_core(func);
}

void hdmitx_api_exit(void)
{
#ifdef CONFIG_HDMI2_HDCP_SUNXI
	hdcp_exit();
#endif
	hdmi_api = NULL;
}
