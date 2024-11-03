#include <net/if.h>

#include <errno.h>
#include <kmod.h>
#include <pci.h>
#include <std.h>
#include <mem.h>

#define PMEM_BEGIN (16 * 1024)
#define PMEM_END   (PMEM_BEGIN + PMEM_SIZE)
#define PMEM_SIZE  (32 * 1024)

#define PMEM_RX_BEGIN (PMEM_BEGIN)
#define PMEM_RX_END   (PMEM_RX_BEGIN + PMEM_RX_SIZE)
#define PMEM_RX_SIZE  (30 * 1024)

#define PMEM_TX_BEGIN (PMEM_RX_END)
#define PMEM_TX_END   (PMEM_TX_BEGIN + PMEM_TX_SIZE)
#define PMEM_TX_SIZE  (2 * 1024)

#define REG_CR  0x00
#define REG_DMA 0x10
#define REG_RST 0x1F

/* page0 r */
#define REG_0R_CLDA0   0x1
#define REG_0R_CLDA1   0x2
#define REG_0R_BNRY    0x3
#define REG_0R_TSR     0x4
#define REG_0R_NCR     0x5
#define REG_0R_FIFO    0x6
#define REG_0R_ISR     0x7
#define REG_0R_CRDA0   0x8
#define REG_0R_CRDA1   0x9
#define REG_0R_8029ID0 0xA
#define REG_0R_8029ID1 0xB
#define REG_0R_RSR     0xC
#define REG_0R_CNTR0   0xD
#define REG_0R_CNTR1   0xE
#define REG_0R_CNTR2   0xF

/* page0 w */
#define REG_0W_PSTART 0x1
#define REG_0W_PSTOP  0x2
#define REG_0W_BNRY   0x3
#define REG_0W_TPSR   0x4
#define REG_0W_TBCR0  0x5
#define REG_0W_TBCR1  0x6
#define REG_0W_ISR    0x7
#define REG_0W_RSAR0  0x8
#define REG_0W_RSAR1  0x9
#define REG_0W_RBCR0  0xA
#define REG_0W_RBCR1  0xB
#define REG_0W_RCR    0xC
#define REG_0W_TCR    0xD
#define REG_0W_DCR    0xE
#define REG_0W_IMR    0xF

/* page1 rw */
#define REG_1RW_PAR0 0x1
#define REG_1RW_PAR1 0x2
#define REG_1RW_PAR2 0x3
#define REG_1RW_PAR3 0x4
#define REG_1RW_PAR4 0x5
#define REG_1RW_PAR5 0x6
#define REG_1RW_CURR 0x7
#define REG_1RW_MAR0 0x8
#define REG_1RW_MAR1 0x9
#define REG_1RW_MAR2 0xA
#define REG_1RW_MAR3 0xB
#define REG_1RW_MAR4 0xC
#define REG_1RW_MAR5 0xD
#define REG_1RW_MAR6 0xE
#define REG_1RW_MAR7 0xF

/* page2 r */
#define REG_2R_PSTART 0x1
#define REG_2R_PSTOP  0x2
#define REG_2R_TPSR   0x4
#define REG_2R_RCR    0xC
#define REG_2R_TCR    0xD
#define REG_2R_DCR    0xE
#define REG_2R_IMR    0xF

/* page3 r */
#define REG_3R_9346CR    0x1
#define REG_3R_CONFIG0   0x3
#define REG_3R_CONFIG2   0x5
#define REG_3R_CONFIG3   0x6
#define REG_3R_8029ASID0 0xE
#define REG_3R_8029ASID1 0xF

/* page3 w */
#define REG_3W_9346CR  0x1
#define REG_3W_CONFIG2 0x5
#define REG_3W_CONFIG3 0x6
#define REG_3W_HLTCLK  0x7

#define CR_STP   (1 << 0) /* stop command */
#define CR_STA   (1 << 1) /* useless ??? */
#define CR_TXP   (1 << 2) /* start transmit */
#define CR_RD(n) (((n) & 0x7) << 3) /* remote status */
#define CR_PS(n) (((n) & 0x3) << 6) /* page select */

#define INT_PRX (1 << 0) /* packet received without error */
#define INT_PTX (1 << 1) /* packet transmitted without error */
#define INT_RXE (1 << 2) /* packet received with CRC error / alignment error / missed packet */
#define INT_TXE (1 << 3) /* packet transmit aborted */
#define INT_OVW (1 << 4) /* rx buffer overflow */
#define INT_CNT (1 << 5) /* MSB of tally counter set */
#define INT_RDC (1 << 6) /* DMA completed */
#define INT_RST (1 << 7) /* reset state */

