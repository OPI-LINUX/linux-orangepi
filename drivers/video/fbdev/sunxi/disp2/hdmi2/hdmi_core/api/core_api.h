/*
 * Allwinner SoCs hdmi2.0 driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#ifndef CORE_API_H_
#define CORE_API_H_

#include <linux/dma-mapping.h>
#include <video/sunxi_display2.h>


#define	NO_UPDATED			0
#define	MODE_UPDATED		0x1
#define	FORMAT_UPDATED		0x2
#define	BIT_UPDATED		0x4
#define	EOTF_UPDATED		0x8
#define	CS_UPDATED		0x10
#define	DVI_UPDATED		0x20
#define	RANGE_UPDATED		0x40
#define	SCAN_UPDATED		0x80
#define	RATIO_UPDATED		0x100



typedef enum {
	PHY_ACCESS_UNDEFINED = 0,
	PHY_I2C = 1,
	PHY_JTAG
} phy_access_t;

/***********AUDIO************/
typedef enum {
	INTERFACE_NOT_DEFINED = -1, I2S = 0, SPDIF, HBR, GPA, DMA
} interfaceType_t;

typedef enum {
	PACKET_NOT_DEFINED = -1, AUDIO_SAMPLE = 1, HBR_STREAM
} packet_t;

typedef enum {
	CODING_NOT_DEFINED = -1,
	PCM = 1,
	AC3,
	MPEG1,
	MP3,
	MPEG2,
	AAC,
	DTS,
	ATRAC,
	ONE_BIT_AUDIO,
	DOLBY_DIGITAL_PLUS,
	DTS_HD,
	MAT,
	DST,
	WMAPRO
} codingType_t;

typedef enum {
	DMA_NOT_DEFINED = -1,
	DMA_4_BEAT_INCREMENT = 0,
	DMA_8_BEAT_INCREMENT,
	DMA_16_BEAT_INCREMENT,
	DMA_UNUSED_BEAT_INCREMENT,
	DMA_UNSPECIFIED_INCREMENT
} dmaIncrement_t;

/* Supplementary Audio type, table 8-14 HDMI 2.0 Spec. pg 79 */
typedef enum {
	RESERVED = 0,
	AUDIO_FOR_VIS_IMP_NARR,
	AUDIO_FOR_VIS_IMP_SPOKEN,
	AUDIO_FOR_HEAR_IMPAIRED,
	ADDITIONAL_AUDIO
} suppl_A_Type_t;
/**********************AUDIO*************************/

/***********************VIDEO************************/
/** event_t events to register a callback for in the API
 */
typedef enum {
	MODE_UNDEFINED = -1,
	DVI = 0,
	HDMI
} video_mode_t;

typedef enum {
	COLOR_DEPTH_INVALID = 0,
	COLOR_DEPTH_8 = 8,
	COLOR_DEPTH_10 = 10,
	COLOR_DEPTH_12 = 12,
	COLOR_DEPTH_16 = 16
} color_depth_t;

typedef enum {
	PIXEL_REPETITION_OFF = 0,
	PIXEL_REPETITION_1 = 1,
	PIXEL_REPETITION_2 = 2,
	PIXEL_REPETITION_3 = 3,
	PIXEL_REPETITION_4 = 4,
	PIXEL_REPETITION_5 = 5,
	PIXEL_REPETITION_6 = 6,
	PIXEL_REPETITION_7 = 7,
	PIXEL_REPETITION_8 = 8,
	PIXEL_REPETITION_9 = 9,
	PIXEL_REPETITION_10 = 10
} pixel_repetition_t;

typedef enum {
	HDMI_14 = 1,
	HDMI_20,
	MHL_24,
	MHL_PACKEDPIXEL
} operation_mode_t;

typedef enum {
	ENC_UNDEFINED = -1,
	RGB = 0,
	YCC444,
	YCC422,
	YCC420
} encoding_t;

typedef enum {
	ITU601 = 1,
	ITU709,
	EXTENDED_COLORIMETRY
} colorimetry_t;

