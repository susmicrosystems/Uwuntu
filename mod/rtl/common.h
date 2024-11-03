#ifndef DEV_RTL_COMMON_H
#define DEV_RTL_COMMON_H

/*
 * RTL8139C(L)+ datasheet 1.6
 * RTL8169 1.21
 */

#define REG_IDR0      0x00
#define REG_IDR1      0x01
#define REG_IDR2      0x02
#define REG_IDR3      0x03
#define REG_IDR4      0x04
#define REG_IDR5      0x05
#define REG_MAR0      0x08
#define REG_MAR1      0x09
#define REG_MAR2      0x0A
#define REG_MAR3      0x0B
#define REG_MAR4      0x0C
#define REG_MAR5      0x0D
#define REG_MAR6      0x0E
#define REG_MAR7      0x0F

#ifdef RTL_8139
#define REG_TSD0      0x10
#define REG_TSD1      0x14
#define REG_TSD2      0x18
#define REG_TSD3      0x1C
#define REG_TSAD0     0x20
#define REG_TSAD1     0x24
#define REG_TSAD2     0x28
#define REG_TSAD3     0x2C
#define REG_RBSTART   0x30
#endif

#ifdef RTL_8169
#define REG_DTCCR     0x10
#define REG_TNPDS     0x20
#define REG_THPDS     0x28
#define REG_FLASH     0x30
#endif

#define REG_ERBCR     0x34
#define REG_ERSR      0x36
#define REG_CR        0x37

#ifdef RTL_8139
#define REG_CAPR      0x38
#define REG_CBR       0x3A
#endif

#ifdef RTL_8169
#define REG_TPPOLL    0x38
#endif

#define REG_IMR       0x3C
#define REG_ISR       0x3E
#define REG_TCR       0x40
#define REG_RCR       0x44
#define REG_TCTR      0x48
#define REG_MPC       0x4C
#define REG_9346CR    0x50

#ifdef RTL_8139
#define REG_CONFIG0   0x51
#define REG_CONFIG1   0x52
#define REG_TIMERINT  0x54
#define REG_MSR       0x58
#define REG_CONFIG3   0x59
#define REG_CONFIG4   0x5A
#define REG_MULINT    0x5C
#define REG_RERID     0x5E
#define REG_TSAD      0x60
#define REG_BMCR      0x62
#define REG_BMSR      0x64
#define REG_ANAR      0x66
#define REG_ANLPAR    0x68
#define REG_ANER      0x6A
#define REG_DIS       0x6C
#define REG_FCSC      0x6E
#define REG_NWAYTR    0x70
#define REG_REC       0x72
#define REG_CSCR      0x74
#define REG_PHY1_PARM 0x78
#define REG_TW_PARM   0x7C
#define REG_PHY2_PARM 0x80
#define REG_TDOKLADDR 0x82
#define REG_CRC0      0x84
#define REG_CRC1      0x85
#define REG_CRC2      0x84
#define REG_CRC3      0x86
#define REG_CRC4      0x87
#define REG_CRC5      0x88
#define REG_CRC6      0x89
#define REG_CRC7      0x8A
#define REG_WAKEUP0   0x8C
#define REG_WAKEUP1   0x94
#define REG_WAKEUP2   0x9C
#define REG_WAKEUP3   0xA4
#define REG_WAKEUP4   0xAC
#define REG_WAKEUP5   0xB4
#define REG_WAKEUP6   0xBC
#define REG_WAKEUP7   0xC4
#define REG_LSBCRC0   0xCC
#define REG_LSBCRC1   0xCD
#define REG_LSBCRC2   0xCE
#define REG_LSBCRC3   0xCF
#define REG_LSBCRC4   0xD0
#define REG_LSBCRC5   0xD1
#define REG_LSBCRC6   0xD2
#define REG_LSBCRC7   0xD3
#define REG_FLASH     0xD4
#define REG_CONFIG5   0xD8
#define REG_TPPOLL    0xD9
#endif

#ifdef RTL_8169
#define REG_CONFIG0   0x51
#define REG_CONFIG1   0x52
#define REG_CONFIG2   0x53
#define REG_CONFIG3   0x54
#define REG_CONFIG4   0x55
#define REG_CONFIG5   0x56
#define REG_TIMERINT  0x58
#define REG_MULINT    0x5C
#define REG_PHYAR     0x60
#define REG_TBICSR0   0x64
#define REG_TBI_ANAR  0x68
#define REG_TBI_LPAR  0x6A
#define REG_PHYSTATUS 0x6C
#define REG_WAKEUP0   0x84
#define REG_WAKEUP1   0x8C
#define REG_WAKEUP2LD 0x94
#define REG_WAKEUP2HD 0x9C
#define REG_WAKEUP3LD 0xA4
#define REG_WAKEUP3HD 0xAC
#define REG_WAKEUP4LD 0xB4
#define REG_WAKEUP5HD 0xBC
#define REG_CRC0      0xC4
#define REG_CRC1      0xC6
#define REG_CRC2      0xC8
#define REG_CRC3      0xCA
#define REG_CRC4      0xCC
#define REG_RMS       0xDA
#endif

