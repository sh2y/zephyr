/*
 * Copyright (c) 2010-2016 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief fixed-size stack object
 */

#include <kernel.h>
#include <kernel_structs.h>
#include <debug/object_tracing_common.h>
#include <toolchain.h>
#include <linker/sections.h>
#include <ksched.h>
#include <wait_q.h>
#include <misc/__assert.h>
#include <init.h>
#include <syscall_handler.h>

extern struct k_stack _k_stack_list_start[];
extern struct k_stack _k_stack_list_end[];

#ifdef CONFIG_OBJECT_TRACING

struct k_stack *_trace_list_k_stack;

/*
 * Complete initialization of statically defined stacks.
 */
static int init_stack_module(struct device *dev)
{
	ARG_UNUSED(dev);

	struct k_stack *stack;

	for (stack = _k_stack_list_start; stack < _k_stack_list_end; stack++) {
		SYS_TRACING_OBJ_INIT(k_stack, stack);
	}
	return 0;
}

SYS_INIT(init_stack_module, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_OBJECTS);

#endif /* CONFIG_OBJECT_TRACING */

void _impl_k_stack_init(struct k_stack *stack, u32_t *buffer, int num_entries)
{
	sys_dlist_init(&stack->wait_q);
	stack->next = stack->base = buffer;
	stack->top = stack->base + num_entries;

	SYS_TRACING_OBJ_INIT(k_stack, stack);
	_k_object_init(stack);
}

#ifdef CONFIG_USERSPACE
u32_t _handler_k_stack_init(u32_t stack, u32_t buffer, u32_t num_entries_p,
			    u32_t arg4, u32_t arg5, u32_t arg6, void *ssf)
{
	int num_entries = (int)num_entries_p;

	/* FIXME why is 'num_entries' signed?? */
	_SYSCALL_VERIFY(num_entries > 0, ssf);
	_SYSCALL_IS_OBJ(stack, K_OBJ_STACK, 1, ssf);
	_SYSCALL_MEMORY(buffer, num_entries * sizeof(u32_t), 1, ssf);

	_impl_k_stack_init((struct k_stack *)stack, (u32_t *)buffer,
			   num_entries);
	return 0;
}
#endif

void _impl_k_stack_push(struct k_stack *stack, u32_t data)
{
	struct k_thread *first_pending_thread;
	unsigned int key;

	__ASSERT(stack->next != stack->top, "stack is full");

	key = irq_lock();

	first_pending_thread = _unpend_first_thread(&stack->wait_q);

	if (first_pending_thread) {
		_abort_thread_timeout(first_pending_thread);
		_ready_thread(first_pending_thread);

		_set_thread_return_value_with_data(first_pending_thread,
						   0, (void *)data);

		if (!_is_in_isr() && _must_switch_threads()) {
			(void)_Swap(key);
			return;
		}
	} else {
		*(stack->next) = data;
		stack->next++;
	}

	irq_unlock(key);
}

#ifdef CONFIG_USERSPACE
u32_t _handler_k_stack_push(u32_t stack_p, u32_t data, u32_t arg3,
			    u32_t arg4, u32_t arg5, u32_t arg6, void *ssf)
{
	struct k_stack *stack = (struct k_stack *)stack_p;
	_SYSCALL_ARG2;

	_SYSCALL_IS_OBJ(stack, K_OBJ_STACK, 0, ssf);
	_SYSCALL_VERIFY(stack->next != stack->top, ssf);

	_impl_k_stack_push(stack, data);
	return 0;
}
#endif

int _impl_k_stack_pop(struct k_stack *stack, u32_t *data, s32_t timeout)
{
	unsigned int key;
	int result;

	key = irq_lock();

	if (likely(stack->next > stack->base)) {
		stack->next--;
		*data = *(stack->next);
		irq_unlock(key);
		return 0;
	}

	if (timeout == K_NO_WAIT) {
		irq_unlock(key);
		return -EBUSY;
	}

	_pend_current_thread(&stack->wait_q, timeout);

	result = _Swap(key);
	if (result == 0) {
		*data = (u32_t)_current->base.swap_data;
	}
	return result;
}

#ifdef CONFIG_USERSPACE
u32_t _handler_k_stack_pop(u32_t stack, u32_t data, u32_t timeout,
			   u32_t arg4, u32_t arg5, u32_t arg6, void *ssf)
{
	_SYSCALL_ARG3;

	_SYSCALL_IS_OBJ(stack, K_OBJ_STACK, 0, ssf);
	_SYSCALL_MEMORY(data, sizeof(u32_t), 1, ssf);

	return _impl_k_stack_pop((struct k_stack *)stack, (u32_t *)data,
				 timeout);

}
#endif