typedef enum {
	XV_YCC601 = 0,
	XV_YCC709,
	S_YCC601,
	ADOBE_YCC601,
	ADOBE_RGB,
	BT2020_Yc_Cbc_Crc,
	BT2020_Y_CB_CR
} ext_colorimetry_t;

typedef enum {
	SDR_LUMINANCE_RANGE = 0,
	HDR_LUMINANCE_RANGE,
	SMPTE_ST_2084,
	HLG
} eotf_t;

enum video_format_type {
	VIDEO_CEA_FORMAT = 0,
	VIDEO_HDMI14_4K_FORMAT = 1,
	VIDEO_3D_FORMAT = 2,
};
/***********************VIDEO************************/

/******************EDID*************************/
enum EDID_ERROR {
	CHECKSUM_ERROR = -3,
	HEADER_ERROR = -2,
	READ_ERROR = -1,
	NONE_ERROR = 0,
};
/******************EDID*************************/

/***********HDCP************/
enum hdmi_hdcp_type {
	HDCP_UNDEFINED = -1,
	HDCP14 = 0,
	HDCP22
};
/***********HDCP************/


/***********************EDID**************************/
/**
 * @file
 * For detailed handling of this structure,
 * refer to documentation of the functions
 */
typedef struct {
	/** VIC code */
	u32 mCode;

	/** Identifies modes that ONLY can be displayed in YCC 4:2:0 */
	u8 mLimitedToYcc420;

	/** Identifies modes that can also be displayed in YCC 4:2:0 */
	u8 mYcc420;

	u16 mPixelRepetitionInput;

	/** In units of 1KHz */
	u32 mPixelClock;

	/** 1 for interlaced, 0 progressive */
	u8 mInterlaced;

	u16 mHActive;

	u16 mHBlanking;

	u16 mHBorder;

	u16 mHImageSize; /*For picture aspect ratio*/

	u16 mHSyncOffset;

	u16 mHSyncPulseWidth;

	/** 0 for Active low, 1 active high */
	u8 mHSyncPolarity;

	u16 mVActive;

	u16 mVBlanking;

	u16 mVBorder;

	u16 mVImageSize; /*For picture aspect ratio*/

	u16 mVSyncOffset;

	u16 mVSyncPulseWidth;

	/** 0 for Active low, 1 active high */
	u8 mVSyncPolarity;

} dtd_t;
/***********************EDID**************************/


/***********AUDIO************/
/* Audio Metadata Packet Header, table 8-4 HDMI 2.0 Spec. pg 71 */
typedef struct {
	u8 m3dAudio;
	u8 mNumViews;
	u8 mNumAudioStreams;
} audioMetaDataHeader_t;

/* Audio Metadata Descriptor, table 8-13 HDMI 2.0 Spec. pg 78 */
typedef struct {
	u8 mMultiviewRightLeft;
	u8 mLC_Valid;
	u8 mSuppl_A_Valid;
	u8 mSuppl_A_Mixed;
	suppl_A_Type_t mSuppl_A_Type;
	u8 mLanguage_Code[3];/*ISO 639.2 alpha-3 code, examples: eng,fre,spa,por,jpn,chi */

} audioMetaDataDescriptor_t;

typedef struct {
	audioMetaDataHeader_t mAudioMetaDataHeader;
	audioMetaDataDescriptor_t mAudioMetaDataDescriptor[4];
} audioMetaDataPacket_t;

/**
 * For detailed handling of this structure,
 * refer to documentation of the functions
 */
