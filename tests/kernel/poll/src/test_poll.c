/*
 * Copyright (c) 2017 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @addtogroup t_poll_api
 * @{
 * @defgroup t_poll_api_basic test_poll_api_basic
 * @brief TestPurpose: verify zephyr poll apis across different context
 * - API coverage
 *   -# k_sem_init K_SEMA_DEFINE
 *   -# k_sem_take k_sema_give k_sema_reset
 *   -# k_sem_count_get
 * @}
 */

#include <ztest.h>
#include <kernel.h>

/* global values and data structures */
struct fifo_msg {
	void *private;
	uint32_t msg;
};

#define SIGNAL_RESULT 0x1ee7d00d
#define FIFO_MSG_VALUE 0xdeadbeef

/* verify k_poll() without waiting */
static struct k_sem no_wait_sem;
static struct k_fifo no_wait_fifo;
static struct k_poll_signal no_wait_signal;

void test_poll_no_wait(void)
{
	struct fifo_msg msg = { NULL, FIFO_MSG_VALUE }, *msg_ptr;

	k_sem_init(&no_wait_sem, 1, 1);
	k_fifo_init(&no_wait_fifo);
	k_poll_signal_init(&no_wait_signal);

	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &no_wait_sem),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &no_wait_fifo),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &no_wait_signal),
	};

	/* test polling events that are already ready */
	k_fifo_put(&no_wait_fifo, &msg);
	k_poll_signal(&no_wait_signal, SIGNAL_RESULT);

	assert_equal(k_poll(events, ARRAY_SIZE(events), 0), 0, "");

	assert_equal(events[0].state, K_POLL_STATE_SEM_AVAILABLE, "");
	assert_equal(k_sem_take(&no_wait_sem, 0), 0, "");

	assert_equal(events[1].state, K_POLL_STATE_FIFO_DATA_AVAILABLE, "");
	msg_ptr = k_fifo_get(&no_wait_fifo, 0);
	assert_not_null(msg_ptr, "");
	assert_equal(msg_ptr, &msg, "");
	assert_equal(msg_ptr->msg, FIFO_MSG_VALUE, "");

	assert_equal(events[2].state, K_POLL_STATE_SIGNALED, "");
	assert_equal(no_wait_signal.signaled, 1, "");
	assert_equal(no_wait_signal.result, SIGNAL_RESULT, "");

	/* verify events are not ready anymore (user has to clear them first) */
	events[0].state = K_POLL_STATE_NOT_READY;
	events[1].state = K_POLL_STATE_NOT_READY;
	events[2].state = K_POLL_STATE_NOT_READY;
	no_wait_signal.signaled = 0;

	assert_equal(k_poll(events, ARRAY_SIZE(events), 0), -EAGAIN, "");
	assert_equal(events[0].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(events[1].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(events[2].state, K_POLL_STATE_NOT_READY, "");

	assert_not_equal(k_sem_take(&no_wait_sem, 0), 0, "");
	assert_is_null(k_fifo_get(&no_wait_fifo, 0), "");
}

/* verify k_poll() that has to wait */

static struct k_sem wait_sem = K_SEM_INITIALIZER(wait_sem, 0, 1);
static struct k_fifo wait_fifo = K_FIFO_INITIALIZER(wait_fifo);
static struct k_poll_signal wait_signal = K_POLL_SIGNAL_INITIALIZER();

struct fifo_msg wait_msg = { NULL, FIFO_MSG_VALUE };

static __stack __noinit char poll_wait_helper_stack[KB(1)];

#define TAG_0 10
#define TAG_1 11
#define TAG_2 12

struct k_poll_event wait_events[] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&wait_sem, TAG_0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_FIFO_DATA_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&wait_fifo, TAG_1),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SIGNAL,
					K_POLL_MODE_NOTIFY_ONLY,
					&wait_signal, TAG_2),
};

static void poll_wait_helper(void *use_fifo, void *p2, void *p3)
{
	(void)p2; (void)p3;

	k_sleep(250);

	k_sem_give(&wait_sem);

	if ((intptr_t)use_fifo) {
		k_fifo_put(&wait_fifo, &wait_msg);
	}

	k_poll_signal(&wait_signal, SIGNAL_RESULT);
}

