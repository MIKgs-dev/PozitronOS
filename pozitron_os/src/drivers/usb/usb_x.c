#include "drivers/usb/compat.h"
#include <drivers/pci.h>
#include <drivers/timer.h>
#include <drivers/usb/usb.h>
#include <drivers/usb/scsi_cmds.h>
#include <drivers/usb/usb_scsi_low.h>

struct usbdisk_info_t {
        struct controller *ctrl;
        uint16_t heads;
        uint16_t cylinders;
        uint16_t sectors_per_track;
        uint8_t  model_number[41];
        uint8_t  slave;
        uint64_t sectors;
        int  address_mode;
#define ADDRESS_MODE_CHS    0
#define ADDRESS_MODE_LBA    1
#define ADDRESS_MODE_LBA48  2
#define ADDRESS_MODE_PACKET 3
        uint32_t hw_sector_size;
        unsigned drive_exists : 1;
        unsigned slave_absent : 1;
        unsigned removable : 1;
	
	unsigned char usb_device_address;	
};

struct usbdisk_info_t usbdisk_info;

#define TEST 0

#if TEST==1
typedef struct partition_entry {
        uchar boot_flag;

        uchar chs[7];

        unsigned int lba_start;
        unsigned int lba_len;
} __attribute__ ((packed)) partition_entry_t;

typedef struct partition {
        char loader[446];
        partition_entry_t entry[4];
        char sig[2];
} __attribute__ ((packed)) partition_t;
#endif

int usb_probe(int drive)
{
	struct usbdisk_info_t *info = &usbdisk_info;
#if TEST==1
	partition_t part;
	unsigned char sense_data[32];
#endif
        int i,res;
        int error_count=100;

	printf("LinuxLabs USB bootloader\n");

//	outb( 0x30, 0x70);	// reset primary boot
//	outb( 0xff, 0x71);
	init_devices();
	hci_init();

	info->usb_device_address = 0;
        // find first usb device

        while(error_count && (res = poll_usb()))        // keep polling usb until no more devices are enumerated
                if(res<0)
                        if(!--error_count)
                                printf("There is a USB device, but it won't init! This is a bad thing.\n");

        for(i=0; i< next_usb_dev ; i++) {
                if(usb_device[i].class == 0x08 && usb_device[i].subclass == 0x06 && usb_device[i].protocol == 0x50) {
                        printf("Found USB block device %d\n", i);
			if(drive==0) {
                        	info->usb_device_address = i;
				break;
			}
			drive--;
                }
        }
	
	if(info->usb_device_address == 0) return -1;

	UnitReady(info->usb_device_address);

#if TEST==1
//Test
        printf("Requesting initial sense data\n");
        request_sense( info->usb_device_address, sense_data, 32);
        PrintSense(sense_data, 32);

        res = ll_read_block(info->usb_device_address, (uint8_t *)&part, 0, 1);

        printf("ll_read_block returns %d\n", res);

        res=-1;

	debug("part address (phy) = %x, (virt) = %x\n", (uint32_t) virt_to_phys(&part), (uint32_t)&part);

        for(i=0; i<4; i++) {
                printf("%d: boot=%02x, start=%08x length=%08x\n",i,  part.entry[i].boot_flag, part.entry[i].lba_start, part.entry[i]
.lba_len);
                }


#endif

	return 0;
}
int usb_read(int drive, uint64_t sectors, void *buffer)
{
        struct usbdisk_info_t *info = &usbdisk_info;
        int result;
	int blocknum = sectors;
	int i;
//	printf("sector= %d\t", blocknum);
	result = ll_read_block(info->usb_device_address, buffer,blocknum, 1);
#if 0
       for(i=0;i<128;i++) {
             if((i%4)==0) printf("\n %08x:",i*4);
             printf(" %08x ",(uint32_t)*((uint32_t *)buffer+i));
       }
#endif

        if(result!=512) return -1;	

	return 0;	
}