typedef struct {
	interfaceType_t mInterfaceType;

	codingType_t mCodingType; /** (audioParams_t *params, see InfoFrame) */

	u8 mChannelNum;

	u8 mChannelAllocation; /** channel allocation (audioParams_t *params,
						   see InfoFrame) */

	u8 mSampleSize;	/**  sample size (audioParams_t *params, 16 to 24) */

	u32 mSamplingFrequency;	/** sampling frequency (audioParams_t *params, Hz) */

	u8 mLevelShiftValue; /** level shift value (audioParams_t *params,
						 see InfoFrame) */

	u8 mDownMixInhibitFlag;	/** down-mix inhibit flag (audioParams_t *params,
							see InfoFrame) */

	u8 mIecCopyright; /** IEC copyright */

	u8 mIecCgmsA; /** IEC CGMS-A */

	u8 mIecPcmMode;	/** IEC PCM mode */

	u8 mIecCategoryCode; /** IEC category code */

	u8 mIecSourceNumber; /** IEC source number */

	u8 mIecClockAccuracy; /** IEC clock accuracy */

	packet_t mPacketType; /** packet type. currently only Audio Sample (AUDS)
						  and High Bit Rate (HBR) are supported */

	u16 mClockFsFactor; /** Input audio clock Fs factor used at the audio
						packetizer to calculate the CTS value and ACR packet
						insertion rate */

	dmaIncrement_t mDmaBeatIncrement; /** Incremental burst modes: unspecified
									lengths (upper limit is 1kB boundary) and
									INCR4, INCR8, and INCR16 fixed-beat burst */

	u8 mDmaThreshold; /** When the number of samples in the Audio FIFO is lower
						than the threshold, the DMA engine requests a new burst
						request to the AHB master interface */

	u8 mDmaHlock; /** Master burst lock mechanism */

	u8 mGpaInsertPucv;	/* discard incoming (Parity, Channel status, User bit,
				   Valid and B bit) data and insert data configured in
				   controller instead */
	audioMetaDataPacket_t mAudioMetaDataPacket; /** Audio Multistream variables, to be written to the Audio Metadata Packet */
} audioParams_t;
/***********AUDIO************/


/***********************VIDEO************************/
typedef struct fc_drm_pb {
	u8 eotf;
	u8 metadata;
	u16 r_x;
	u16 r_y;
	u16 g_x;
	u16 g_y;
	u16 b_x;
	u16 b_y;
	u16 w_x;
	u16 w_y;
	u16 luma_max;
	u16 luma_min;
	u16 mcll;
	u16 mfll;
} fc_drm_pb_t;

typedef struct {
	u32 update;
	video_mode_t mHdmi;
	u8 mCea_code;
	u8 mHdmi_code;
	u8 mHdr;
	fc_drm_pb_t *pb;
	fc_drm_pb_t *dynamic_pb;
	encoding_t mEncodingOut;
	encoding_t mEncodingIn;
	u8 mColorResolution; /*color depth*/
	u8 mPixelRepetitionFactor; /*For packetizer pixel repeater*/
	dtd_t mDtd;
	u8 mRgbQuantizationRange;
	u8 mPixelPackingDefaultPhase;
	u8 mColorimetry;
	u8 mScanInfo;
	u8 mActiveFormatAspectRatio;
	u8 mNonUniformScaling;
	ext_colorimetry_t mExtColorimetry;
	u8 mColorimetryDataBlock;
	u8 mItContent;
	u16 mEndTopBar;
	u16 mStartBottomBar;
	u16 mEndLeftBar;
	u16 mStartRightBar;
	u16 mCscFilter;
	u16 mCscA[4];
	u16 mCscC[4];
	u16 mCscB[4];
	u16 mCscScale;
	u8 mHdmiVideoFormat;/*0:There's not 4k*2k or not 3D  1:4k*2k  2:3D */
	u8 m3dStructure; /*packing frame and so on*/
	u8 m3dExtData;/*3d extra structure, if 3d_structure=0x1000,there must be a 3d extra structure*/
	u8 mHdmiVic;
	u8 mHdmi20;/*decided by sink*/
	u8 scdc_ability;
} videoParams_t;
/***********************VIDEO************************/


