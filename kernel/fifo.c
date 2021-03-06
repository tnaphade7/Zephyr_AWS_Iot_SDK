/*
 * Copyright (c) 2010-2016 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * @brief dynamic-size FIFO queue object.
 */


#include <kernel.h>
#include <kernel_structs.h>
#include <debug/object_tracing_common.h>
#include <toolchain.h>
#include <sections.h>
#include <wait_q.h>
#include <ksched.h>
#include <misc/slist.h>
#include <init.h>

extern struct k_fifo _k_fifo_list_start[];
extern struct k_fifo _k_fifo_list_end[];

struct k_fifo *_trace_list_k_fifo;

#ifdef CONFIG_OBJECT_TRACING

/*
 * Complete initialization of statically defined fifos.
 */
static int init_fifo_module(struct device *dev)
{
	ARG_UNUSED(dev);

	struct k_fifo *fifo;

	for (fifo = _k_fifo_list_start; fifo < _k_fifo_list_end; fifo++) {
		SYS_TRACING_OBJ_INIT(k_fifo, fifo);
	}
	return 0;
}

SYS_INIT(init_fifo_module, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_OBJECTS);

#endif /* CONFIG_OBJECT_TRACING */

void k_fifo_init(struct k_fifo *fifo)
{
	sys_slist_init(&fifo->data_q);
	sys_dlist_init(&fifo->wait_q);

	_INIT_OBJ_POLL_EVENT(fifo);

	SYS_TRACING_OBJ_INIT(k_fifo, fifo);
}

static void prepare_thread_to_run(struct k_thread *thread, void *data)
{
	_abort_thread_timeout(thread);
	_ready_thread(thread);
	_set_thread_return_value_with_data(thread, 0, data);
}

/* returns 1 if a reschedule must take place, 0 otherwise */
static inline int handle_poll_event(struct k_fifo *fifo)
{
#ifdef CONFIG_POLL
	uint32_t state = K_POLL_STATE_FIFO_DATA_AVAILABLE;

	return fifo->poll_event ?
	       _handle_obj_poll_event(&fifo->poll_event, state) : 0;
#else
	return 0;
#endif
}

void k_fifo_put(struct k_fifo *fifo, void *data)
{
	struct k_thread *first_pending_thread;
	unsigned int key;

	key = irq_lock();

	first_pending_thread = _unpend_first_thread(&fifo->wait_q);

	if (first_pending_thread) {
		prepare_thread_to_run(first_pending_thread, data);
		if (!_is_in_isr() && _must_switch_threads()) {
			(void)_Swap(key);
			return;
		}
	} else {
		sys_slist_append(&fifo->data_q, data);
		if (handle_poll_event(fifo)) {
			(void)_Swap(key);
			return;
		}
	}

	irq_unlock(key);
}

void k_fifo_put_list(struct k_fifo *fifo, void *head, void *tail)
{
	__ASSERT(head && tail, "invalid head or tail");

	struct k_thread *first_thread, *thread;
	unsigned int key;

	key = irq_lock();

	first_thread = _peek_first_pending_thread(&fifo->wait_q);
	while (head && ((thread = _unpend_first_thread(&fifo->wait_q)))) {
		prepare_thread_to_run(thread, head);
		head = *(void **)head;
	}

	if (head) {
		sys_slist_append_list(&fifo->data_q, head, tail);
	}

	if (first_thread) {
		if (!_is_in_isr() && _must_switch_threads()) {
			(void)_Swap(key);
			return;
		}
	} else {
		if (handle_poll_event(fifo)) {
			(void)_Swap(key);
			return;
		}
	}

	irq_unlock(key);
}

void k_fifo_put_slist(struct k_fifo *fifo, sys_slist_t *list)
{
	__ASSERT(!sys_slist_is_empty(list), "list must not be empty");

	/*
	 * note: this works as long as:
	 * - the slist implementation keeps the next pointer as the first
	 *   field of the node object type
	 * - list->tail->next = NULL.
	 */
	return k_fifo_put_list(fifo, list->head, list->tail);
}

void *k_fifo_get(struct k_fifo *fifo, int32_t timeout)
{
	unsigned int key;
	void *data;

	key = irq_lock();

	if (likely(!sys_slist_is_empty(&fifo->data_q))) {
		data = sys_slist_get_not_empty(&fifo->data_q);
		irq_unlock(key);
		return data;
	}

	if (timeout == K_NO_WAIT) {
		irq_unlock(key);
		return NULL;
	}

	_pend_current_thread(&fifo->wait_q, timeout);

	return _Swap(key) ? NULL : _current->base.swap_data;
}
