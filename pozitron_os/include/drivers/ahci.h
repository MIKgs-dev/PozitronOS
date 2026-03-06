#ifndef AHCI_H
#define AHCI_H

#include <stdint.h>

#define AHCI_PCI_CLASS     0x01
#define AHCI_PCI_SUBCLASS  0x06
#define AHCI_PCI_PROG_IF   0x01

#define AHCI_MAX_PORTS     32
#define AHCI_MAX_CMDS      32
#define AHCI_SECTOR_SIZE   512

#define AHCI_ATAPI         0
#define AHCI_SEMB          1
#define AHCI_PM            2

#define AHCI_CMD_READ_DMA_EXT  0x25
#define AHCI_CMD_WRITE_DMA_EXT 0x35
#define AHCI_CMD_IDENTIFY      0xEC

// Global registers
#define AHCI_CAP            0x00
#define AHCI_GHC            0x04
#define AHCI_IS             0x08
#define AHCI_PI             0x0C
#define AHCI_VS             0x10
#define AHCI_CCC_CTL        0x14
#define AHCI_CCC_PORTS      0x18
#define AHCI_EM_LOC         0x1C
#define AHCI_EM_CTL         0x20
#define AHCI_CAP2           0x24
#define AHCI_BOHC           0x28

// Port registers
#define AHCI_PXCLB          0x00
#define AHCI_PXCLBU         0x04
#define AHCI_PXFB           0x08
#define AHCI_PXFBU          0x0C
#define AHCI_PXIS           0x10
#define AHCI_PXIE           0x14
#define AHCI_PXCMD          0x18
#define AHCI_PXTFD          0x20
#define AHCI_PXSIG          0x24
#define AHCI_PXSSTS         0x28
#define AHCI_PXSCTL         0x2C
#define AHCI_PXSERR         0x30
#define AHCI_PXSACT         0x34
#define AHCI_PXCI           0x38
#define AHCI_PXSNTF         0x3C
#define AHCI_PXFBS          0x40
#define AHCI_PXDEVSLP       0x44

// GHC bits
#define AHCI_GHC_HR         0x00000001
#define AHCI_GHC_IE         0x00000002
#define AHCI_GHC_MRSM       0x00000004
#define AHCI_GHC_AE         0x80000000

// PXCMD bits
#define AHCI_PXCMD_ST       0x0001
#define AHCI_PXCMD_SUD      0x0002
#define AHCI_PXCMD_POD      0x0004
#define AHCI_PXCMD_CLO      0x0008
#define AHCI_PXCMD_FRE      0x0010
#define AHCI_PXCMD_CCS_MASK 0x1F00
#define AHCI_PXCMD_CCS_SHIFT 8
#define AHCI_PXCMD_MPSS     0x2000
#define AHCI_PXCMD_FR       0x4000
#define AHCI_PXCMD_CR       0x8000
#define AHCI_PXCMD_CPD      0x10000
#define AHCI_PXCMD_ISP      0x20000
#define AHCI_PXCMD_HPCP     0x40000
#define AHCI_PXCMD_PMA      0x80000
#define AHCI_PXCMD_CPS      0x100000
#define AHCI_PXCMD_PCS      0x200000
#define AHCI_PXCMD_CCCS     0x400000
#define AHCI_PXCMD_ICC_MASK 0xF000000
#define AHCI_PXCMD_ICC_SHIFT 24
#define AHCI_PXCMD_ASP      0x20000000
#define AHCI_PXCMD_ALPE     0x40000000
#define AHCI_PXCMD_DLAE     0x80000000

// PXSSTS bits
#define AHCI_PXSSTS_DET_MASK    0x0F
#define AHCI_PXSSTS_DET_NODEV   0x00
#define AHCI_PXSSTS_DET_PRESENT 0x03
#define AHCI_PXSSTS_SPD_MASK    0xF0
#define AHCI_PXSSTS_SPD_SHIFT   4
#define AHCI_PXSSTS_IPM_MASK    0xF00
#define AHCI_PXSSTS_IPM_SHIFT   8

// PXSCTL bits
#define AHCI_PXSCTL_DET_MASK    0x0F
#define AHCI_PXSCTL_DET_NONE    0x00
#define AHCI_PXSCTL_DET_INIT    0x01
#define AHCI_PXSCTL_DET_DISABLE 0x04
#define AHCI_PXSCTL_IPM_MASK    0x0F0
#define AHCI_PXSCTL_IPM_SHIFT   4
#define AHCI_PXSCTL_IPM_NONE    0x00
#define AHCI_PXSCTL_IPM_PARTIAL 0x01
#define AHCI_PXSCTL_IPM_SLUMBER 0x02
#define AHCI_PXSCTL_IPM_DEVSLP  0x04
#define AHCI_PXSCTL_SPD_MASK    0xF00
#define AHCI_PXSCTL_SPD_SHIFT   8
#define AHCI_PXSCTL_ICC_MASK    0xF000
#define AHCI_PXSCTL_ICC_SHIFT   12
#define AHCI_PXSCTL_ASP         0x20000
#define AHCI_PXSCTL_ALPE        0x40000
#define AHCI_PXSCTL_DLAE        0x80000