/*************HDCP*****************/
typedef struct {
	u8 use_hdcp;
	u8 use_hdcp22;
	u8 hdcp_on;
	/** Enable Feature 1.1 */
	int mEnable11Feature;

	/** Check Ri every 128th frame */
	int mRiCheck;

	/** I2C fast mode */
	int mI2cFastMode;

	/** Enhanced link verification */
	int mEnhancedLinkVerification;

	/** Number of supported devices
	 * (depending on instantiated KSV MEM RAM – Revocation Memory to support
	 * HDCP repeaters)
	 */
	u8 maxDevices;

	/** KSV List buffer
	 * Shall be dimensioned to accommodate 5[bytes] x No.
	 * of supported devices
	 * (depending on instantiated KSV MEM RAM – Revocation Memory to support
	 * HDCP repeaters)
	 * plus 8 bytes (64-bit) M0 secret value
	 * plus 2 bytes Bstatus
	 * Plus 20 bytes to calculate the SHA-1 (VH0-VH4)
	 * Total is (30[bytes] + 5[bytes] x Number of supported devices)
	 */
	u8 *mKsvListBuffer;

	/** aksv total of 14 chars**/
	u8 *mAksv;

	/** Keys list
	 * 40 Keys of 14 characters each
	 * stored in segments of 8 bits (2 chars)
	 * **/
	u8 *mKeys;

	u8 *mSwEncKey;

	unsigned long esm_hpi_base;
	dma_addr_t esm_firm_phy_addr;
	unsigned long esm_firm_vir_addr;
	u32 esm_firm_size;
	dma_addr_t esm_data_phy_addr;
	unsigned long esm_data_vir_addr;
	u32 esm_data_size;
} hdcpParams_t;
/**********************HDCP**************************/


/**********************PRODUCT**************************/
/** For detailed handling of this structure,
refer to documentation of the functions */
typedef struct {
	/* Vendor Name of eight 7-bit ASCII characters */
	u8 mVendorName[8];

	u8 mVendorNameLength;

	/* Product name or description,
	consists of sixteen 7-bit ASCII characters */
	u8 mProductName[16];

	u8 mProductNameLength;

	/* Code that classifies the source device (CEA Table 15) */
	u8 mSourceType;

	/* oui 24 bit IEEE Registration Identifier */
	u32 mOUI;

	u8 mVendorPayload[24];

	u8 mVendorPayloadLength;

} productParams_t;
/**********************PRODUCT**************************/

/**********************EDID**************************/
#define MAX_HDMI_VIC		16
#define MAX_HDMI_3DSTRUCT	16
#define MAX_VIC_WITH_3D		16

/**
 * @file
 * Short Video Descriptor.
 * Parse and hold Short Video Descriptors found in Video Data Block in EDID.
 */
/** For detailed handling of this structure,
	refer to documentation of the functions */
typedef struct {
	int	mNative;

	unsigned mCode;

	unsigned mLimitedToYcc420;

	unsigned mYcc420;

} shortVideoDesc_t;

/**
 * @file
 * Short Audio Descriptor.
 * Found in Audio Data Block (shortAudioDesc_t *sad, CEA Data Block Tage Code 1)
 * Parse and hold information from EDID data structure
 */
/** For detailed handling of this structure, refer to documentation
	of the functions */
typedef struct {
	u8 mFormat;

	u8 mMaxChannels;

	u8 mSampleRates;

	u8 mByte3;
} shortAudioDesc_t;

/*For detailed handling of this structure,
	refer to documentation of the functions */
typedef struct {
	u16 mPhysicalAddress; /*physical address for cec */

	int mSupportsAi; /*Support ACP ISRC1 ISRC2 packets*/

	int mDeepColor30;

	int mDeepColor36;

	int mDeepColor48;

	int mDeepColorY444;

	int mDviDual; /*Support DVI dual-link operation*/

	u16 mMaxTmdsClk;

	u16 mVideoLatency;

	u16 mAudioLatency;

	u16 mInterlacedVideoLatency;

	u16 mInterlacedAudioLatency;

	u32 mId;
	/*Sink Support for some particular content types*/
	u8 mContentTypeSupport;

	u8 mImageSize; /*for picture espect ratio*/

	int mHdmiVicCount;

	u8 mHdmiVic[MAX_HDMI_VIC];/*the max vic length in vsdb is MAX_HDMI_VIC*/

	int m3dPresent;
	/* row index is the VIC number */
	int mVideo3dStruct[MAX_VIC_WITH_3D][MAX_HDMI_3DSTRUCT];
	/*row index is the VIC number */
	int mDetail3d[MAX_VIC_WITH_3D][MAX_HDMI_3DSTRUCT];

	int mValid;

} hdmivsdb_t;

