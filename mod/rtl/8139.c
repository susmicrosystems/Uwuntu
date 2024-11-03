#define RTL_8139
#include "common.h"

#include <net/if.h>

#include <errno.h>
#include <mutex.h>
#include <waitq.h>
#include <time.h>
#include <pci.h>
#include <std.h>
#include <mem.h>

#define RXB_NPAGES 9
#define TXB_NDESC  4

struct rtl8139
{
	struct pci_device *device;
	struct pci_map pci_map;
	struct page *rxb_pages;
	uint8_t *rxb;
	uint32_t rxb_off;
	uint32_t rxb_len;
	struct page *txb_pages[TXB_NDESC];
	uint8_t *txb[TXB_NDESC];
	size_t tx_len[TXB_NDESC];
	uint32_t tx_head;
	uint32_t tx_tail;
	struct waitq waitq;
	struct mutex mutex;
	struct irq_handle irq_handle;
	struct netif *netif;
};

static int emit_pkt(struct netif *netif, struct netpkt *pkt)
{
	struct rtl8139 *rtl = netif->userdata;
	if (pkt->len >= 0x700)
		return -ENOBUFS;
	RTL_LOCK(rtl);
	while (!(RTL_RU32(rtl, REG_TSD0 + rtl->tx_tail * 4) & TSD_OWN))
	{
		int ret = waitq_wait_tail_mutex(&rtl->waitq, &rtl->mutex,
		                                NULL);
		if (ret)
		{
			RTL_UNLOCK(rtl);
			return ret;
		}
	}
	memcpy(rtl->txb[rtl->tx_tail], pkt->data, pkt->len);
	RTL_WU32(rtl, REG_TSAD0 + rtl->tx_tail * 4,
	         pm_page_addr(rtl->txb_pages[rtl->tx_tail]));
	RTL_WU32(rtl, REG_TSD0 + rtl->tx_tail * 4, pkt->len);
#if 0
	printf("[%lld] rtl output %lu\n", realtime_seconds(), rtl->tx_id);
#endif
	rtl->tx_len[rtl->tx_tail] = pkt->len;
	rtl->tx_tail = (rtl->tx_tail + 1) % TXB_NDESC;
	RTL_UNLOCK(rtl);
	return 0;
}

static const struct netif_op netif_op =
{
	.emit = emit_pkt,
};

static void rx_pkt(struct rtl8139 *rtl)
{
	uint8_t *pkt = &rtl->rxb[rtl->rxb_off];
	uint16_t hdr = ((uint16_t*)pkt)[0];
	uint16_t len = ((uint16_t*)pkt)[1];
	uint8_t *data = &pkt[4];
	if (hdr & (PKT_FAE | PKT_CRC | PKT_ISE))
	{
		rtl->netif->stats.rx_errors++;
		goto end;
	}
	if (!(hdr & PKT_ROK))
	{
		rtl->netif->stats.rx_errors++;
		goto end;
	}
	rtl->netif->stats.rx_packets++;
	rtl->netif->stats.rx_bytes += len;
	struct netpkt *netpkt = netpkt_alloc(len);
	if (!netpkt)
	{
		printf("rtl819: failed to allocate packet\n");
		goto end;
	}
#if 0
	printf("[%lld] rtl input %lx\n", realtime_seconds(), rtl->rxb_off);
#endif
	memcpy(netpkt->data, data, len);
	ether_input(rtl->netif, netpkt);

end:
	rtl->rxb_off = ((rtl->rxb_off + len + 4 + 3) & ~3) % rtl->rxb_len;
	RTL_WU32(rtl, REG_CAPR, rtl->rxb_off - 0x10);
}

static void int_handler(void *userdata)
{
	struct rtl8139 *rtl = userdata;
	uint16_t status = RTL_RU16(rtl, REG_ISR);
	if (!status)
		return;
	RTL_WU16(rtl, REG_ISR, status);
#if 0
	printf("[%lld] rtl int %x (tsad: %x)\n", realtime_seconds(),
	       status, RTL_RU16(rtl, REG_TSAD));
#endif
	if (status & (IR_ROK | IR_RER))
		rx_pkt(rtl);
	if (status & (IR_TOK | IR_TER))
	{
		while (rtl->tx_head != rtl->tx_tail)
		{
			if (RTL_RU32(rtl, REG_TSD0 + rtl->tx_head * 4) & TSD_TOK)
			{
				rtl->netif->stats.tx_packets++;
				rtl->netif->stats.tx_bytes += rtl->tx_len[rtl->tx_head];
			}
			else
			{
				rtl->netif->stats.tx_errors++;
			}
			rtl->tx_head = (rtl->tx_head + 1) % TXB_NDESC;
		}
		waitq_broadcast(&rtl->waitq, 0);
	}
}

