/*
 * Copyright 2010-2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/**
 * @file timer.c
 * @brief Linux implementation of the timer interface.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include "timer_platform.h"
#include <kernel.h>											// c

bool has_timer_expired(Timer *timer)
{
	if (left_ms(timer) > 0)
		return false;
	else
		return true;
}

void countdown_ms(Timer *timer, uint32_t timeout)
{
	timer->timeout = timeout;
	timer->start_timestamp = k_cycle_get_32();					// c
}

uint32_t left_ms(Timer *timer)
{
	uint32_t current_timestamp = k_cycle_get_32();				// c
	uint32_t time_diff_ms = current_timestamp - timer->start_timestamp;
	if (timer->timeout > time_diff_ms)
		return timer->timeout - time_diff_ms;
	else
		return 0;
}

void countdown_sec(Timer *timer, uint32_t timeout)
{
	/* converting timeout in milliseconds */
	timer->timeout = timeout * 1000;
	timer->start_timestamp = k_cycle_get_32();					// c
}

void init_timer(Timer *timer)
{
	timer->timeout = 0;
	timer->start_timestamp = 0;
}

