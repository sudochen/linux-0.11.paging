# This is a basic Makefile for setting the general configuration
include Makefile.head

LDFLAGS	+= -Ttext 0 -e startup_32
CFLAGS	+= -Iinclude -Wall
CPP	+= -Iinclude

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, /dev/xxxx or empty, in which case the
# default of hd1(0301) is used by 'build'.
#
# ROOT_DEV= 0301	# hd1
# ROOT_DEV= 021d	# FLOPPY B

ROOT_DEV= 021d	# FLOPPY B

ARCHIVES=kernel/kernel.o mm/mm.o fs/fs.o
DRIVERS =kernel/blk_drv/blk_drv.a kernel/chr_drv/chr_drv.a
MATH	=kernel/math/math.a
LIBS	=lib/lib.a

all: Image
Image: boot/bootsect boot/setup kernel.bin FORCE
	$(BUILD) boot/bootsect boot/setup kernel.bin Image
	$(Q)rm -f kernel.bin
	$(Q)sync

init/main.o: FORCE
	$(Q)(cd init; make $(S) main.o)

boot/head.o: boot/head.s FORCE
	$(Q)(cd boot; make $(S) head.o)

kernel.bin: kernel.elf
	$(Q)cp -f kernel.elf kernel.tmp.elf
	$(Q)$(STRIP) kernel.tmp.elf
	$(Q)$(OBJCOPY) -O binary -R .note -R .comment kernel.tmp.elf kernel.bin
	$(Q)rm kernel.tmp.elf

kernel.elf: boot/head.o init/main.o \
	$(ARCHIVES) $(DRIVERS) $(MATH) $(LIBS) FORCE
	$(Q)$(LD) $(LDFLAGS) boot/head.o init/main.o \
	$(ARCHIVES) \
	$(DRIVERS) \
	$(MATH) \
	$(LIBS) \
	-o kernel.elf
	$(Q)nm kernel.elf | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)'| sort > kernel.map

kernel/math/math.a: FORCE
	$(Q)(cd kernel/math; make $(S))

kernel/blk_drv/blk_drv.a: FORCE
	$(Q)(cd kernel/blk_drv; make $(S))

kernel/chr_drv/chr_drv.a: FORCE
	$(Q)(cd kernel/chr_drv; make $(S))

kernel/kernel.o: FORCE
	$(Q)(cd kernel; make $(S))

mm/mm.o: FORCE
	$(Q)(cd mm; make $(S))

fs/fs.o: FORCE
	$(Q)(cd fs; make $(S))

lib/lib.a: FORCE
	$(Q)(cd lib; make $(S))

boot/setup: boot/setup.s FORCE
	$(Q)(cd boot; make $(S))

boot/bootsect: boot/bootsect.s kernel.bin FORCE
	$(Q)(cd boot; make $(S))

clean:
	$(Q)rm -f Image kernel.map tmp_make core boot/bootsect boot/setup
	$(Q)rm -f kernel.elf boot/*.o  typescript* info bochsout.txt
	$(Q)for i in init mm fs kernel lib boot; do (cd $$i; make $(S) clean); done

run: qemu

QEMU_OPS_T := -nographic -serial mon:stdio -m 64M -boot a
qemu:
	qemu-system-i386 ${QEMU_OPS_T} -fda Image  -hda ./rootfs/hdc-0.11.img 

QEMU_OPS_X := -m 64M -boot a
qemu-x:
	qemu-system-i386 ${QEMU_OPS_X} -fda Image  -hda ./rootfs/hdc-0.11.img 

bochs:
	bochs -f bochsrc

distclean: clean
	$(Q)rm -f tag* cscope* linux-0.11.*

FORCE: ;

.PHONE: Image kernel.elf kernel.bin
