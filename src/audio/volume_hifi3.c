/*
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Tomasz Lauda <tomasz.lauda@linux.intel.com>
 */

/**
 * \file audio/volume_hifi3.c
 * \brief Volume HiFi3 processing implementation
 * \authors Tomasz Lauda <tomasz.lauda@linux.intel.com>
 */

#include "volume.h"

#if defined(__XCC__) && XCHAL_HAVE_HIFI3

#include <xtensa/tie/xt_hifi3.h>

/** \brief Volume scale ratio. */
#define VOL_SCALE (uint32_t)((double)INT32_MAX / VOL_MAX)

/**
 * \brief Sets buffer to be circular using HiFi3 functions.
 * \param[in,out] buffer Circular buffer.
 */
static void vol_setup_circular(struct comp_buffer *buffer)
{
	AE_SETCBEGIN0(buffer->addr);
	AE_SETCEND0(buffer->end_addr);
}

/**
 * \brief HiFi3 enabled volume processing from 16 bit to 16 bit.
 * \param[in,out] dev Volume base component device.
 * \param[in,out] sink Destination buffer.
 * \param[in,out] source Source buffer.
 * \param[in] frames Number of frames to process.
 */
static void vol_s16_to_s16(struct comp_dev *dev, struct comp_buffer *sink,
			   struct comp_buffer *source, uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	uint32_t vol_scaled[SOF_IPC_MAX_CHANNELS];
	ae_f32x2 volume;
	ae_f32x2 mult;
	ae_f32x2 out_sample;
	ae_f16x4 in_sample = AE_ZERO16();
	size_t channel;
	int i;
	ae_int16 *in = (ae_int16 *)source->r_ptr;
	ae_int16 *out = (ae_int16 *)sink->w_ptr;

	/* Scale to VOL_MAX */
	for (channel = 0; channel < dev->params.channels; channel++)
		vol_scaled[channel] = cd->volume[channel] * VOL_SCALE;

	/* Main processing loop */
	for (i = 0; i < frames; i++) {
		/* Processing per channel */
		for (channel = 0; channel < dev->params.channels; channel++) {
			/* Set source as circular buffer */
			vol_setup_circular(source);

			/* Load the input sample */
			AE_L16_XC(in_sample, in, sizeof(ae_int16));

			/* Get gain coefficients */
			volume = *((ae_f32 *)&vol_scaled[channel]);

			/* Multiply the input sample */
			mult = AE_MULFP32X16X2RS_L(volume, in_sample);

			/* Shift right and round to get 16 in 32 bits */
			out_sample = AE_SRAA32RS(mult, 16);

			/* Set sink as circular buffer */
			vol_setup_circular(sink);

			/* Store the output sample */
			AE_S16_0_XC(AE_MOVF16X4_FROMF32X2(out_sample),
				    out, sizeof(ae_int16));
		}
	}
}

/**
 * \brief HiFi3 enabled volume processing from 16 bit to x bit.
 * \param[in,out] dev Volume base component device.
 * \param[in,out] sink Destination buffer.
 * \param[in,out] source Source buffer.
 * \param[in] frames Number of frames to process.
 */
static void vol_s16_to_sX(struct comp_dev *dev, struct comp_buffer *sink,
			  struct comp_buffer *source, uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	uint32_t vol_scaled[SOF_IPC_MAX_CHANNELS];
	ae_f32x2 volume;
	ae_f32x2 mult;
	ae_f32x2 out_sample;
	ae_f16x4 in_sample = AE_ZERO16();
	size_t channel;
	uint8_t shift_left = 0;
	int i;
	ae_int16 *in = (ae_int16 *)source->r_ptr;
	ae_int32 *out = (ae_int32 *)sink->w_ptr;

	/* Get value of shift left */
	if (cd->sink_format == SOF_IPC_FRAME_S24_4LE)
		shift_left = 8;
	else if (cd->sink_format == SOF_IPC_FRAME_S32_LE)
		shift_left = 16;

	/* Scale to VOL_MAX */
	for (channel = 0; channel < dev->params.channels; channel++)
		vol_scaled[channel] = cd->volume[channel] * VOL_SCALE;

	/* Main processing loop */
	for (i = 0; i < frames; i++) {
		/* Processing per channel */
		for (channel = 0; channel < dev->params.channels; channel++) {
			/* Set source as circular buffer */
			vol_setup_circular(source);

			/* Load the input sample */
			AE_L16_XC(in_sample, in, sizeof(ae_int16));

			/* Get gain coefficients */
			volume = *((ae_f32 *)&vol_scaled[channel]);

			/* Multiply the input sample */
			mult = AE_MULFP32X16X2RS_L(volume, in_sample);

			/* Shift right and round to get 16 in 32 bits */
			out_sample = AE_SRAA32RS(mult, 16);

			/* Shift left to get the right alignment */
			out_sample = AE_SLAA32(out_sample, shift_left);

			/* Set sink as circular buffer */
			vol_setup_circular(sink);

			/* Store the output sample */
			AE_S32_L_XC(out_sample, out, sizeof(ae_int32));
		}
	}
}

