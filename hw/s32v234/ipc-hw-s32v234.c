/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright 2018-2019,2021,2023 NXP
 */
#include <linux/io.h>

#include "ipc-shm.h"
#include "ipc-os.h"
#include "ipc-hw.h"
#include "ipc-hw-platform.h"

/**
 * struct ipc_hw_priv - platform specific private data
 *
 * @mscm_tx_irq:    MSCM inter-core interrupt reserved for shm driver tx
 * @mscm_rx_irq:    MSCM inter-core interrupt reserved for shm driver rx
 * @remote_core:    remote core to trigger the interrupt on
 * @mscm:           pointer to memory-mapped hardware peripheral MSCM
 */
static struct ipc_hw_priv {
	int mscm_tx_irq;
	int mscm_rx_irq;
	int remote_core;
	struct mscm_regs *mscm;
} priv;

/**
 * ipc_hw_get_rx_irq() - get MSCM inter-core interrupt index [0..3] used for Rx
 *
 * Return: MSCM inter-core interrupt index used for Rx
 */
int ipc_hw_get_rx_irq(const uint8_t instance)
{
	return priv.mscm_rx_irq;
}

/**
 * ipc_hw_init() - platform specific initialization
 *
 * @cfg:    configuration parameters
 *
 * inter_core_tx_irq can be disabled by passing IPC_IRQ_NONE, if polling is
 * desired in transmit notification path. inter_core_tx_irq and
 * inter_core_rx_irq are not allowed to have the same value to avoid possible
 * race conditions when updating the value of the IRSPRCn register.
 * If the value IPC_CORE_DEFAULT is passed as remote_core, the default value
 * defined for the selected platform will be used instead. local_core value has
 * no effect for this platform.
 *
 * Return: 0 for success, -EINVAL for either inter core interrupt invalid or
 *         invalid remote core, -ENOMEM for failing to map MSCM address space
 */
int ipc_hw_init(const uint8_t instance, const struct ipc_shm_cfg *cfg)
{
	/* map MSCM hardware peripheral block */
	void *addr = ipc_os_map_intc();

	return _ipc_hw_init(instance, cfg->inter_core_tx_irq,
			cfg->inter_core_rx_irq, &cfg->remote_core,
			&cfg->local_core, addr);
}

/**
 * _ipc_hw_init() - platform specific initialization
 *
 * Low level variant of ipc_hw_init() used by UIO device implementation.
 */
int _ipc_hw_init(const uint8_t instance, int tx_irq, int rx_irq,
		 const struct ipc_shm_remote_core *remote_core,
		 const struct ipc_shm_local_core *local_core, void *mscm_addr)
{
	(void)local_core; /* unused */

	if (!mscm_addr)
		return -EINVAL;

	priv.mscm = (struct mscm_regs *)mscm_addr;

	/* only M4 core is supported */
	if (remote_core->type != IPC_CORE_DEFAULT
		&& remote_core->type != IPC_CORE_M4) {
		return -EINVAL;
	}

	if (((tx_irq != IPC_IRQ_NONE)
			&& ((tx_irq < IRQ_ID_MIN) || (tx_irq > IRQ_ID_MAX)))
		|| (rx_irq < IRQ_ID_MIN) || (rx_irq > IRQ_ID_MAX)
		|| (rx_irq == tx_irq)) {
		return -EINVAL;
	}

	priv.mscm_tx_irq = tx_irq;
	priv.mscm_rx_irq = rx_irq;
	priv.remote_core = DEFAULT_REMOTE_CORE;

	/*
	 * disable rx irq source to avoid receiving an interrupt from remote
	 * before any of the buffer rings are initialized
	 */
	ipc_hw_irq_disable(instance);

	return 0;
}

/**
 * ipc_hw_free() - unmap MSCM IP block and clear irq
 */
void ipc_hw_free(const uint8_t instance)
{
	ipc_hw_irq_clear(instance);

	/* unmap MSCM hardware peripheral block */
	ipc_os_unmap_intc(priv.mscm);
}

/**
 * ipc_hw_irq_enable() - enable notifications from remote
 */
void ipc_hw_irq_enable(const uint8_t instance)
{
	uint16_t irsprc_mask;

	/* enable MSCM core-to-core interrupt routing */
	irsprc_mask = readw_relaxed(&priv.mscm->irsprc[priv.mscm_rx_irq]);
	writew_relaxed(irsprc_mask | MSCM_IRSPRCn_CPxE(A53),
			&priv.mscm->irsprc[priv.mscm_rx_irq]);
}

/**
 * ipc_hw_irq_disable() - disable notifications from remote
 */
void ipc_hw_irq_disable(const uint8_t instance)
{
	uint16_t irsprc_mask;

	/* disable MSCM core-to-core interrupt routing */
	irsprc_mask = readw_relaxed(&priv.mscm->irsprc[priv.mscm_rx_irq]);
	writew_relaxed(irsprc_mask & ~MSCM_IRSPRCn_CPxE(A53),
			&priv.mscm->irsprc[priv.mscm_rx_irq]);
}

/**
 * ipc_hw_irq_notify() - notify remote that data is available
 */
void ipc_hw_irq_notify(const uint8_t instance)
{
	if (priv.mscm_tx_irq == IPC_IRQ_NONE)
		return;

	/* trigger MSCM core-to-core directed interrupt */
	writel_relaxed(MSCM_IRCPGIR_TLF(MSCM_IRCPGIR_TLF_CPUTL) |
			MSCM_IRCPGIR_CPUTL(priv.remote_core) |
			MSCM_IRCPGIR_INTID(priv.mscm_tx_irq),
			&priv.mscm->ircpgir);
}

/**
 * ipc_hw_irq_clear() - clear available data notification
 */
void ipc_hw_irq_clear(const uint8_t instance)
{
	/* clear MSCM core-to-core directed interrupt */
	writel_relaxed(MSCM_IRCPxIR_INT(priv.mscm_rx_irq),
		       &priv.mscm->ircp1ir);
}