void test_poll_wait(void)
{
	const int main_low_prio = 10;

	struct fifo_msg *msg_ptr;
	int rc;

	int old_prio = k_thread_priority_get(k_current_get());

	/*
	 * Wait for 3 non-ready events to become ready from a higher priority
	 * thread.
	 */
	k_thread_priority_set(k_current_get(), main_low_prio);

	k_thread_spawn(poll_wait_helper_stack, KB(1), poll_wait_helper,
		       (void *)1, 0, 0, main_low_prio - 1, 0, 0);

	rc = k_poll(wait_events, ARRAY_SIZE(wait_events), K_SECONDS(1));

	k_thread_priority_set(k_current_get(), old_prio);

	assert_equal(rc, 0, "");

	assert_equal(wait_events[0].state, K_POLL_STATE_SEM_AVAILABLE, "");
	assert_equal(k_sem_take(&wait_sem, 0), 0, "");
	assert_equal(wait_events[0].tag, TAG_0, "");

	assert_equal(wait_events[1].state,
		     K_POLL_STATE_FIFO_DATA_AVAILABLE, "");
	msg_ptr = k_fifo_get(&wait_fifo, 0);
	assert_not_null(msg_ptr, "");
	assert_equal(msg_ptr, &wait_msg, "");
	assert_equal(msg_ptr->msg, FIFO_MSG_VALUE, "");
	assert_equal(wait_events[1].tag, TAG_1, "");

	assert_equal(wait_events[2].state, K_POLL_STATE_SIGNALED, "");
	assert_equal(wait_signal.signaled, 1, "");
	assert_equal(wait_signal.result, SIGNAL_RESULT, "");
	assert_equal(wait_events[2].tag, TAG_2, "");

	/* verify events are not ready anymore */
	wait_events[0].state = K_POLL_STATE_NOT_READY;
	wait_events[1].state = K_POLL_STATE_NOT_READY;
	wait_events[2].state = K_POLL_STATE_NOT_READY;
	wait_signal.signaled = 0;

	assert_equal(k_poll(wait_events, ARRAY_SIZE(wait_events),
			    K_SECONDS(1)), -EAGAIN, "");

	assert_equal(wait_events[0].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(wait_events[1].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(wait_events[2].state, K_POLL_STATE_NOT_READY, "");

	/* tags should not have been touched */
	assert_equal(wait_events[0].tag, TAG_0, "");
	assert_equal(wait_events[1].tag, TAG_1, "");
	assert_equal(wait_events[2].tag, TAG_2, "");

	/*
	 * Wait for 2 out of 3 non-ready events to become ready from a higher
	 * priority thread.
	 */
	k_thread_priority_set(k_current_get(), main_low_prio);

	k_thread_spawn(poll_wait_helper_stack, KB(1), poll_wait_helper, 0, 0, 0,
		       main_low_prio - 1, 0, 0);

	rc = k_poll(wait_events, ARRAY_SIZE(wait_events), K_SECONDS(1));

	k_thread_priority_set(k_current_get(), old_prio);

	assert_equal(rc, 0, "");

	assert_equal(wait_events[0].state, K_POLL_STATE_SEM_AVAILABLE, "");
	assert_equal(k_sem_take(&wait_sem, 0), 0, "");
	assert_equal(wait_events[0].tag, TAG_0, "");

	assert_equal(wait_events[1].state, K_POLL_STATE_NOT_READY, "");
	msg_ptr = k_fifo_get(&wait_fifo, K_NO_WAIT);
	assert_is_null(msg_ptr, "");
	assert_equal(wait_events[1].tag, TAG_1, "");

	assert_equal(wait_events[2].state, K_POLL_STATE_SIGNALED, "");
	assert_equal(wait_signal.signaled, 1, "");
	assert_equal(wait_signal.result, SIGNAL_RESULT, "");
	assert_equal(wait_events[2].tag, TAG_2, "");

	/*
	 * Wait for each event to be ready from a lower priority thread, one at
	 * a time.
	 */

	wait_events[0].state = K_POLL_STATE_NOT_READY;
	wait_events[1].state = K_POLL_STATE_NOT_READY;
	wait_events[2].state = K_POLL_STATE_NOT_READY;
	wait_signal.signaled = 0;

	k_thread_spawn(poll_wait_helper_stack, KB(1), poll_wait_helper,
		       (void *)1, 0, 0, old_prio + 1, 0, 0);

	/* semaphore */
	rc = k_poll(wait_events, ARRAY_SIZE(wait_events), K_SECONDS(1));

	assert_equal(rc, 0, "");

	assert_equal(wait_events[0].state, K_POLL_STATE_SEM_AVAILABLE, "");
	assert_equal(k_sem_take(&wait_sem, 0), 0, "");
	assert_equal(wait_events[0].tag, TAG_0, "");

	assert_equal(wait_events[1].state, K_POLL_STATE_NOT_READY, "");
	msg_ptr = k_fifo_get(&wait_fifo, K_NO_WAIT);
	assert_is_null(msg_ptr, "");
	assert_equal(wait_events[1].tag, TAG_1, "");

	assert_equal(wait_events[2].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(wait_events[2].tag, TAG_2, "");

	wait_events[0].state = K_POLL_STATE_NOT_READY;

	/* fifo */
	rc = k_poll(wait_events, ARRAY_SIZE(wait_events), K_SECONDS(1));

	assert_equal(rc, 0, "");

	assert_equal(wait_events[0].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(k_sem_take(&wait_sem, 0), -EBUSY, "");
	assert_equal(wait_events[0].tag, TAG_0, "");

	assert_equal(wait_events[1].state,
		     K_POLL_STATE_FIFO_DATA_AVAILABLE, "");
	msg_ptr = k_fifo_get(&wait_fifo, K_NO_WAIT);
	assert_not_null(msg_ptr, "");
	assert_equal(wait_events[1].tag, TAG_1, "");

	assert_equal(wait_events[2].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(wait_events[2].tag, TAG_2, "");

	wait_events[1].state = K_POLL_STATE_NOT_READY;

	/* poll signal */
	rc = k_poll(wait_events, ARRAY_SIZE(wait_events), K_SECONDS(1));

	assert_equal(rc, 0, "");

	assert_equal(wait_events[0].state, K_POLL_STATE_NOT_READY, "");
	assert_equal(k_sem_take(&wait_sem, 0), -EBUSY, "");
	assert_equal(wait_events[0].tag, TAG_0, "");

	assert_equal(wait_events[1].state, K_POLL_STATE_NOT_READY, "");
	msg_ptr = k_fifo_get(&wait_fifo, K_NO_WAIT);
	assert_is_null(msg_ptr, "");
	assert_equal(wait_events[1].tag, TAG_1, "");

	assert_equal(wait_events[2].state, K_POLL_STATE_SIGNALED, "");
	assert_equal(wait_signal.signaled, 1, "");
	assert_equal(wait_signal.result, SIGNAL_RESULT, "");
	assert_equal(wait_events[2].tag, TAG_2, "");

	wait_events[2].state = K_POLL_STATE_NOT_READY;
	wait_signal.signaled = 0;
}

/* verify -EADDRINUSE return value when object has already a poller */
static struct k_sem eaddrinuse_sem = K_SEM_INITIALIZER(eaddrinuse_sem, 0, 1);
static struct k_sem eaddrinuse_reply =
	K_SEM_INITIALIZER(eaddrinuse_reply, 0, 1);

static __stack __noinit char eaddrinuse_hogger_stack[KB(1)];

static void eaddrinuse_hogger(void *p1, void *p2, void *p3)
{
	(void)p1; (void)p2; (void)p3;

	struct k_poll_event event;

	k_poll_event_init(&event, K_POLL_TYPE_SEM_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY, &eaddrinuse_sem);

	(void)k_poll(&event, 1, K_FOREVER);
	k_sem_take(&eaddrinuse_sem, K_FOREVER);
	k_sem_give(&eaddrinuse_reply);
}

static struct k_sem eaddrinuse_ready_sem =
	K_SEM_INITIALIZER(eaddrinuse_ready_sem, 1, 1);

void test_poll_eaddrinuse(void)
{
	int old_prio = k_thread_priority_get(k_current_get());
	const int main_low_prio = 10;
	int rc;

	struct k_poll_event events[] = {
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &eaddrinuse_sem),
		K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					 K_POLL_MODE_NOTIFY_ONLY,
					 &eaddrinuse_ready_sem),
	};

	k_thread_priority_set(k_current_get(), main_low_prio);

	k_thread_spawn(eaddrinuse_hogger_stack, KB(1), eaddrinuse_hogger,
		       0, 0, 0, main_low_prio - 1, 0, 0);

	rc = k_poll(events, ARRAY_SIZE(events), K_SECONDS(1));

	k_thread_priority_set(k_current_get(), old_prio);

	assert_equal(rc, -EADDRINUSE, "");
	assert_equal(events[0].state, K_POLL_STATE_EADDRINUSE, "");
	assert_equal(events[1].state, K_POLL_STATE_SEM_AVAILABLE, "");

	/* free hogger, ensuring it awoken from k_poll() and got the sem */
	k_sem_give(&eaddrinuse_sem);
	rc = k_sem_take(&eaddrinuse_reply, K_SECONDS(1));

	assert_equal(rc, 0, "");
}