#define RCR_SEP (1 << 0) /* accept packets with receive errors */
#define RCR_AR  (1 << 1) /* accept packets < 64 bytes */
#define RCR_AB  (1 << 2) /* accept broadcast packets */
#define RCR_AM  (1 << 3) /* accept multicast packets */
#define RCR_PRO (1 << 4) /* promiscious mode */
#define RCR_MON (1 << 5) /* monitor mode (packets data are not transfered) */

#define DCR_WTS (1 << 0) /* word transfer select (1 = 16bits, 0 = 8bits) */
#define DCR_BOS (1 << 1) /* byte order select (1 = msb, 0 = lsb) */
#define DCR_LAS (1 << 2) /* must be zero */
#define DCR_LS  (1 << 3) /* loopback select */
#define DCR_ARM (1 << 4) /* auto-init remote */
#define DCR_FT0 (1 << 5) /* fifo threshold bit 0 */
#define DCR_FT1 (1 << 6) /* fifo threshold bit 1 */

#define RSR_PRX (1 << 0) /* packet rx ok */
#define RSR_CRC (1 << 1) /* CRC error */
#define RSR_FAE (1 << 2) /* frame alignment error */
#define RSR_MPA (1 << 4) /* missed packet */
#define RSR_PHY (1 << 5) /* multicast / broadcast dst */
#define RSR_DIS (1 << 6) /* disabled receiver */
#define RSR_DFR (1 << 7) /* deferring */

#define NE2K_RU8(ne2k, reg) pci_ru8(&(ne2k)->pci_map, reg)
#define NE2K_RU16(ne2k, reg) pci_ru16(&(ne2k)->pci_map, reg)
#define NE2K_RU32(ne2k, reg) pci_ru32(&(ne2k)->pci_map, reg)
#define NE2K_WU8(ne2k, reg, val) pci_wu8(&(ne2k)->pci_map, reg, val)
#define NE2K_WU16(ne2k, reg, val) pci_wu16(&(ne2k)->pci_map, reg, val)
#define NE2K_WU32(ne2k, reg, val) pci_wu32(&(ne2k)->pci_map, reg, val)
#define NE2K_LOCK(ne2k) mutex_lock(&(ne2k)->mutex)
#define NE2K_UNLOCK(ne2k) mutex_unlock(&(ne2k)->mutex)

struct ne2k
{
	struct pci_device *device;
	struct pci_map pci_map;
	struct waitq waitq;
	struct mutex mutex;
	size_t tx_len;
	uint8_t rx_off;
	int tx_avail;
	struct netif *netif;
	struct irq_handle irq_handle;
};

static inline void dump_regs(struct ne2k *ne2k)
{
	uint8_t cr = NE2K_RU8(ne2k, REG_CR);
	for (size_t p = 0; p < 4; ++p)
	{
		NE2K_WU8(ne2k, REG_CR, CR_RD(1) | CR_PS(p));
		for (size_t i = 0; i < 0x10; ++i)
			printf("%02x ", NE2K_RU8(ne2k, i));
		printf("\n");
	}
	NE2K_WU8(ne2k, REG_CR, cr);
}

int emit_pkt(struct netif *netif, struct netpkt *pkt)
{
	struct ne2k *ne2k = netif->userdata;
	if (pkt->len >= PMEM_TX_SIZE)
		return -ENOBUFS;
	NE2K_LOCK(ne2k);
	while (!ne2k->tx_avail)
	{
		int ret = waitq_wait_tail_mutex(&ne2k->waitq, &ne2k->mutex,
		                                NULL);
		if (ret)
		{
			NE2K_UNLOCK(ne2k);
			return ret;
		}
	}
	NE2K_WU8(ne2k, REG_0W_RSAR0, 0);
	NE2K_WU8(ne2k, REG_0W_RSAR1, PMEM_TX_BEGIN >> 8);
	NE2K_WU8(ne2k, REG_0W_RBCR0, (pkt->len >> 0) & 0xFF);
	NE2K_WU8(ne2k, REG_0W_RBCR1, (pkt->len >> 8) & 0xFF);
	uint32_t longs = pkt->len / 4;
	for (uint32_t i = 0; i < longs; ++i)
		NE2K_WU32(ne2k, REG_DMA, ((uint32_t*)pkt->data)[i]);
	uint32_t bytes = pkt->len % 4;
	for (uint32_t i = 0; i < bytes; ++i)
		NE2K_WU8(ne2k, REG_DMA, ((uint8_t*)pkt->data)[longs * 4 + i]);
	NE2K_WU8(ne2k, REG_0W_TBCR0, (pkt->len >> 0) & 0xFF);
	NE2K_WU8(ne2k, REG_0W_TBCR1, (pkt->len >> 8) & 0xFF);
	NE2K_WU8(ne2k, REG_0W_TPSR, PMEM_TX_BEGIN >> 8);
	NE2K_WU8(ne2k, REG_CR, CR_RD(1) | CR_PS(0) | CR_TXP);
	ne2k->tx_len = pkt->len;
	ne2k->tx_avail = 0;
	NE2K_UNLOCK(ne2k);
	return 0;
}

