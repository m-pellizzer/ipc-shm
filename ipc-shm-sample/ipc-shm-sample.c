/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (C) 2018 NXP Semiconductors
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/semaphore.h>

#include "../ipc-shm-dev/ipc-shm.h"

#define MODULE_NAME "ipc-shm-sample"
#define MODULE_VER "0.1"

MODULE_AUTHOR("NXP");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS(MODULE_NAME);
MODULE_DESCRIPTION("NXP Shared Memory IPC Sample Application Module");
MODULE_VERSION(MODULE_VER);

#define LOCAL_SHM_ADDR 0x34000000
#define IPC_SHM_SIZE 0x100000 /* 1M local shm, 1M remote shm */
#define REMOTE_SHM_ADDR (LOCAL_SHM_ADDR + IPC_SHM_SIZE)
#define SHM_SAMPLE_BUF_SIZE 256
#define S_BUF_LEN 8
#define M_BUF_LEN 256
#define L_BUF_LEN 4*1024

/* convenience wrappers for printing messages */
#define sample_fmt(fmt) MODULE_NAME": %s(): "fmt
#define sample_err(fmt, ...) pr_err(sample_fmt(fmt), __func__, ##__VA_ARGS__)
#define sample_warn(fmt, ...) pr_warn(sample_fmt(fmt), __func__, ##__VA_ARGS__)
#define sample_info(fmt, ...) pr_info(MODULE_NAME": "fmt, ##__VA_ARGS__)
#define sample_dbg(fmt, ...) pr_debug(sample_fmt(fmt), __func__, ##__VA_ARGS__)

static char *ipc_shm_sample_msg = "Hello world! ";
module_param(ipc_shm_sample_msg, charp, 0660);
MODULE_PARM_DESC(ipc_shm_sample_msg, "Message to be sent to the remote app.");

static int msg_sizes[16] = {M_BUF_LEN};
static int msg_argc = 1;
module_param_array(msg_sizes, int, &msg_argc, 0000);
MODULE_PARM_DESC(msg_sizes, "Sample message sizes");

static void shm_sample_rx_cb(void *cb_arg, int chan_id, void *buf, size_t size);

/**
 * struct ipc_sample_priv - sample private data
 * @run_cmd:		run command state: 1 - start; 0 - stop
 * @ipc_kobj:		sysfs kernel object
 * @run_attr:		sysfs run command attributes
 * @shm_sample_sema:	binary semaphore used to sync with Autosar app
 */
struct ipc_sample_priv {
	int run_cmd;
	struct kobject *ipc_kobj;
	struct kobj_attribute run_attr;
	struct semaphore shm_sample_sema;
	char last_rx_msg[L_BUF_LEN];
	char last_tx_msg[L_BUF_LEN];
};

/* sample private data */
static struct ipc_sample_priv priv;

/* IPC shared memory parameters (see ipc-shm.h) */
static struct ipc_shm_cfg shm_cfg = {
	.local_shm_addr = (void *)LOCAL_SHM_ADDR,
	.remote_shm_addr = (void *)REMOTE_SHM_ADDR,
	.shm_size = IPC_SHM_SIZE,
	.channels = {
		{
			.type = SHM_CHANNEL_MANAGED,
			.memory = {
				.managed = {
					.pools = {
						{
							.num_bufs = 5,
							.buf_size = S_BUF_LEN
						},
						{
							.num_bufs = 5,
							.buf_size = M_BUF_LEN
						},
						{
							.num_bufs = 5,
							.buf_size = L_BUF_LEN
						},
					},
				},
			},
			.ops = {
				.cb_arg = &priv,
				.rx_cb = shm_sample_rx_cb,
				.rx_unmanaged_cb = NULL,
			},
		},
	}
};

/*
 * generate_data() - generates data a-z with rollover.
 * @s: string that saves the generated data
 * @len: generated data length
 *
 * Return: pointer to the string
 */
static char *generate_data(char *s, int len, int seq_no)
{
	int i, j;

	if (!s)
		goto out;

	snprintf(s, len, "#%d ", seq_no);
	for (i = strlen(s), j = 0; i < len - 1; i++, j++) {
		s[i] = ipc_shm_sample_msg[j % strlen(ipc_shm_sample_msg)];
	}
	s[i] = '\0';

out:
	return s;
}

/*
 * shm RX callback. Prints the data, releases the channel and releases the
 * semaphore.
 */
static void shm_sample_rx_cb(void *cb_arg, int chan_id, void *buf,
			     size_t size)
{
	struct ipc_sample_priv *prv = (struct ipc_sample_priv *)cb_arg;
	int err = 0;

	/* process the received data */
	sample_info("ch %d: << %d bytes:%*.s\n",
		    chan_id, (int)size, (int)size, (char *)buf);
	memcpy(priv.last_rx_msg, buf, size);

	/* release the buffer */
	err = ipc_shm_release_buf(chan_id, buf);
	if (err) {
		sample_err("failed to free buffer for channel %d,"
			    "err code %d\n", chan_id, err);
	}

	/* signal echo reply via semaphore */
	up(&prv->shm_sample_sema);
}