#define REG_C_CR      0xE0
#define REG_RDSAR     0xE4
#define REG_ETTHR     0xEC
#define REG_FER       0xF0
#define REG_FEMR      0xF4
#define REG_FPSR      0xF8
#define REG_FFER      0xFC

#define IR_ROK   (1 << 0)  /* rx ok */
#define IR_RER   (1 << 1)  /* rx error */
#define IR_TOK   (1 << 2)  /* tx ok */
#define IR_TER   (1 << 3)  /* tx error */
#ifdef RTL_8139
#define IR_RBO   (1 << 4)  /* rx buffer overflow */
#endif
#ifdef RTL_8169
#define IR_RDU   (1 << 4) /* rx buffer overflow / rx desc unavailable */
#endif
#define IR_PUN   (1 << 5)  /* packet underrun */
#define IR_FOVW  (1 << 6)  /* rx fifo overflow */
#define IR_TDU   (1 << 7)  /* tx descriptor unavailable */
#define IR_SWINT (1 << 8)  /* software */
#ifdef RTL_8139
#define IR_LCHG  (1 << 13) /* cable length change */
#endif
#define IR_TOUT  (1 << 14) /* timeout */
#define IR_SERR  (1 << 15) /* system error */

#define RCR_AAP      (1 << 0) /* accept all packets (promiscuous) */
#define RCR_APM      (1 << 1) /* accept physical match */
#define RCR_AM       (1 << 2) /* accept multicast */
#define RCR_AB       (1 << 3) /* accept broadcast */
#define RCR_AR       (1 << 4) /* accept runt (< 64 bytes) */
#define RCR_AER      (1 << 5) /* accept error */
#define RCR_SEL      (1 << 6) /* eeprom type select (off: 9346, on: 9356) */
#ifdef RTL_8139
#define RCR_WRAP     (1 << 7) /* enable rx buf wrap mode */
#endif
#define RCR_MXDMA(n) (((n) & 0x7) << 8)  /* max dma burst size */
#ifdef RTL_8139
#define RCR_RBLEN(n) (((n) & 0x3) << 11) /* rx buffer length */
#endif
#define RCR_RXFTH(n) (((n) & 0x7) << 13) /* rx fifo threshold */
#define RCR_RER8     (1 << 16) /* rx with packet larger than 8 bytes */
#ifdef RTL_8139
#define RCR_MERINT   (1 << 17) /* multiple early interrupt select */
#define RCR_ERTH(n)  (((n) & 0xF) << 24) /* early rx threshold bits */
#endif
#ifdef RTL_8169
#define RCR_MERINT (1 << 24)
#endif

#define CR_BUFE (1 << 0) /* empty buffer */
#define CR_TE   (1 << 2) /* transmitter enable */
#define CR_RE   (1 << 3) /* receiver enable */
#define CR_RST  (1 << 4) /* reset */

#define PKT_ROK  (1 << 0)  /* receive ok */
#define PKT_FAE  (1 << 1)  /* frame alignment error */
#define PKT_CRC  (1 << 2)  /* CRC error */
#define PKT_LONG (1 << 3)  /* long packet (> 4k) */
#define PKT_RUNT (1 << 4)  /* runt packet (< 64 bytes) */
#define PKT_ISE  (1 << 5)  /* invalid symbol */
#define PKT_BAR  (1 << 13) /* broadcast address received */
#define PKT_PAM  (1 << 14) /* physical address matched */
#define PKT_MAR  (1 << 15) /* multicast address received */

#define TSD_SIZE(n) ((n & 0x1FF))        /* packet size */
#define TSD_OWN     (1 << 13)            /* DMA operation completed (desc available) */
#define TSD_TUN     (1 << 14)            /* tx fifo exhausted */
#define TSD_TOK     (1 << 15)            /* transmit ok */
#define TSD_ERTXTH(n) ((1 << 16) << (n)) /* early tx threshold */
#define TSD_NCC(n)    ((1 << 24) << (n)) /* collisions count */
#define TSD_CDH       (1 << 28)          /* cd heart beat */
#define TSD_OWC       (1 << 29)          /* out of window collision */
#define TSD_TABT      (1 << 30)          /* transmit abort */
#define TSD_CRS       (1 << 31)          /* carrier sense lost (ACAB nonobstant) */

