#define RTL_8169
#include "common.h"

#include <net/if.h>

#include <errno.h>
#include <pci.h>
#include <std.h>
#include <mem.h>

#define DESC_LEN 0xFFF
#define DESC_COUNT 1024

struct rtl8169_desc
{
	uint32_t command;
	uint32_t vlan;
	uint32_t lowbuf;
	uint32_t highbuf;
};

struct rtl8169
{
	struct pci_device *device;
	struct pci_map pci_map;
	struct page *rxd_pages;
	struct page *txd_pages;
	struct rtl8169_desc *rxd;
	struct rtl8169_desc *txd;
	struct page *rxb_pages[DESC_COUNT];
	struct page *txb_pages[DESC_COUNT];
	uint8_t *rxb[DESC_COUNT];
	uint8_t *txb[DESC_COUNT];
	struct waitq waitq;
	struct mutex mutex;
	struct netif *netif;
	size_t txd_tail;
	size_t rxd_tail;
	struct irq_handle irq_handle;
};

static int emit_pkt(struct netif *netif, struct netpkt *pkt)
{
	struct rtl8169 *rtl = netif->userdata;
	if (pkt->len >= DESC_LEN)
		return -ENOBUFS;
	RTL_LOCK(rtl);
	while (rtl->txd[rtl->txd_tail].command & DESC_TX_OWN)
	{
		int ret = waitq_wait_tail_mutex(&rtl->waitq, &rtl->mutex,
		                                NULL);
		if (ret)
		{
			RTL_UNLOCK(rtl);
			return ret;
		}
	}
	memcpy(rtl->txb[rtl->txd_tail], pkt->data, pkt->len);
	struct rtl8169_desc *txd = &rtl->txd[rtl->txd_tail];
	txd->lowbuf = pm_page_addr(rtl->txb_pages[rtl->txd_tail]);
	txd->highbuf = 0;
	txd->vlan = 0;
	txd->command = pkt->len | DESC_TX_FS | DESC_TX_LS | DESC_TX_OWN;
	if (rtl->txd_tail == DESC_COUNT - 1)
		txd->command |= DESC_TX_EOR;
	RTL_WU8(rtl, REG_TPPOLL, TPPOLL_NPQ);
	while (RTL_RU8(rtl, REG_TPPOLL) & TPPOLL_NPQ)
		;
	rtl->netif->stats.tx_packets++;
	rtl->netif->stats.tx_bytes += pkt->len;
	rtl->txd_tail = (rtl->txd_tail + 1) % DESC_COUNT;
	RTL_UNLOCK(rtl);
	return 0;
}

static const struct netif_op netif_op =
{
	.emit = emit_pkt,
};

static void rx_desc(struct rtl8169 *rtl, struct rtl8169_desc *desc, size_t i)
{
	if (desc->command & (DESC_RX_BOVF | DESC_RX_FOVF | DESC_RX_RWT | DESC_RX_RES | DESC_RX_CRC))
	{
		rtl->netif->stats.rx_errors++;
		return;
	}
	size_t len = DESC_RX_LEN(desc->command);
	rtl->netif->stats.rx_packets++;
	rtl->netif->stats.rx_bytes += len;
	struct netpkt *netpkt = netpkt_alloc(len);
	if (!netpkt)
		panic("rtl8169: failed to allocate packet\n");
	memcpy(netpkt->data, rtl->rxb[i], len);
	ether_input(rtl->netif, netpkt);
}

static void rx_pkt(struct rtl8169 *rtl)
{
	while (1)
	{
		struct rtl8169_desc *desc = &rtl->rxd[rtl->rxd_tail];
		if (desc->command & DESC_RX_OWN)
			return;
		rx_desc(rtl, desc, rtl->rxd_tail);
		desc->vlan = 0;
		desc->lowbuf = pm_page_addr(rtl->rxb_pages[rtl->rxd_tail]);
		desc->highbuf = 0;
		desc->command = DESC_RX_OWN | DESC_LEN;
		if (rtl->rxd_tail == DESC_COUNT - 1)
			desc->command |= DESC_RX_EOR;
		rtl->rxd_tail = (rtl->rxd_tail + 1) % DESC_COUNT;
	}
}

static void int_handler(void *userdata)
{
	struct rtl8169 *rtl = userdata;
	uint16_t status = RTL_RU16(rtl, REG_ISR);
	if (!status)
		return;
#if 0
	printf("rtl int %04" PRIx16 "\n", status);
#endif
	RTL_WU16(rtl, REG_ISR, status);
	if (status & (IR_ROK | IR_RER))
		rx_pkt(rtl);
	if (status & (IR_TOK | IR_TER))
		waitq_broadcast(&rtl->waitq, 0);
}