/* HDMI 2.0 HF_VSDB */
typedef struct {
	u32 mIeee_Oui;

	u8 mValid;

	u8 mVersion;

	u8 mMaxTmdsCharRate;

	u8 m3D_OSD_Disparity;

	u8 mDualView;

	u8 mIndependentView;

	u8 mLTS_340Mcs_scramble;

	u8 mRR_Capable;

	u8 mSCDC_Present;

	u8 mDC_30bit_420;

	u8 mDC_36bit_420;

	u8 mDC_48bit_420;

} hdmiforumvsdb_t;

/**
 * @file
 * Second Monitor Descriptor
 * Parse and hold Monitor Range Limits information read from EDID
 */
typedef struct {
	u8 mMinVerticalRate;

	u8 mMaxVerticalRate;

	u8 mMinHorizontalRate;

	u8 mMaxHorizontalRate;

	u8 mMaxPixelClock;

	int mValid;
} monitorRangeLimits_t;

/**
 * @file
 * Video Capability Data Block.
 * (videoCapabilityDataBlock_t * vcdbCEA Data Block Tag Code 0).
 * Parse and hold information from EDID data structure.
 * For detailed handling of this structure,
 * refer to documentation of the functions
 */

typedef struct {
	int mQuantizationRangeSelectable;

	u8 mPreferredTimingScanInfo;

	u8 mItScanInfo;

	u8 mCeScanInfo;

	int mValid;
} videoCapabilityDataBlock_t;

/**
 * @file
 * Colorimetry Data Block class.
 * Holds and parses the Colorimetry data-block information.
 */

typedef struct {
	u8 mByte3;

	u8 mByte4;

	int mValid;

} colorimetryDataBlock_t;

struct hdr_static_metadata_data_block {
	u8 et_n;
	u8 sm_n;

	/*Desired Content Max Luminance data*/
	u8 dc_max_lum_data;

	/*Desired Content Max Frame-average Luminance data*/
	u8 dc_max_fa_lum_data;

	/*Desired Content Min Luminance data*/
	u8 dc_min_lum_data;
};


/**
 * @file
 * SpeakerAllocation Data Block.
 * Holds and parse the Speaker Allocation data block information.
 * For detailed handling of this structure,
 * refer to documentation of the functions
 */

typedef struct {
	u8 mByte1;
	int mValid;
} speakerAllocationDataBlock_t;

struct est_timings {
	u8 t1;
	u8 t2;
	u8 mfg_rsvd;
} __packed;


struct std_timing {
	u8 hsize; /* need to multiply by 8 then add 248 */
	u8 vfreq_aspect;
} __packed;

/* If detailed data is pixel timing */
struct detailed_pixel_timing {
	u8 hactive_lo;
	u8 hblank_lo;
	u8 hactive_hblank_hi;
	u8 vactive_lo;
	u8 vblank_lo;
	u8 vactive_vblank_hi;
	u8 hsync_offset_lo;
	u8 hsync_pulse_width_lo;
	u8 vsync_offset_pulse_width_lo;
	u8 hsync_vsync_offset_pulse_width_hi;
	u8 width_mm_lo;
	u8 height_mm_lo;
	u8 width_height_mm_hi;
	u8 hborder;
	u8 vborder;
	u8 misc;
} __packed;

/* If it's not pixel timing, it'll be one of the below */
struct detailed_data_string {
	u8 str[13];
} __packed;

