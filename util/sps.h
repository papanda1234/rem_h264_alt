/**
 * @brief Decode a Sequence Parameter Set (SPS) bitstream
 * @licence 2 clause BSD license.
 * @copyright Copyright (C) 2010 - 2019 Creytiv.com
 */
#ifndef _H264_SPS_H_
#define _H264_SPS_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief H.264 Sequence Parameter Set (SPS)
 * @sa https://www.itu.int/rec/T-REC-H.264
 */
struct h264_sps {
	uint8_t profile_idc;
	uint8_t level_idc;
	uint8_t seq_parameter_set_id;               /* 0-31 */
	uint8_t chroma_format_idc;                  /* 0-3 */

	unsigned log2_max_frame_num;
	unsigned pic_order_cnt_type;

	unsigned max_num_ref_frames;
	unsigned pic_width_in_mbs;
	unsigned pic_height_in_map_units;

	bool frame_cropping_flg;
	unsigned frame_crop_left_offset;            /* pixels */
	unsigned frame_crop_right_offset;           /* pixels */
	unsigned frame_crop_top_offset;             /* pixels */
	unsigned frame_crop_bottom_offset;          /* pixels */

	unsigned width_cropped;			/* pixels */
	unsigned height_cropped;		/* pixels */
	unsigned width;				/* pixels */
	unsigned height;			/* pixels */
};

/**
 * @brief Decode a Sequence Parameter Set (SPS) bitstream
 *
 * @param sps  Decoded H.264 SPS
 * @param buf  SPS bitstream to decode, excluding NAL header
 * @param len  Number of bytes
 *
 * @return 0 if success, otherwise errorcode
 */
int h264_sps_decode(struct h264_sps *sps, const uint8_t *buf, const size_t len);

#endif