/*
 * Implements the ipc-shm sample.
 * The sample sends a message for each received message from the remote CPU.
 * It uses a semaphore for synchronization (incremented by the RX callback
 * and decremented before this thread sends data to the remote CPU).
 */
static int run_demo(void)
{
	int err = 0;
	int i, j;
	int size = 0;
	int chan_id = 0;
	char *buf = NULL;

	sample_info("starting demo...\n");

	/* init binary semaphore with zero to block after tx until a reply is
	 * received from remote OS and rx callback unlocks the semaphore
	 */
	sema_init(&priv.shm_sample_sema, 0);

	sample_dbg("semaphore initialized...\n");

	for (i = 0; i < msg_argc; i++) {
		size = msg_sizes[i];
		for (j = 0; j < priv.run_cmd; j++) {
			if (strcmp(priv.last_rx_msg, priv.last_tx_msg) != 0) {
				sample_err("last rx msg != last tx msg\n");
				sample_err(">> %s\n", priv.last_tx_msg);
				sample_err("<< %s\n", priv.last_rx_msg);
				err = -EINVAL;
				return err;
			}

			buf = ipc_shm_acquire_buf(chan_id, size);
			if (!buf) {
				sample_err("failed to get buffer for channel ID"
					   " %d and size %d\n", chan_id, size);
				err = -ENOMEM;
				return err;
			}

			/* write data to buf */
			generate_data(buf, size, j + 1);
			memcpy(priv.last_tx_msg, buf, size);

			sample_info("ch %d: >> %d bytes:%*.s\n", chan_id, size,
				    size, (char *)buf);

			/* send data to peer */
			err = ipc_shm_tx(chan_id, buf, size);
			if (err) {
				sample_err("tx failed for channel ID %d, size "
					   "%d, error code %d\n", 0, size, err);
				return err;
			}

			/* get semaphore */
			err = down_interruptible(&priv.shm_sample_sema);
			if (err == -EINTR) {
				sample_info("interrupted...\n");
				return err;
			}

			if (err) {
				sample_err("failed to get semaphore for channel"
					   " ID %d, error code %d\n", 0, err);
				return err;
			}
		}
	}

	sample_info("demo ended\n");
	return 0;
}

/*
 * callback called when reading sysfs command file
 */
static ssize_t ipc_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	int value = 0;

	if (strcmp(attr->attr.name, priv.run_attr.attr.name) == 0) {
		value = priv.run_cmd;
	}

	return sprintf(buf, "%d\n", value);
}

/*
 * callback called when writing in sysfs command file
 */
static ssize_t ipc_sysfs_store(struct kobject *kobj,
			       struct kobj_attribute *attr, const char *buf,
			       size_t count)
{
	int value;
	int err;

	err = kstrtoint(buf, 0, &value);
	if (err)
		return err;

	if (strcmp(attr->attr.name, priv.run_attr.attr.name) == 0) {
		priv.run_cmd = value;
		run_demo();
	}

	return count;
}

/*
 * Init sysfs folder and command file
 */
static int ipc_sysfs_init(void)
{
	int err = 0;
	struct kobj_attribute run_attr =
		__ATTR(run, 0600, ipc_sysfs_show, ipc_sysfs_store);
	priv.run_attr = run_attr;

	/* create ipc-sample folder in sys/kernel */
	priv.ipc_kobj = kobject_create_and_add(MODULE_NAME, kernel_kobj);
	if (!priv.ipc_kobj)
		return -ENOMEM;

	/* create sysfs file for ipc sample run command */
	err = sysfs_create_file(priv.ipc_kobj, &priv.run_attr.attr);
	if (err) {
		sample_err("sysfs file creation failed, error code %d\n", err);
		goto err_kobj_free;
	}

	return 0;

err_kobj_free:
	kobject_put(priv.ipc_kobj);
	return err;
}

static void ipc_sysfs_free(void)
{
	kobject_put(priv.ipc_kobj);
}

static int __init sample_mod_init(void)
{
	int err = 0;

	sample_dbg("module version "MODULE_VER" init\n");

	err = ipc_sysfs_init();
	if (err)
		return err;

	err = ipc_shm_init(&shm_cfg);
	if (err)
		goto err_sysfs_free;

	return 0;

err_sysfs_free:
	ipc_sysfs_free();
	return err;
}

static void __exit sample_mod_exit(void)
{
	sample_dbg("module version "MODULE_VER" exit\n");
	/* stop the demo */
	priv.run_cmd = 0;

	ipc_shm_free();
	ipc_sysfs_free();
}

module_init(sample_mod_init);
module_exit(sample_mod_exit);
