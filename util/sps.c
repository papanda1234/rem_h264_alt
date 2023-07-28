/**
 * @brief Decode a Sequence Parameter Set (SPS) bitstream
 * @licence 2 clause BSD license
 * @copyright Copyright (C) 2010 - 2019 Creytiv.com
 */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include "sps.h"

/**
 * @brief Exponential Golomb Decorder
 */
struct getbit {
	const uint8_t *buf;
	size_t pos;
	size_t end;
};

static void     getbit_init(struct getbit *gb, const uint8_t *buf, size_t size);
static size_t   getbit_get_left(const struct getbit *gb);
static unsigned get_bit(struct getbit *gb);
static int      get_ue_golomb(struct getbit *gb, unsigned *valp);

/**
 * @brief parameters.
 */
enum {
	MAX_SPS_COUNT          = 32,
	MAX_LOG2_MAX_FRAME_NUM = 16,
	MACROBLOCK_SIZE        = 16,
};

/**
 * @brief Max Macroblocks
 */
#define MAX_MACROBLOCKS 1048576u

/**
 * @brief Decode a Scaling List.
 * @param gb a instance of Exponential Golomb Decorder
 * @param scaling_list output a scaling list, must be allocated
 * @param sizeofscalinglist size of a scaling list
 * @param usedefaultscalingmatrix If this flag is true, the corresponding scaling matrix must be used.
 * @return 0 if success, otherwise errorcode
 */
static int scaling_list(struct getbit *gb,
			unsigned *scaling_list, size_t sizeofscalinglist,
			bool *usedefaultscalingmatrix)
{
	unsigned lastscale = 8;
	unsigned nextscale = 8;
	size_t j;
	int err;

	for (j = 0; j < sizeofscalinglist; j++) {

		if (nextscale != 0) {

			unsigned delta_scale;

			err = get_ue_golomb(gb, &delta_scale);
			if (err)
				return err;

			nextscale = (lastscale + delta_scale + 256) % 256;

			*usedefaultscalingmatrix = (j==0 && nextscale==0);
		}

		scaling_list[j] = (nextscale==0) ? lastscale : nextscale;

		lastscale = scaling_list[j];
	}

	return 0;
}


/**
 * @brief Decode a Scaling Matrix.
 * @param gb a instance of Exponential Golomb Decorder
 * @param chroma_format_idc chroma_format_idc
 * @return 0 if success, otherwise errorcode
 */
static int decode_scaling_matrix(struct getbit *gb, unsigned chroma_format_idc)
{
	unsigned scalinglist4x4[16];
	unsigned scalinglist8x8[64];
	bool usedefaultscalingmatrix[12];
	unsigned i;
	int err;

	for (i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); i++) {

		unsigned seq_scaling_list_present_flag;

		if (getbit_get_left(gb) < 1)
			return EBADMSG;

		seq_scaling_list_present_flag = get_bit(gb);

		if (seq_scaling_list_present_flag) {

			if (i < 6) {
				err = scaling_list(gb, scalinglist4x4, 16,
					   &usedefaultscalingmatrix[i]);
			}
			else {
				err = scaling_list(gb, scalinglist8x8, 64,
					   &usedefaultscalingmatrix[i]);
			}

			if (err)
				return err;
		}
	}

	return 0;
}

/**
 * @brief Decode a Sequence Parameter Set (SPS) bitstream
 *
 * @param sps  Decoded H.264 SPS
 * @param buf  SPS bitstream to decode, excluding NAL header
 * @param len  Number of bytes
 *
 * @return 0 if success, otherwise errorcode
 */
