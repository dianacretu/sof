// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.
//
// Author: Janusz Jankowski <janusz.jankowski@linux.intel.com>

#include <sof/common.h>
#include <sof/drivers/ssp.h>
#include <sof/lib/clk.h>
#include <sof/lib/notifier.h>

static struct clock_info platform_clocks_info[NUM_CLOCKS];

struct clock_info *clocks = platform_clocks_info;

static int clock_platform_set_cpu_freq(int clock, int freq_idx)
{
	uint32_t enc = cpu_freq_enc[freq_idx];

	/* set CPU frequency request for CCU */
#if CAVS_VERSION == CAVS_VERSION_1_5
	io_reg_update_bits(SHIM_BASE + SHIM_CLKCTL, SHIM_CLKCTL_HDCS, 0);
#endif
	io_reg_update_bits(SHIM_BASE + SHIM_CLKCTL,
			   SHIM_CLKCTL_DPCS_MASK(cpu_get_id()),
			   enc);

	return 0;
}

void platform_clock_init(void)
{
	int i;

	for (i = 0; i < PLATFORM_CORE_COUNT; i++) {
		platform_clocks_info[i] = (struct clock_info) {
			.freqs_num = NUM_CPU_FREQ,
			.freqs = cpu_freq,
			.default_freq_idx = CPU_DEFAULT_IDX,
			.notification_id = NOTIFIER_ID_CPU_FREQ,
			.notification_mask = NOTIFIER_TARGET_CORE_MASK(i),
			.set_freq = clock_platform_set_cpu_freq,
		};
	}

	platform_clocks_info[CLK_SSP] = (struct clock_info) {
		.freqs_num = NUM_SSP_FREQ,
		.freqs = ssp_freq,
		.default_freq_idx = SSP_DEFAULT_IDX,
		.notification_id = NOTIFIER_ID_SSP_FREQ,
		.notification_mask = NOTIFIER_TARGET_CORE_ALL_MASK,
		.set_freq = NULL,
	};
}