static const struct netif_op netif_op =
{
	.emit = emit_pkt,
};

void rx_pkt(struct ne2k *ne2k)
{
	struct netpkt *pkt = NULL;
	NE2K_LOCK(ne2k);
	NE2K_WU8(ne2k, REG_0W_RSAR0, 0);
	NE2K_WU8(ne2k, REG_0W_RSAR1, ne2k->rx_off);
	NE2K_WU8(ne2k, REG_0W_RBCR0, 4);
	NE2K_WU8(ne2k, REG_0W_RBCR1, 0);
	uint32_t hdr = NE2K_RU32(ne2k, REG_DMA);
	uint8_t rsr  = (hdr >>  0) & 0xFF;
	uint8_t nxt  = (hdr >>  8) & 0xFF;
	uint16_t len = (hdr >> 16) & 0xFFFF;
#if 0
	printf("rsr: %x\n", rsr);
	printf("nxt: %u\n", nxt);
	printf("len: %u\n", len);
#endif
	if (rsr & (RSR_CRC | RSR_FAE | RSR_MPA | RSR_DFR))
	{
		ne2k->netif->stats.rx_errors++;
		goto end;
	}
	ne2k->netif->stats.rx_packets++;
	ne2k->netif->stats.rx_bytes += len;
	pkt = netpkt_alloc(len);
	if (!pkt)
	{
		printf("ne2k: failed to allocate packet\n");
		goto end;
	}
	NE2K_WU8(ne2k, REG_0W_RBCR0, (len >> 0) & 0xFF);
	NE2K_WU8(ne2k, REG_0W_RBCR1, (len >> 8) & 0xFF);
	uint32_t longs = len / 4;
	for (uint32_t i = 0; i < longs; ++i)
		((uint32_t*)pkt->data)[i] = NE2K_RU32(ne2k, REG_DMA);
	uint32_t bytes = len % 4;
	for (uint32_t i = 0; i < bytes; ++i)
		((uint8_t*)pkt->data)[longs * 4 + i] = NE2K_RU8(ne2k, REG_DMA);

end:
	ne2k->rx_off = nxt;
	NE2K_WU8(ne2k, REG_0W_BNRY, nxt);
	NE2K_UNLOCK(ne2k);
	if (pkt)
		ether_input(ne2k->netif, pkt);
}

void int_handler(void *userdata)
{
	struct ne2k *ne2k = userdata;
	uint8_t status = NE2K_RU8(ne2k, REG_0R_ISR);
	if (!status)
		return;
	NE2K_WU8(ne2k, REG_0W_ISR, status);
#if 0
	printf("status: %x\n", status);
#endif
	if (status & (INT_PTX | INT_TXE))
	{
		NE2K_LOCK(ne2k);
		if (status & INT_TXE)
		{
			ne2k->netif->stats.tx_errors++;
		}
		else
		{
			ne2k->netif->stats.tx_packets++;
			ne2k->netif->stats.tx_bytes += ne2k->tx_len;
		}
		ne2k->tx_avail = 1;
		waitq_broadcast(&ne2k->waitq, 0);
		NE2K_UNLOCK(ne2k);
	}
	if (status & (INT_PRX | INT_RXE))
		rx_pkt(ne2k);
#if 0
	dump_regs(ne2k);
#endif
}