int h264_sps_decode(struct h264_sps *sps, const uint8_t *buf, const size_t len)
{
	struct getbit gb;
	
	uint8_t profile_idc;
	unsigned seq_parameter_set_id;
	unsigned frame_mbs_only_flag;
	unsigned chroma_format_idc = 1;
	unsigned mb_w_m1;
	unsigned mb_h_m1;
	int err;

	if (!sps || !buf || len < 3)
		return EINVAL;

	memset(sps, 0, sizeof(*sps));

	profile_idc    = buf[0];
	sps->level_idc = buf[2];

	getbit_init(&gb, buf+3, (len-3)*8);

	err = get_ue_golomb(&gb, &seq_parameter_set_id);
	if (err)
		return err;

	if (seq_parameter_set_id >= MAX_SPS_COUNT) {
		fprintf(stderr, "h264: sps: sps_id %u out of range\n",
			   seq_parameter_set_id);
		return EBADMSG;
	}

	if (profile_idc == 100 ||
	    profile_idc == 110 ||
	    profile_idc == 122 ||
	    profile_idc == 244 ||
	    profile_idc ==  44 ||
	    profile_idc ==  83 ||
	    profile_idc ==  86 ||
	    profile_idc == 118 ||
	    profile_idc == 128 ||
	    profile_idc == 138 ||
	    profile_idc == 144) {

		unsigned seq_scaling_matrix_present_flag;

		err = get_ue_golomb(&gb, &chroma_format_idc);
		if (err)
			return err;

		if (chroma_format_idc > 3U) {
			return EBADMSG;
		}
		else if (chroma_format_idc == 3) {

			if (getbit_get_left(&gb) < 1)
				return EBADMSG;

			/* separate_colour_plane_flag */
			(void)get_bit(&gb);
		}

		/* bit_depth_luma/chroma */
		err  = get_ue_golomb(&gb, NULL);
		err |= get_ue_golomb(&gb, NULL);
		if (err)
			return err;

		if (getbit_get_left(&gb) < 2)
			return EBADMSG;

		/* qpprime_y_zero_transform_bypass_flag */
		get_bit(&gb);

		seq_scaling_matrix_present_flag = get_bit(&gb);
		if (seq_scaling_matrix_present_flag) {

			err = decode_scaling_matrix(&gb, chroma_format_idc);
			if (err)
				return err;
		}
	}

	err = get_ue_golomb(&gb, &sps->log2_max_frame_num);
	if (err)
		return err;

	sps->log2_max_frame_num += 4;

	if (sps->log2_max_frame_num > MAX_LOG2_MAX_FRAME_NUM) {
		fprintf(stderr, "h264: sps: log2_max_frame_num"
			   " out of range: %u\n", sps->log2_max_frame_num);
		return EBADMSG;
	}

	err = get_ue_golomb(&gb, &sps->pic_order_cnt_type);
	if (err)
		return err;

	if (sps->pic_order_cnt_type == 0) {

		/* log2_max_pic_order_cnt_lsb */
		err = get_ue_golomb(&gb, NULL);
		if (err)
			return err;
	}
	else if (sps->pic_order_cnt_type == 2) {
	}
	else {
		fprintf(stderr, "h264: sps: WARNING:"
			   " unknown pic_order_cnt_type (%u)\n",
			   sps->pic_order_cnt_type);
		return ENOTSUP;
	}

	err = get_ue_golomb(&gb, &sps->max_num_ref_frames);
	if (err)
		return err;

	if (getbit_get_left(&gb) < 1)
		return EBADMSG;

	/* gaps_in_frame_num_value_allowed_flag */
	(void)get_bit(&gb);

	err  = get_ue_golomb(&gb, &mb_w_m1);
	err |= get_ue_golomb(&gb, &mb_h_m1);
	if (err)
		return err;

	if (getbit_get_left(&gb) < 1)
		return EBADMSG;
	frame_mbs_only_flag = get_bit(&gb);

	sps->pic_width_in_mbs        = mb_w_m1 + 1;
	sps->pic_height_in_map_units = mb_h_m1 + 1;

	sps->pic_height_in_map_units *= 2 - frame_mbs_only_flag;

	if (sps->pic_width_in_mbs >= MAX_MACROBLOCKS ||
	    sps->pic_height_in_map_units >= MAX_MACROBLOCKS) {
		fprintf(stderr, "h264: sps: width/height overflow\n");
		return EBADMSG;
	}

	if (!frame_mbs_only_flag) {

		if (getbit_get_left(&gb) < 1)
			return EBADMSG;

		/* mb_adaptive_frame_field_flag */
		(void)get_bit(&gb);
	}

	if (getbit_get_left(&gb) < 1)
		return EBADMSG;

	/* direct_8x8_inference_flag */
	(void)get_bit(&gb);

	if (getbit_get_left(&gb) < 1)
		return EBADMSG;

	sps->frame_cropping_flg = get_bit(&gb);

	sps->width = MACROBLOCK_SIZE * sps->pic_width_in_mbs;
	sps->height = MACROBLOCK_SIZE * sps->pic_height_in_map_units;

	if (sps->frame_cropping_flg) {

		unsigned crop_left;
		unsigned crop_right;
		unsigned crop_top;
		unsigned crop_bottom;

		unsigned vsub   = (chroma_format_idc == 1) ? 1 : 0;
		unsigned hsub   = (chroma_format_idc == 1 ||
			      chroma_format_idc == 2) ? 1 : 0;
		unsigned sx = 1u << hsub;
		unsigned sy = (2u - frame_mbs_only_flag) << vsub;

		err  = get_ue_golomb(&gb, &crop_left);
		err |= get_ue_golomb(&gb, &crop_right);
		err |= get_ue_golomb(&gb, &crop_top);
		err |= get_ue_golomb(&gb, &crop_bottom);
		if (err)
			return err;

		if ((crop_left + crop_right ) * sx >= sps->width ||
		    (crop_top  + crop_bottom) * sy >= sps->height) {
			return EBADMSG;
		}

		sps->frame_crop_left_offset   = sx * crop_left;
		sps->frame_crop_right_offset  = sx * crop_right;
		sps->frame_crop_top_offset    = sy * crop_top;
		sps->frame_crop_bottom_offset = sy * crop_bottom;
	}

	/* success */
	sps->profile_idc = profile_idc;
	sps->seq_parameter_set_id = (uint8_t)seq_parameter_set_id;
	sps->chroma_format_idc = chroma_format_idc;
	sps->width_cropped = MACROBLOCK_SIZE * sps->pic_width_in_mbs
		- sps->frame_crop_left_offset
		- sps->frame_crop_right_offset;

	sps->height_cropped = MACROBLOCK_SIZE * sps->pic_height_in_map_units
		- sps->frame_crop_top_offset
		- sps->frame_crop_bottom_offset;

	return 0;
}