// PXSERR bits
#define AHCI_PXSERR_IED         0x00000001
#define AHCI_PXSERR_IE          0x00000002
#define AHCI_PXSERR_PC          0x00000004
#define AHCI_PXSERR_DP          0x00000008
#define AHCI_PXSERR_TFE         0x00000010
#define AHCI_PXSERR_INF         0x00000020
#define AHCI_PXSERR_IPF         0x00000040
#define AHCI_PXSERR_M           0x00000100
#define AHCI_PXSERR_WBDS        0x00000200
#define AHCI_PXSERR_X            0x00000400
#define AHCI_PXSERR_NDF         0x00000800
#define AHCI_PXSERR_TRF         0x00001000
#define AHCI_PXSERR_PRX         0x00010000
#define AHCI_PXSERR_PTX         0x00020000
#define AHCI_PXSERR_DI          0x00040000
#define AHCI_PXSERR_E           0x00080000

// Command List entry (32 bytes)
typedef struct {
    uint16_t    cfl:5;      // Command FIS length
    uint16_t    a:1;        // ATAPI
    uint16_t    w:1;        // Write
    uint16_t    p:1;        // Prefetchable
    uint16_t    r:1;        // Reset
    uint16_t    b:1;        // BIST
    uint16_t    c:1;        // Clear busy upon R_OK
    uint16_t    rsv:1;
    uint16_t    pmp:4;      // Port Multiplier Port
    uint16_t    prdtl;      // Physical Region Descriptor Table Length
    uint32_t    prdbc;      // Physical Region Descriptor Byte Count
    uint32_t    ctba;       // Command Table Descriptor Base Address
    uint32_t    ctbau;      // Command Table Descriptor Base Address Upper
    uint32_t    rsv1[4];
} ahci_cmd_entry_t;

// PRDT entry (16 bytes)
typedef struct {
    uint32_t    dba;        // Data Base Address
    uint32_t    dbau;       // Data Base Address Upper
    uint32_t    rsv;
    uint32_t    dbc;        // Data Byte Count
} __attribute__((packed)) ahci_prdt_entry_t;

// Command Table (variable size)
typedef struct {
    uint8_t             cfis[64];           // Command FIS
    uint8_t             acmd[16];           // ATAPI command
    uint8_t             rsv[48];
    ahci_prdt_entry_t   prdt[];              // PRDT entries
} ahci_cmd_table_t;

// Port structure
typedef struct {
    uint32_t            clb;        // Command List Base Address
    uint32_t            clbu;       // Command List Base Address Upper
    uint32_t            fb;         // FIS Base Address
    uint32_t            fbu;        // FIS Base Address Upper
    uint32_t            is;         // Interrupt Status
    uint32_t            ie;         // Interrupt Enable
    uint32_t            cmd;        // Command
    uint32_t            rsv0;
    uint32_t            tfd;        // Task File Data
    uint32_t            sig;        // Signature
    uint32_t            ssts;       // Serial ATA Status
    uint32_t            sctl;       // Serial ATA Control
    uint32_t            serr;       // Serial ATA Error
    uint32_t            sact;       // Serial ATA Active
    uint32_t            ci;         // Command Issue
    uint32_t            sntf;       // Serial ATA Notification
    uint32_t            fbs;        // FIS-based Switching
    uint32_t            devslp;     // Device Sleep
    uint32_t            rsv1[10];
} __attribute__((packed)) ahci_port_regs_t;

// Private port data
typedef struct {
    uint8_t             present;
    uint8_t             atapi;
    uint32_t            sectors;
    uint32_t            max_cmds;
    ahci_cmd_entry_t*   cmd_list;
    uint32_t            cmd_list_phys;
    ahci_cmd_table_t*   cmd_tables[AHCI_MAX_CMDS];
    uint32_t            cmd_tables_phys[AHCI_MAX_CMDS];
} ahci_port_t;

// Main AHCI controller
typedef struct {
    uintptr_t           base;
    uint32_t            cap;
    uint32_t            cap2;
    uint32_t            ports_impl;
    uint32_t            ghc;
    uint8_t             num_ports;
    
    ahci_port_regs_t*   ports[AHCI_MAX_PORTS];
    ahci_port_t         port_data[AHCI_MAX_PORTS];
    
    uint8_t             pci_bus;
    uint8_t             pci_dev;
    uint8_t             pci_func;
    uint32_t            irq;
} ahci_controller_t;

int ahci_init(void);
int ahci_read_sectors(int port, uint32_t lba, uint32_t count, void* buffer);
int ahci_write_sectors(int port, uint32_t lba, uint32_t count, void* buffer);

extern ahci_controller_t* ahci_ctrl;
uint32_t ahci_port_read(ahci_controller_t* ctrl, int port, uint32_t reg);
void ahci_port_write(ahci_controller_t* ctrl, int port, uint32_t reg, uint32_t val);

#endif