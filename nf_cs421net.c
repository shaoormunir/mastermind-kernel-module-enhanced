/*
 * Raise an interrupt upon each target skb, storing it for later
 * processing.
 *
 * Copyright(c) 2016-2019 Jason Tang <jtang@umbc.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "CS421Net: " fmt

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <linux/ip.h>
#include <linux/tcp.h>

#include "nf_cs421net.h"

/* defined in arch/x86/kernel/irq.c */
extern int trigger_irq(unsigned);

#define CS421NET_IRQ 6
static DEFINE_SPINLOCK(lock);
static DECLARE_COMPLETION(retrieved);
static bool cs421net_enabled;
struct workqueue_struct *cs421net_wq;

struct incoming_data {
	char *data;
	size_t len;
	struct list_head list;
};
static LIST_HEAD(cs421net_list);

/**
 * cs421net_enable() - start capturing data from the network
 *
 * This device will now raise interrupts when data arrive.
 */
void cs421net_enable(void)
{
	cs421net_enabled = true;
	reinit_completion(&retrieved);
}

EXPORT_SYMBOL(cs421net_enable);

/**
 * cs421net_disable() - stop capturing data from the network
 *
 * This device will stop raising interrupts when data arrive.
 */
void cs421net_disable(void)
{
	cs421net_enabled = false;
	if (!completion_done(&retrieved))
		complete_all(&retrieved);
}

EXPORT_SYMBOL(cs421net_disable);

/**
 * cs421net_get_data() - retrieve the oldest pending data
 *
 * This function is safe to be called from within interrupt context.
 *
 * If all data have been retrieved, then this function returns
 * NULL. Otherwise, it returns the oldest data.
 *
 * WARNING: The returned buffer is NOT A STRING; it is not necessarily
 * null-terminated.
 *
 * @len: out parameter to store data length
 * Return: allocated data buffer (caller is responsible for freeing
 * it), or %NULL if none pending
 */
char *cs421net_get_data(size_t * const len)
{
	struct incoming_data *oldest;
	unsigned long flags;
	void *payload;

	spin_lock_irqsave(&lock, flags);
	oldest =
	    list_first_entry_or_null(&cs421net_list, struct incoming_data,
				     list);
	if (!oldest) {
		spin_unlock_irqrestore(&lock, flags);
		*len = 0;
		return NULL;
	}
	payload = oldest->data;
	*len = oldest->len;
	list_del(&oldest->list);
	kfree(oldest);
	spin_unlock_irqrestore(&lock, flags);

	complete(&retrieved);
	return payload;
}

EXPORT_SYMBOL(cs421net_get_data);

/**
 * cs421net_work_func() - function invoked by the workqueue
 *
 * Raise an interrupt, then wait for the interrupt handler to
 * acknowledge the interrupt.
 */
static void cs421net_work_func(struct work_struct *work)
{
	if (trigger_irq(CS421NET_IRQ) < 0)
		pr_err("Could not generate interrupt\n");
}

static DECLARE_WORK(cs421net_work, cs421net_work_func);

/**
 * cs421net_hook() - log incoming data from CS421Net
 *
 * For each skb, add the payload to the list and schedule an
 * interrupt.
 */
static unsigned int
cs421net_hook(void *priv, struct sk_buff *skb,
	      const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	char *payload_data;
	unsigned payload_len;
	struct incoming_data *data;
	unsigned long flags;

	if (!cs421net_enabled)
		goto out;

	if (!skb)
		goto out;
	iph = ip_hdr(skb);
	if (!iph || iph->protocol != IPPROTO_TCP)
		goto out;
	tcph = tcp_hdr(skb);
	if (!tcph || ntohs(tcph->dest) != 4210)
		goto out;

	payload_data = skb_network_header(skb) + (iph->ihl * 4) +
	    (tcph->doff * 4);
	payload_len = ntohs(iph->tot_len) - (iph->ihl * 4) - (tcph->doff * 4);
	if (payload_len == 0)
		goto out;
	data = kmalloc(sizeof(*data), GFP_ATOMIC);
	if (!data)
		goto out;
	data->len = payload_len;
	data->data = kzalloc(100, GFP_ATOMIC);
	if (!data->data)
		goto out_free_node;
	if (skb_copy_bits(skb, iph->ihl * 4 + tcph->doff * 4,
			  data->data, payload_len))
		goto out_free_data_buf;

	spin_lock_irqsave(&lock, flags);
	list_add_tail(&data->list, &cs421net_list);
	spin_unlock_irqrestore(&lock, flags);
	pr_info("Raising IRQ %u for payload length %zu\n", CS421NET_IRQ,
		data->len);
	queue_work(cs421net_wq, &cs421net_work);

	goto out;

out_free_data_buf:
	kfree(data->data);
out_free_node:
	kfree(data);
out:
	return NF_ACCEPT;
}

static struct nf_hook_ops nf_cs421net = {
	.hook = cs421net_hook,
	.hooknum = NF_INET_LOCAL_IN,
	.pf = PF_INET,
	.priority = NF_IP_PRI_MANGLE,
};

static int __init cs421net_init(void)
{
	int retval;

	cs421net_wq = create_singlethread_workqueue("CS421Net");
	if (!cs421net_wq) {
		retval = -ENOMEM;
		goto out;
	}
	retval = nf_register_net_hook(&init_net, &nf_cs421net);
	if (retval < 0)
		destroy_workqueue(cs421net_wq);
out:
	pr_info("initialization returning %d\n", retval);
	return retval;
}

static void __exit cs421net_exit(void)
{
	struct incoming_data *data, *tmp;

	cs421net_enabled = false;
	nf_unregister_net_hook(&init_net, &nf_cs421net);
	complete_all(&retrieved);
	cancel_work_sync(&cs421net_work);
	destroy_workqueue(cs421net_wq);
	list_for_each_entry_safe(data, tmp, &cs421net_list, list) {
		kfree(data->data);
		kfree(data);
	}
	pr_info("exited\n");
}

module_init(cs421net_init);
module_exit(cs421net_exit);

MODULE_DESCRIPTION("CS421 Virtual Network");
MODULE_LICENSE("GPL");