/**
 * \brief HiFi3 enabled volume processing from x bit to 16 bit.
 * \param[in,out] dev Volume base component device.
 * \param[in,out] sink Destination buffer.
 * \param[in,out] source Source buffer.
 * \param[in] frames Number of frames to process.
 */
static void vol_sX_to_s16(struct comp_dev *dev, struct comp_buffer *sink,
			  struct comp_buffer *source, uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	uint32_t vol_scaled[SOF_IPC_MAX_CHANNELS];
	ae_f32x2 volume;
	ae_f32x2 mult;
	ae_f32x2 in_sample = AE_ZERO32();
	ae_f16x4 out_sample;
	size_t channel;
	uint8_t shift_left = 0;
	int i;
	ae_int32 *in = (ae_int32 *)source->r_ptr;
	ae_int16 *out = (ae_int16 *)sink->w_ptr;

	/* Get value of shift left */
	if (cd->source_format == SOF_IPC_FRAME_S24_4LE)
		shift_left = 8;

	/* Scale to VOL_MAX */
	for (channel = 0; channel < dev->params.channels; channel++)
		vol_scaled[channel] = cd->volume[channel] * VOL_SCALE;

	/* Main processing loop */
	for (i = 0; i < frames; i++) {
		/* Processing per channel */
		for (channel = 0; channel < dev->params.channels; channel++) {
			/* Set source as circular buffer */
			vol_setup_circular(source);

			/* Load the input sample */
			AE_L32_XC(in_sample, in, sizeof(ae_int32));

			/* Shift left to get the right alignment */
			in_sample = AE_SLAA32(in_sample, shift_left);

			/* Get gain coefficients */
			volume = *((ae_f32 *)&vol_scaled[channel]);

			/* Multiply the input sample */
			mult = AE_MULFP32X2RS(volume, in_sample);

			/* Shift right to get 16 in 32 bits */
			out_sample = AE_MOVF16X4_FROMF32X2
					(AE_SRLA32(mult, 16));

			/* Set sink as circular buffer */
			vol_setup_circular(sink);

			/* Store the output sample */
			AE_S16_0_XC(out_sample, out, sizeof(ae_int16));
		}
	}
}

/**
 * \brief HiFi3 enabled volume processing from 24/32 bit to 24/32 or 32 bit.
 * \param[in,out] dev Volume base component device.
 * \param[in,out] sink Destination buffer.
 * \param[in,out] source Source buffer.
 * \param[in] frames Number of frames to process.
 */
static void vol_s24_to_s24_s32(struct comp_dev *dev, struct comp_buffer *sink,
			       struct comp_buffer *source, uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	uint32_t vol_scaled[SOF_IPC_MAX_CHANNELS];
	ae_f32x2 volume;
	ae_f32x2 in_sample = AE_ZERO32();
	ae_f32x2 out_sample;
	ae_f32x2 mult;
	size_t channel;
	uint8_t shift_left = 0;
	int i;
	ae_int32 *in = (ae_int32 *)source->r_ptr;
	ae_int32 *out = (ae_int32 *)sink->w_ptr;

	/* Get value of shift left */
	if (cd->sink_format == SOF_IPC_FRAME_S32_LE)
		shift_left = 8;

	/* Scale to VOL_MAX */
	for (channel = 0; channel < dev->params.channels; channel++)
		vol_scaled[channel] = cd->volume[channel] * VOL_SCALE;

	/* Main processing loop */
	for (i = 0; i < frames; i++) {
		/* Processing per channel */
		for (channel = 0; channel < dev->params.channels; channel++) {
			/* Set source as circular buffer */
			vol_setup_circular(source);

			/* Load the input sample */
			AE_L32_XC(in_sample, in, sizeof(ae_int32));

			/* Get gain coefficients */
			volume = *((ae_f32 *)&vol_scaled[channel]);

			/* Multiply the input sample */
			mult = AE_MULFP32X2RS(volume, AE_SLAA32(in_sample, 8));

			/* Shift right to get 24 in 32 bits (LSB) */
			out_sample = AE_SRLA32(mult, 8);

			/* Shift left to get the right alignment */
			out_sample = AE_SLAA32(out_sample, shift_left);

			/* Set sink as circular buffer */
			vol_setup_circular(sink);

			/* Store the output sample */
			AE_S32_L_XC(out_sample, out, sizeof(ae_int32));
		}
	}
}