struct detailed_data_monitor_range {
	u8 min_vfreq;
	u8 max_vfreq;
	u8 min_hfreq_khz;
	u8 max_hfreq_khz;
	u8 pixel_clock_mhz; /* need to multiply by 10 */
	u8 flags;
	union {
		struct {
			u8 reserved;
			u8 hfreq_start_khz; /* need to multiply by 2 */
			u8 c; /* need to divide by 2 */
			u16 m;
			u8 k;
			u8 j; /* need to divide by 2 */
		} __packed gtf2;
		struct {
			u8 version;
			u8 data1; /* high 6 bits: extra clock resolution */
			u8 data2; /* plus low 2 of above: max hactive */
			u8 supported_aspects;
			u8 flags; /* preferred aspect and blanking support */
			u8 supported_scalings;
			u8 preferred_refresh;
		} __packed cvt;
	} formula;
} __packed;

struct detailed_data_wpindex {
	u8 white_yx_lo; /* Lower 2 bits each */
	u8 white_x_hi;
	u8 white_y_hi;
	u8 gamma; /* need to divide by 100 then add 1 */
} __packed;

struct cvt_timing {
	u8 code[3];
} __packed;


struct detailed_non_pixel {
	u8 pad1;
	u8 type; /* ff=serial, fe=string, fd=monitor range, fc=monitor name
		    fb=color point data, fa=standard timing data,
		    f9=undefined, f8=mfg. reserved */
	u8 pad2;
	union {
		struct detailed_data_string str;
		struct detailed_data_monitor_range range;
		struct detailed_data_wpindex color;
		struct std_timing timings[6];
		struct cvt_timing cvt[4];
	} data;
} __packed;

struct detailed_timing {
	u16 pixel_clock; /* need to multiply by 10 KHz */
	union {
		struct detailed_pixel_timing pixel_data;
		struct detailed_non_pixel other_data;
	} data;
} __packed;

struct edid {
	u8 header[8];
	/* Vendor & product info */
	u8 mfg_id[2];
	u8 prod_code[2];
	u32 serial; /* FIXME: byte order */
	u8 mfg_week;
	u8 mfg_year;
	/* EDID version */
	u8 version;
	u8 revision;
	/* Display info: */
	u8 input;
	u8 width_cm;
	u8 height_cm;
	u8 gamma;
	u8 features;
	/* Color characteristics */
	u8 red_green_lo;
	u8 black_white_lo;
	u8 red_x;
	u8 red_y;
	u8 green_x;
	u8 green_y;
	u8 blue_x;
	u8 blue_y;
	u8 white_x;
	u8 white_y;
	/* Est. timings and mfg rsvd timings*/
	struct est_timings established_timings;
	/* Standard timings 1-8*/
	struct std_timing standard_timings[8];
	/* Detailing timings 1-4 */
	struct detailed_timing detailed_timings[4];
	/* Number of 128 byte ext. blocks */
	u8 extensions;
	/* Checksum */
	u8 checksum;
} __packed;

typedef struct {
	/* Array to hold all the parsed Detailed Timing Descriptors.*/
	dtd_t edid_mDtd[32];
	unsigned int edid_mDtdIndex;

	/*array to hold all the parsed Short Video Descriptors.*/
	shortVideoDesc_t edid_mSvd[128];

	/* shortVideoDesc_t tmpSvd; */
	unsigned int edid_mSvdIndex;
	/*array to hold all the parsed Short Audio Descriptors.*/
	shortAudioDesc_t edid_mSad[128];
	unsigned int edid_mSadIndex;

	/* A string to hold the Monitor Name parsed from EDID.*/
	char edid_mMonitorName[13];

	int edid_mYcc444Support;
	int edid_mYcc422Support;
	int edid_mYcc420Support;

	int edid_mBasicAudioSupport;
	int edid_mUnderscanSupport;

	/* If Sink is HDMI 2.0*/
	int edid_m20Sink;
	hdmivsdb_t edid_mHdmivsdb;
	hdmiforumvsdb_t edid_mHdmiForumvsdb;
	monitorRangeLimits_t edid_mMonitorRangeLimits;
	videoCapabilityDataBlock_t edid_mVideoCapabilityDataBlock;
	colorimetryDataBlock_t edid_mColorimetryDataBlock;
	struct hdr_static_metadata_data_block edid_hdr_static_metadata_data_block;
	speakerAllocationDataBlock_t edid_mSpeakerAllocationDataBlock;
	int hf_eeodb_block_count; //HF-EEODB

	/*detailed discriptor*/
	struct detailed_timing detailed_timings[2];
} sink_edid_t;


