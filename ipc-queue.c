/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2018-2019,2022-2024 NXP
 */

#include "ipc-os.h"
#include "ipc-shm.h"
#include "ipc-queue.h"

/**
 * ipc_queue_pop() - removes element from queue
 * @queue:	[IN] queue pointer
 * @buf:	[OUT] pointer where to copy the removed element
 *
 * Element is removed from pop ring that is mapped in remote shared memory and
 * it corresponds to the remote push ring.
 *
 * Return:	0 on success, error code otherwise
 */
int8_t ipc_queue_pop(struct ipc_queue *queue, void *buf)
{
	uint32_t write; /* cache write index for thread-safety */
	uint32_t read; /* cache read index for thread-safety */
	void *src;

	if ((queue == NULL) || (buf == NULL)) {
		return -EINVAL;
	}

	write = queue->pop_ring->write;

	/* read indexes of push/pop rings are swapped (interference freedom) */
	read = queue->push_ring->read;

	/* Check integrity of queue and if read and write are valid value*/
	if ((ipc_queue_check_integrity(queue) != 0)
			|| (read >= queue->elem_num)
			|| (write >= queue->elem_num))
		return -EINVAL;

	/* check if queue is empty */
	if (read == write)
		return -ENOBUFS;

	/* copy queue element in buffer */
	src = &queue->pop_ring->data[read * queue->elem_size];
	(void) memcpy(buf, src, queue->elem_size);

	/* increment read index with wrap around */
	queue->push_ring->read = (read + 1u) % queue->elem_num;

	return 0;
}

/**
 * ipc_queue_push() - pushes element into the queue
 * @queue:	[IN] queue pointer
 * @buf:	[IN] pointer to element to be pushed into the queue
 *
 * Element is pushed into the push ring that is mapped in local shared memory
 * and corresponds to the remote pop ring.
 *
 * Return:	0 on success, error code otherwise
 */
int8_t ipc_queue_push(struct ipc_queue *queue, const void *buf)
{
	uint32_t write; /* cache write index for thread-safety */
	uint32_t read; /* cache read index for thread-safety */
	void *dst;

	if ((queue == NULL) || (buf == NULL)) {
		return -EINVAL;
	}

	write = queue->push_ring->write;

	/* read indexes of push/pop rings are swapped (interference freedom) */
	read = queue->pop_ring->read;

	/* Check if read and write are valid value */
	if ((read >= queue->elem_num) || (write >= queue->elem_num))
		return -EINVAL;

	/* check if queue is full ([write + 1 == read] because of sentinel) */
	if (((write + 1u) % queue->elem_num) == read)
		return -ENOMEM;

	/* copy element from buffer in queue */
	dst = &queue->push_ring->data[write * queue->elem_size];
	(void) memcpy(dst, buf, queue->elem_size);

	/* increment write index with wrap around */
	queue->push_ring->write = (write + 1u) % queue->elem_num;

	return 0;
}

/**
 * ipc_queue_sync_index() - synchronize queue read/write with remote memory
 * @queue:                  [IN] queue pointer
 * @queue_type:             [IN] indicate queue of channel or pool buffer
 *
 * Return: 0 on success, error code otherwise
 */
static int8_t ipc_queue_sync_index(struct ipc_queue *queue,
					enum ipc_shm_queue_type queue_type)
{
	/* Check if remote initialization is in progress */
	if (queue->pop_ring->sentinel == IPC_QUEUE_INIT_IN_PROGRESS)
		return -EAGAIN;

	/* Mark that the queue initialization is in progress */
	queue->push_ring->sentinel = IPC_QUEUE_INIT_IN_PROGRESS;

	if (queue->pop_ring->sentinel == IPC_QUEUE_INIT_DONE) {
		/* Use values from remote if it is already initialized */
		queue->push_ring->write = queue->pop_ring->read;
		if (queue_type == IPC_SHM_CHANNEL_QUEUE) {
			queue->push_ring->read = queue->pop_ring->write
							% queue->elem_num;
		} else {
			queue->push_ring->read = (queue->pop_ring->write + 1u)
							% queue->elem_num;
		}
		return 0;
	}

	queue->push_ring->write = 0;
	queue->push_ring->read = 0;

	return 0;
}

/**
 * ipc_queue_init() - initializes queue and maps push/pop rings in memory
 * @queue:		[IN] queue pointer
 * @elem_num:		[IN] number of elements in queue
 * @elem_size:		[IN] element size in bytes (8-byte multiple)
 * @push_ring_addr:	[IN] local addr where to map the push buffer ring
 * @pop_ring_addr:	[IN] remote addr where to map the pop buffer ring
 *
 * Element size must be 8-byte multiple to ensure memory alignment.
 *
 * Queue will add one additional sentinel element to its size for lock-free
 * single-producer - single-consumer thread-safety.
 *
 * Return: 0 on success, error code otherwise
 */
int8_t ipc_queue_init(struct ipc_queue *queue,
			struct ipc_queue_data queue_data)
{
	if ((queue == NULL)
		|| (queue_data.push_addr == (uintptr_t)NULL)
		|| (queue_data.pop_addr == (uintptr_t)NULL)
		|| (queue_data.elem_num == 0u)
		|| (queue_data.elem_size == 0u)
		|| ((queue_data.elem_size % 8u) != 0u)
		|| ((queue_data.queue_type != IPC_SHM_CHANNEL_QUEUE)
			&& (queue_data.queue_type != IPC_SHM_POOL_QUEUE))) {
		return -EINVAL;
	}

	/* add 1 sentinel element in queue for lock-free thread-safety */
	queue->elem_num = queue_data.elem_num + 1u;

	queue->elem_size = queue_data.elem_size;

	/* map and init push ring in local memory */
	queue->push_ring = (struct ipc_ring *)queue_data.push_addr;

	/* map pop ring in remote memory (init is done by remote) */
	queue->pop_ring = (struct ipc_ring *)queue_data.pop_addr;

	/* Synchronize read/write indexes */
	return ipc_queue_sync_index(queue, queue_data.queue_type);
}


void ipc_queue_free(struct ipc_queue *queue)
{
	/* Clear push ring sentinel and data */
	if (queue == NULL)
		return;

	if (queue->push_ring == NULL)
		return;

	if ((queue->push_ring->sentinel == IPC_QUEUE_INIT_DONE)
		|| (queue->push_ring->sentinel
			== IPC_QUEUE_INIT_IN_PROGRESS)) {
		queue->elem_num = 0;
		queue->elem_size = 0;
		queue->push_ring->sentinel = 0;
		queue->push_ring->write = 0;
		queue->push_ring->read = 0;
	}
	queue->push_ring = NULL;
}

/**
 * ipc_queue_check_integrity() - check if the sentinel was not overwritten
 * @queue:	[IN] queue pointer
 *
 * Check if the sentinel was not overwritten
 *
 * Return:	0 on success, error code otherwise
 */
int8_t ipc_queue_check_integrity(struct ipc_queue *queue)
{
	if ((IPC_QUEUE_INIT_DONE == queue->pop_ring->sentinel) &&
			(IPC_QUEUE_INIT_DONE == queue->push_ring->sentinel))
		return 0;

	return -EINVAL;
}
