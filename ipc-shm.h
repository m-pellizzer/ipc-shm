/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2018-2024 NXP
 */
#ifndef IPC_SHM_H
#define IPC_SHM_H

#include "ipc-types.h"

/**
 * ipc_shm_init_instance() - Initialize the specified instance
 *                           of the IPC-Shm driver
 *
 * @instance: instance id
 * @cfg:      ipc-shm instance configuration
 *
 * Return IPC_SHM_E_OK on success, error code otherwise
 */
int8_t ipc_shm_init_instance(uint8_t instance, const struct ipc_shm_cfg *cfg);

/**
 * ipc_shm_init() - initialize shared memory device
 * @cfg:              configuration parameters
 *
 * Function is non-reentrant.
 *
 * Return: 0 on success, error code otherwise
 */
int8_t ipc_shm_init(const struct ipc_shm_instances_cfg *cfg);

/**
 * ipc_shm_free_instance() - Deinitialize the specified instance
 *                           of the IPC-Shm driver
 *
 * @instance: instance id
 *
 * Function is non-reentrant.
 */
void ipc_shm_free_instance(const uint8_t instance);

/**
 * ipc_shm_free() - release all instances of shared memory device
 *
 * Function is non-reentrant.
 */
void ipc_shm_free(void);

/**
 * ipc_shm_acquire_buf() - request a buffer for the given channel
 * @instance:       instance id
 * @chan_id:        channel index
 * @mem_size:       required size
 *
 * Function used only for managed channels where buffer management is enabled.
 * Function is thread-safe for different channels but not for the same channel.
 *
 * Return: pointer to the buffer base address or NULL if buffer not found
 */
void *ipc_shm_acquire_buf(const uint8_t instance, uint8_t chan_id,
				uint32_t mem_size);

/**
 * ipc_shm_release_buf() - release a buffer for the given channel
 * @instance:       instance id
 * @chan_id:        channel index
 * @buf:            buffer pointer
 *
 * Function used only for managed channels where buffer management is enabled.
 * Function is thread-safe for different channels but not for the same channel.
 *
 * Return: 0 on success, error code otherwise
 */
int8_t ipc_shm_release_buf(const uint8_t instance, uint8_t chan_id,
				const void *buf);

/**
 * ipc_shm_tx() - send data on given channel and notify remote
 * @instance:       instance id
 * @chan_id:        channel index
 * @buf:            buffer pointer
 * @size:           size of data written in buffer
 *
 * Function used only for managed channels where buffer management is enabled.
 * Function is thread-safe for different channels but not for the same channel.
 *
 * Return: 0 on success, error code otherwise
 */
int8_t ipc_shm_tx(const uint8_t instance, int chan_id, void *buf,
			uint32_t size);

/**
 * ipc_shm_unmanaged_acquire() - acquire the unmanaged channel local memory
 * @instance:       instance id
 * @chan_id:        channel index
 *
 * Function used only for unmanaged channels. The memory must be acquired only
 * once after the channel is initialized. There is no release function needed.
 * Function is thread-safe for different channels but not for the same channel.
 *
 * Return: pointer to the channel memory or NULL if invalid channel
 */
void *ipc_shm_unmanaged_acquire(const uint8_t instance, uint8_t chan_id);

/**
 * ipc_shm_unmanaged_tx() - notify remote that data has been written in channel
 * @instance:       instance id
 * @chan_id:        channel index
 *
 * Function used only for unmanaged channels. It can be used after the channel
 * memory has been acquired whenever is needed to signal remote that new data
 * is available in channel memory.
 * Function is thread-safe for different channels but not for the same channel.
 *
 * Return: 0 on success, error code otherwise
 */
int8_t ipc_shm_unmanaged_tx(const uint8_t instance, uint8_t chan_id);

/**
 * ipc_shm_is_remote_ready() - check whether remote is initialized
 * @instance:        instance id
 *
 * Function used to check if the remote is initialized and ready to receive
 * messages. It should be invoked at least before the first transmit operation.
 * Function is thread-safe.
 *
 * Return: 0 if remote is initialized, error code otherwise
 */
int8_t ipc_shm_is_remote_ready(const uint8_t instance);

/**
 * ipc_shm_poll_channels() - poll the channels for available messages to process
 * @instance:        instance id
 *
 * This function handles all channels using a fair handling algorithm: all
 * channels are treated equally and no channel is starving.
 * Function is thread-safe for different instances but not for same instance.
 *
 * Return: number of messages processed, error code otherwise
 */
int8_t ipc_shm_poll_channels(const uint8_t instance);

#endif /* IPC_SHM_H */
