include ../Makefile.head

LDFLAGS	+= -Ttext 0 -e _start

all: bootsect setup

bootsect: bootsect.s
	$(Q)echo "BUILD " $<
	$(Q)$(AS) $(AFLAGS) -o bootsect.o bootsect.s
	$(Q)$(LD) $(LDFLAGS) -o bootsect bootsect.o
	$(Q)cp -f bootsect bootsect.elf
	$(Q)nm bootsect.elf | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)'| sort > bootsect.map
	$(Q)$(OBJCOPY) -R .pdr -R .comment -R.note -S -O binary bootsect

setup: setup.s
	$(Q)echo "BUILD " $<
	$(Q)$(AS) $(AFLAGS) -o setup.o setup.s
	$(Q)$(LD) $(LDFLAGS) -o setup setup.o
	$(Q)cp -f setup setup.elf
	$(Q)nm setup.elf | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)'| sort > setup.map
	$(Q)$(OBJCOPY) -R .pdr -R .comment -R.note -S -O binary setup

head.o: head.s
	$(Q)echo "AS    " $<
	$(Q)$(AS) $(AFLAGS) -o head.o head.s

clean:
	$(Q)rm -f bootsect bootsect.o setup setup.o head.o *.sym *.map *.elf