int init_pci(struct pci_device *device, void *userdata)
{
	(void)userdata;
	struct ne2k *ne2k = malloc(sizeof(*ne2k), M_ZERO);
	if (!ne2k)
		return -ENOMEM;
	int ret = netif_alloc("eth", &netif_op, &ne2k->netif);
	if (ret)
	{
		printf("ne2k: failed to create netif\n");
		free(ne2k);
		return ret;
	}
	ne2k->netif->flags = IFF_UP | IFF_BROADCAST;
	ne2k->netif->userdata = ne2k;
	waitq_init(&ne2k->waitq);
	mutex_init(&ne2k->mutex, 0);
	ne2k->device = device;
	pci_enable_bus_mastering(device);
	ret = pci_map(&ne2k->pci_map, device->header0.bar0, PAGE_SIZE, 0);
	if (ret)
	{
		printf("ne2k: failed to init bar0\n");
		goto err;
	}
	NE2K_RU8(ne2k, REG_RST);
	NE2K_WU8(ne2k, REG_CR, CR_RD(1) | CR_PS(1));
	ne2k->netif->ether.addr[0] = NE2K_RU8(ne2k, REG_1RW_PAR0);
	ne2k->netif->ether.addr[1] = NE2K_RU8(ne2k, REG_1RW_PAR1);
	ne2k->netif->ether.addr[2] = NE2K_RU8(ne2k, REG_1RW_PAR2);
	ne2k->netif->ether.addr[3] = NE2K_RU8(ne2k, REG_1RW_PAR3);
	ne2k->netif->ether.addr[4] = NE2K_RU8(ne2k, REG_1RW_PAR4);
	ne2k->netif->ether.addr[5] = NE2K_RU8(ne2k, REG_1RW_PAR5);
	if (!ne2k->netif->ether.addr[0]
	 && !ne2k->netif->ether.addr[1]
	 && !ne2k->netif->ether.addr[2]
	 && !ne2k->netif->ether.addr[3]
	 && !ne2k->netif->ether.addr[4]
	 && !ne2k->netif->ether.addr[5])
	{
		NE2K_WU8(ne2k, REG_1RW_PAR0, 0x52);
		NE2K_WU8(ne2k, REG_1RW_PAR1, 0x54);
		NE2K_WU8(ne2k, REG_1RW_PAR2, 0x00);
		NE2K_WU8(ne2k, REG_1RW_PAR3, 0x12);
		NE2K_WU8(ne2k, REG_1RW_PAR4, 0x34);
		NE2K_WU8(ne2k, REG_1RW_PAR5, 0x56);
		ne2k->netif->ether.addr[0] = NE2K_RU8(ne2k, REG_1RW_PAR0);
		ne2k->netif->ether.addr[1] = NE2K_RU8(ne2k, REG_1RW_PAR1);
		ne2k->netif->ether.addr[2] = NE2K_RU8(ne2k, REG_1RW_PAR2);
		ne2k->netif->ether.addr[3] = NE2K_RU8(ne2k, REG_1RW_PAR3);
		ne2k->netif->ether.addr[4] = NE2K_RU8(ne2k, REG_1RW_PAR4);
		ne2k->netif->ether.addr[5] = NE2K_RU8(ne2k, REG_1RW_PAR5);
		if (ne2k->netif->ether.addr[0] != 0x52
		 || ne2k->netif->ether.addr[1] != 0x54
		 || ne2k->netif->ether.addr[2] != 0x00
		 || ne2k->netif->ether.addr[3] != 0x12
		 || ne2k->netif->ether.addr[4] != 0x34
		 || ne2k->netif->ether.addr[5] != 0x56)
		{
			printf("ne2k: failed to set mac address\n");
			ret = -EINVAL; /* XXX */
			goto err;
		}
	}
	NE2K_WU8(ne2k, REG_CR, CR_RD(1) | CR_PS(1));
	NE2K_WU8(ne2k, REG_1RW_CURR, PMEM_BEGIN >> 8);
	NE2K_WU8(ne2k, REG_CR, CR_RD(1) | CR_PS(0));
	NE2K_WU8(ne2k, REG_0W_DCR, 0);
	NE2K_WU8(ne2k, REG_0W_RCR, RCR_SEP | RCR_AR | RCR_AB);
	ne2k->tx_avail = 1;
	ne2k->rx_off = PMEM_RX_BEGIN >> 8;
	NE2K_WU8(ne2k, REG_0W_BNRY, PMEM_RX_BEGIN >> 8);
	NE2K_WU8(ne2k, REG_0W_PSTART, PMEM_RX_BEGIN >> 8);
	NE2K_WU8(ne2k, REG_0W_PSTOP, PMEM_RX_END >> 8);
	NE2K_WU8(ne2k, REG_0W_RSAR0, (PMEM_RX_BEGIN >> 0) & 0xFF);
	NE2K_WU8(ne2k, REG_0W_RSAR1, (PMEM_RX_BEGIN >> 8) & 0xFF);
	NE2K_WU8(ne2k, REG_0W_ISR, 0xFF);
	NE2K_WU8(ne2k, REG_0W_IMR, INT_PRX | INT_PTX | INT_RXE | INT_TXE | INT_OVW);
#if 0
	dump_regs(ne2k);
#endif
	ret = register_pci_irq(device, int_handler, ne2k, &ne2k->irq_handle);
	if (ret)
	{
		printf("ne2k: failed to enable irq\n");
		goto err;
	}
	return 0;

err:
	mutex_destroy(&ne2k->mutex);
	waitq_destroy(&ne2k->waitq);
	netif_free(ne2k->netif);
	free(ne2k);
	return ret;
}

int init(void)
{
	pci_probe(0x10EC, 0x8029, init_pci, NULL);
	return 0;
}

void fini(void)
{
}

struct kmod_info kmod =
{
	.magic = KMOD_MAGIC,
	.version = 1,
	.name = "ne2k",
	.init = init,
	.fini = fini,
};