/**********************EDID**************************/

struct hdmi_dev_func {
	int (*main_config)(videoParams_t *video, audioParams_t *audio, productParams_t *product,
						hdcpParams_t *hdcp, u16 phy_model);
	int (*audio_config)(audioParams_t *audio, videoParams_t *video);

	void (*hdcp_close)(void);
	void (*hdcp_configure)(hdcpParams_t *hdcp, videoParams_t *video);
	void (*hdcp_disconfigure)(void);
	u8 (*hdcp_event_handler)(int *param, u32 irq_stat);
	int (*get_hdcp_status)(void);
	u32 (*get_hdcp_avmute)(void);
	int (*get_hdcp_type)(void);
	ssize_t (*hdcp_config_dump)(char *buf);

	void (*hpd_enable)(u8 enable);
	u8 (*dev_hpd_status)(void);

	int (*dtd_fill)(dtd_t *dtd, u32 code, u32 refreshRate);
	int (*edid_parser_cea_ext_reset)(sink_edid_t *edidExt);
	int (*edid_read)(struct edid *edid);
	int (*edid_extension_read)(int block, u8 *edid_ext);
	int (*edid_parser)(u8 *buffer, sink_edid_t *edidExt,
					u16 edid_size);

	void (*fc_drm_up)(fc_drm_pb_t *pb);
	void (*fc_drm_disable)(void);
	void (*set_colorimetry)(u8 metry, u8 ex_metry);
	void (*set_qt_range)(u8 range);
	void (*set_scaninfo)(u8 left);
	void (*set_aspect_ratio)(u8 left);
	int (*device_standby)(void);
	int (*device_close)(void);
	void (*resistor_calibration)(u32 reg, u32 data);

	int (*phy_write)(u8 addr, u32 data);
	int (*phy_read)(u8 addr, u32 *value);
#ifndef SUPPORT_ONLY_HDMI14
	int (*scdc_read)(u8 address, u8 size, u8 *data);
	int (*scdc_write)(u8 address, u8 size, u8 *data);
	u32 (*get_scramble_state)(void);
#endif

	u32 (*get_phy_rxsense_state)(void);
	u32 (*get_phy_pll_lock_state)(void);
	u32 (*get_phy_power_state)(void);
	u32 (*get_tmds_mode)(void);
	u32 (*get_avmute_state)(void);
	u32 (*get_pixelrepetion)(void);
	u32 (*get_colorimetry)(void);
	u32 (*get_pixel_format)(void);
	u32 (*get_video_code)(void);
	void (*set_video_code)(u8 data);
	u32 (*get_color_depth)(void);
	u32 (*get_audio_layout)(void);
	u32 (*get_audio_channel_count)(void);
	u32 (*get_audio_sample_freq)(void);
	u32 (*get_audio_sample_size)(void);
	u32 (*get_audio_n)(void);
	void (*get_vsif)(u8 *data);
	void (*set_vsif) (u8 *data);
	void (*get_vsd_payload)(u8 *video_format, u32 *code);
	void (*avmute_enable)(u8 enable);
	void (*phy_power_enable)(u8 enable);
	void (*dvimode_enable)(u8 enable);
	int (*set_vsif_config) (void *config, videoParams_t *video,
							productParams_t *product,
							struct disp_device_dynamic_config *scfg);
#ifdef CONFIG_AW_PHY
	void (*phy_reset)(void);
	int (*phy_config_resume)(void);
#endif
};

#endif