#define CR_UNLOCK 0xC0 /* unlock config registers */
#define CR_LOCK   0x00 /* lock config registers */

/* TX command byte */
#define DESC_TX_OWN         (1 << 31) /* ownership (1 = ready for card to DMA) */
#define DESC_TX_EOR         (1 << 30) /* end of ring (last of descriptor) */
#define DESC_TX_FS          (1 << 29) /* first segment */
#define DESC_TX_LS          (1 << 28) /* last segment */
#define DESC_TX_LGSEN       (1 << 27) /* large send */
#define DESC_TX_IPCS        (1 << 18) /* ip checksum offload (LGSEN = 0) */
#define DESC_TX_UDPCS       (1 << 17) /* udp checksum offload (LGSEN = 0) */
#define DESC_TX_TCPCS       (1 << 16) /* tcp checksum offload (LGSEN = 0) */
#define DESC_TX_MSS(n)      (((n) & 0x3FF) << 16) /* mss (LGSEN = 1) */
#define DESC_TX_LEN(n)      ((n) & 0xFFFF) /* desc len */
/* TX vlan byte */
#define DESC_TX_TAGC        (1 << 17) /* vlang tag control enable bit */
#define DESC_TX_VLAN_TAG(n) ((n) & 0xFFFF) /* vlan tag */

/* RX command byte */
#define DESC_RX_OWN         (1 << 31) /* ownership (1 = ready for card to DMA) */
#define DESC_RX_EOR         (1 << 30) /* end of ring (last of descriptor) */
#define DESC_RX_FS          (1 << 29) /* first segment */
#define DESC_RX_LS          (1 << 28) /* last segment */
#define DESC_RX_MAR         (1 << 27) /* multicast packet */
#define DESC_RX_PAM         (1 << 26) /* physical address match */
#define DESC_RX_BAR         (1 << 25) /* broadcast address */
#define DESC_RX_BOVF        (1 << 24) /* buffer overflow */
#define DESC_RX_FOVF        (1 << 23) /* fifo overflow */
#define DESC_RX_RWT         (1 << 22) /* watchdog timer */
#define DESC_RX_RES         (1 << 21) /* receive error sum */
#define DESC_RX_RUNT        (1 << 20) /* runt packet (< 64 bytes) */
#define DESC_RX_CRC         (1 << 19) /* crc error */
#define DESC_RX_PID(n)      (((n) >> 17) & 0x3) /* protocol id */
#define DESC_RX_IPF         (1 << 16) /* ip checksum failure */
#define DESC_RX_UDPF        (1 << 15) /* udp checksum failure */
#define DESC_RX_TCPF        (1 << 14) /* tcp checksum failure */
#define DESC_RX_LEN(n)      ((n) & 0x3FFF) /* desc len */
/* RX vlan byte */
#define DESC_RX_TAVA        (1 << 16) /* vlan tag available */
#define DESC_RX_VLAN_TAG(n) ((n) & 0xFFFF) /* vlan tag */

#define TPPOLL_HPQ    (1 << 7) /* high priority queue */
#define TPPOLL_NPQ    (1 << 6) /* normal priority queue */
#define TPPOLL_FSWINT (1 << 0) /* forced software interrupt */

#define TCR_MXDMA(n)   ((n) << 8)  /* max dma burst size */
#define TCR_CRC        (1 << 16)   /* append CRC */
#define TCR_LBK(n)     ((n) << 17) /* loopback test */
#define TCR_IFG2       (1 << 19)   /* interface gap 2 */
#define TCR_HWVERIDI1  (1 << 23)   /* hardware version id1 */
#define TCR_IFG01(n)   ((n) << 24) /* interframe gap */
#define TCR_HWVERID0(n) (((n) >> 26) & 0x1F) /* hardware version id0 */

#define RTL_RU8(rtl, reg) pci_ru8(&(rtl)->pci_map, reg)
#define RTL_RU16(rtl, reg) pci_ru16(&(rtl)->pci_map, reg)
#define RTL_RU32(rtl, reg) pci_ru32(&(rtl)->pci_map, reg)
#define RTL_WU8(rtl, reg, val) pci_wu8(&(rtl)->pci_map, reg, val)
#define RTL_WU16(rtl, reg, val) pci_wu16(&(rtl)->pci_map, reg, val)
#define RTL_WU32(rtl, reg, val) pci_wu32(&(rtl)->pci_map, reg, val)
#define RTL_LOCK(rtl) mutex_lock(&(rtl)->mutex)
#define RTL_UNLOCK(rtl) mutex_unlock(&(rtl)->mutex)

#endif