int init_pci_8139(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct rtl8139 *rtl = malloc(sizeof(*rtl), M_ZERO);
	if (!rtl)
	{
		printf("rtl8139: rtl allocation failed\n");
		return -ENOMEM;
	}
	int ret = netif_alloc("eth", &netif_op, &rtl->netif);
	if (ret)
	{
		printf("rtl8139: netif creation failed\n");
		free(rtl);
		return ret;
	}
	rtl->netif->flags = IFF_UP | IFF_BROADCAST;
	rtl->netif->userdata = rtl;
	waitq_init(&rtl->waitq);
	mutex_init(&rtl->mutex, 0);
	rtl->device = device;
	pci_enable_bus_mastering(device);
	ret = pci_map(&rtl->pci_map, device->header0.bar0, PAGE_SIZE, 0);
	if (ret)
	{
		printf("rtl8139: failed to init bar0\n");
		goto err;
	}
	RTL_WU8(rtl, REG_CONFIG1, 0x0);
	RTL_WU8(rtl, REG_CR, CR_RST);
	while (RTL_RU8(rtl, REG_CR) & CR_RST)
		;
	rtl->netif->ether.addr[0] = RTL_RU8(rtl, REG_IDR0);
	rtl->netif->ether.addr[1] = RTL_RU8(rtl, REG_IDR1);
	rtl->netif->ether.addr[2] = RTL_RU8(rtl, REG_IDR2);
	rtl->netif->ether.addr[3] = RTL_RU8(rtl, REG_IDR3);
	rtl->netif->ether.addr[4] = RTL_RU8(rtl, REG_IDR4);
	rtl->netif->ether.addr[5] = RTL_RU8(rtl, REG_IDR5);
	ret = pm_alloc_pages(&rtl->rxb_pages, RXB_NPAGES);
	if (ret)
	{
		printf("rtl8139: failed to allocate rx buffer pages\n");
		goto err;
	}
	rtl->rxb = vm_map(rtl->rxb_pages, RXB_NPAGES * PAGE_SIZE, VM_PROT_RW);
	if (!rtl->rxb)
	{
		printf("rtl8139: failed to map rx buffer\n");
		ret = -ENOMEM;
		goto err;
	}
	rtl->rxb_len = 1024 * 64;
	for (size_t i = 0; i < TXB_NDESC; ++i)
	{
		ret = pm_alloc_page(&rtl->txb_pages[i]);
		if (ret)
		{
			printf("rtl8139: failed to allocate tx buffer page\n");
			goto err;
		}
		rtl->txb[i] = vm_map(rtl->txb_pages[i], PAGE_SIZE, VM_PROT_RW);
		if (!rtl->txb[i])
		{
			printf("rtl8139: failed to map tx buffer\n");
			ret = -ENOMEM;
			goto err;
		}
	}
	RTL_WU32(rtl, REG_RBSTART, pm_page_addr(rtl->rxb_pages));
	RTL_WU16(rtl, REG_IMR, IR_ROK | IR_TOK | IR_RER | IR_TER);
	RTL_WU32(rtl, REG_RCR, RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_WRAP | RCR_RXFTH(7) | RCR_RBLEN(3));
	ret = register_pci_irq(device, int_handler, rtl, &rtl->irq_handle);
	if (ret)
	{
		printf("rtl8139: failed to enable irq\n");
		goto err;
	}
	RTL_WU8(rtl, REG_CR, CR_RE | CR_TE);
	return 0;

err:
	for (size_t i = 0; i < TXB_NDESC; ++i)
	{
		if (rtl->txb[i])
			vm_unmap(rtl->txb[i], PAGE_SIZE);
		if (rtl->txb_pages[i])
			pm_free_page(rtl->txb_pages[i]);
	}
	if (rtl->rxb)
		vm_unmap(rtl->rxb, RXB_NPAGES * PAGE_SIZE);
	if (rtl->rxb_pages)
		pm_free_pages(rtl->rxb_pages, RXB_NPAGES);
	if (rtl->netif)
		netif_free(rtl->netif);
	waitq_destroy(&rtl->waitq);
	mutex_destroy(&rtl->mutex);
	free(rtl);
	return ret;
}
