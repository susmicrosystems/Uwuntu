include $(MAKEDIR)/env.mk

export KERNEL = yes

DIRS = ac97 \
       em \
       ext2fs \
       hda \
       iso9660 \
       ne2k \
       rtl \
       test \
       usb \
       virtio \
       virtio_blk \
       virtio_gpu \
       virtio_input \
       virtio_net \
       virtio_rng \

ifneq ($(filter $(ARCH), i386 amd64),)
DIRS+= ata \
       pcspkr \
       ps2
endif

include $(MAKEDIR)/dir.mk