static int alloc_desc(struct page **descs_pages, struct rtl8169_desc **descs,
                      struct page **bufs_pages, uint8_t **bufs, int rx)
{
	if (pm_alloc_pages(descs_pages,
	                   sizeof(struct rtl8169_desc) * DESC_COUNT / PAGE_SIZE))
	{
		printf("rtl8169: failed to allocate rxd pages\n");
		return -ENOMEM;
	}
	*descs = vm_map(*descs_pages, sizeof(struct rtl8169_desc) * DESC_COUNT,
	                VM_PROT_RW);
	if (!*descs)
	{
		printf("rtl8169: failed to map rxd\n");
		return -ENOMEM;
	}
	for (size_t i = 0; i < DESC_COUNT; ++i)
	{
		if (pm_alloc_page(&bufs_pages[i]))
		{
			printf("rtl819: failed to allocate rx page\n");
			return -ENOMEM;
		}
		bufs[i] = vm_map(bufs_pages[i], PAGE_SIZE, VM_PROT_RW);
		if (!bufs[i])
		{
			printf("rtl8169: failed to map rx page\n");
			return -ENOMEM;
		}
		struct rtl8169_desc *desc = &(*descs)[i];
		desc->vlan = 0;
		desc->lowbuf = pm_page_addr(bufs_pages[i]);
		desc->highbuf = 0;
		if (i == DESC_COUNT - 1)
			desc->command |= DESC_RX_EOR;
		else
			desc->command = 0;
		if (rx)
			desc->command |= DESC_LEN | DESC_RX_OWN;
	}
	return 0;
}

static void free_desc(struct page *pages, struct rtl8169_desc *descs,
                      struct page **bufs_pages, uint8_t **bufs)
{
	for (size_t i = 0; i < DESC_COUNT; ++i)
	{
		if (bufs[i])
			vm_unmap(bufs[i], PAGE_SIZE);
		if (bufs_pages[i])
			pm_free_page(bufs_pages[i]);
	}
	if (descs)
		vm_unmap(descs, sizeof(struct rtl8169_desc) * DESC_COUNT);
	if (pages)
		pm_free_pages(pages, sizeof(struct rtl8169_desc) * DESC_COUNT / PAGE_SIZE);
}

static void rtl8169_free(struct rtl8169 *rtl)
{
	if (!rtl)
		return;
	netif_free(rtl->netif);
	free_desc(rtl->txd_pages, rtl->txd, rtl->txb_pages, rtl->txb);
	free_desc(rtl->rxd_pages, rtl->rxd, rtl->rxb_pages, rtl->rxb);
	waitq_destroy(&rtl->waitq);
	mutex_destroy(&rtl->mutex);
	free(rtl);
}

int init_pci_8169(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct rtl8169 *rtl = malloc(sizeof(*rtl), M_ZERO);
	if (!rtl)
	{
		printf("rtl8169: rtl allocation failed\n");
		return -ENOMEM;
	}
	int ret = netif_alloc("eth", &netif_op, &rtl->netif);
	if (ret)
	{
		printf("rtl8169: netif creation failed\n");
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
		printf("rtl8169: failed to init bar0\n");
		goto err;
	}
	RTL_WU8(rtl, REG_CR, CR_RST);
	while (RTL_RU8(rtl, REG_CR) & CR_RST)
		;
	rtl->netif->ether.addr[0] = RTL_RU8(rtl, REG_IDR0);
	rtl->netif->ether.addr[1] = RTL_RU8(rtl, REG_IDR1);
	rtl->netif->ether.addr[2] = RTL_RU8(rtl, REG_IDR2);
	rtl->netif->ether.addr[3] = RTL_RU8(rtl, REG_IDR3);
	rtl->netif->ether.addr[4] = RTL_RU8(rtl, REG_IDR4);
	rtl->netif->ether.addr[5] = RTL_RU8(rtl, REG_IDR5);
	ret = alloc_desc(&rtl->rxd_pages, &rtl->rxd, rtl->rxb_pages,
	                 rtl->rxb, 1);
	if (ret)
		goto err;
	ret = alloc_desc(&rtl->txd_pages, &rtl->txd, rtl->txb_pages,
	                 rtl->txb, 0);
	if (ret)
		goto err;
	RTL_WU8(rtl, REG_9346CR, CR_UNLOCK);
	RTL_WU32(rtl, REG_RCR, RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_RXFTH(7) | RCR_MXDMA(7));
	RTL_WU32(rtl, REG_RDSAR, pm_page_addr(rtl->rxd_pages));
	RTL_WU16(rtl, REG_RMS, DESC_LEN);
	RTL_WU8(rtl, REG_CR, CR_TE);
	RTL_WU32(rtl, REG_TCR, TCR_IFG01(3) | TCR_MXDMA(0x7));
	RTL_WU32(rtl, REG_TNPDS, pm_page_addr(rtl->txd_pages));
	RTL_WU8(rtl, REG_ETTHR, 0x1);
	ret = register_pci_irq(device, int_handler, rtl, &rtl->irq_handle);
	if (ret)
	{
		printf("rtl8169: failed to enable IRQ\n");
		goto err;
	}
	RTL_WU8(rtl, REG_CR, CR_RE | CR_TE);
	RTL_WU16(rtl, REG_IMR, IR_ROK | IR_TOK | IR_RER | IR_TER);
	RTL_WU8(rtl, REG_9346CR, CR_LOCK);
	return 0;

err:
	rtl8169_free(rtl);
	return ret;
}