static void getbit_init(struct getbit *gb, const uint8_t *buf, size_t size)
{
	if (!gb)
		return;

	gb->buf = buf;
	gb->pos = 0;
	gb->end = size;
}


static size_t getbit_get_left(const struct getbit *gb)
{
	if (!gb)
		return 0;

	if (gb->end > gb->pos)
		return gb->end - gb->pos;
	else
		return 0;
}


static unsigned get_bit(struct getbit *gb)
{
	const uint8_t *p;
	register unsigned tmp;

	if (!gb)
		return 0;

	if (gb->pos >= gb->end) {
		fprintf(stderr, "get_bit: read past end"
			   " (%zu >= %zu)\n", gb->pos, gb->end);
		return 0;
	}

	p = gb->buf;
	tmp = ((*(p + (gb->pos >> 0x3))) >> (0x7 - (gb->pos & 0x7))) & 0x1;

	++gb->pos;

	return tmp;
}


static int get_ue_golomb(struct getbit *gb, unsigned *valp)
{
	unsigned zeros = 0;
	unsigned info;
	int i;

	if (!gb)
		return EINVAL;

	while (1) {

		if (getbit_get_left(gb) < 1)
			return EBADMSG;

		if (0 == get_bit(gb))
			++zeros;
		else
			break;
	}

	info = 1 << zeros;

	for (i = zeros - 1; i >= 0; i--) {

		if (getbit_get_left(gb) < 1)
			return EBADMSG;

		info |= get_bit(gb) << i;
	}

	if (valp)
		*valp = info - 1;

	return 0;
}