/**
 * \brief HiFi3 enabled volume processing from 32 bit to 24/32 or 32 bit.
 * \param[in,out] dev Volume base component device.
 * \param[in,out] sink Destination buffer.
 * \param[in,out] source Source buffer.
 * \param[in] frames Number of frames to process.
 */
static void vol_s32_to_s24_s32(struct comp_dev *dev, struct comp_buffer *sink,
			       struct comp_buffer *source, uint32_t frames)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	uint32_t vol_scaled[SOF_IPC_MAX_CHANNELS];
	ae_f32x2 volume;
	ae_f32x2 in_sample = AE_ZERO32();
	ae_f32x2 out_sample;
	ae_f32x2 mult;
	size_t channel;
	uint8_t shift_right = 0;
	int i;
	ae_int32 *in = (ae_int32 *)source->r_ptr;
	ae_int32 *out = (ae_int32 *)sink->w_ptr;

	/* Get value of shift right */
	if (cd->sink_format == SOF_IPC_FRAME_S24_4LE)
		shift_right = 8;

	/* Scale to VOL_MAX */
	for (channel = 0; channel < dev->params.channels; channel++)
		vol_scaled[channel] = cd->volume[channel] * VOL_SCALE;

	/* Main processing loop */
	for (i = 0; i < frames; i++) {
		/* Processing per channel */
		for (channel = 0; channel < dev->params.channels; channel++) {
			/* Set source as circular buffer */
			vol_setup_circular(source);

			/* Load the input sample */
			AE_L32_XC(in_sample, in, sizeof(ae_int32));

			/* Get gain coefficients */
			volume = *((ae_f32 *)&vol_scaled[channel]);

			/* Multiply the input sample */
			mult = AE_MULFP32X2RS(volume, in_sample);

			/* Shift right to get the right alignment */
			out_sample = AE_SRLA32(mult, shift_right);

			/* Set sink as circular buffer */
			vol_setup_circular(sink);

			/* Store the output sample */
			AE_S32_L_XC(out_sample, out, sizeof(ae_int32));
		}
	}
}

const struct comp_func_map func_map[] = {
	{SOF_IPC_FRAME_S16_LE, SOF_IPC_FRAME_S16_LE, vol_s16_to_s16},
	{SOF_IPC_FRAME_S16_LE, SOF_IPC_FRAME_S24_4LE, vol_s16_to_sX},
	{SOF_IPC_FRAME_S16_LE, SOF_IPC_FRAME_S32_LE, vol_s16_to_sX},
	{SOF_IPC_FRAME_S24_4LE, SOF_IPC_FRAME_S16_LE, vol_sX_to_s16},
	{SOF_IPC_FRAME_S24_4LE, SOF_IPC_FRAME_S24_4LE, vol_s24_to_s24_s32},
	{SOF_IPC_FRAME_S24_4LE, SOF_IPC_FRAME_S32_LE, vol_s24_to_s24_s32},
	{SOF_IPC_FRAME_S32_LE, SOF_IPC_FRAME_S16_LE, vol_sX_to_s16},
	{SOF_IPC_FRAME_S32_LE, SOF_IPC_FRAME_S24_4LE, vol_s32_to_s24_s32},
	{SOF_IPC_FRAME_S32_LE, SOF_IPC_FRAME_S32_LE, vol_s32_to_s24_s32},
};

const size_t func_count = ARRAY_SIZE(func_map);

#endif