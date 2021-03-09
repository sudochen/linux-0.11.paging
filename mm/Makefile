
include ../Makefile.head

LDFLAGS += -r
CFLAGS	+= -I../include
CPP	+= -I../include

OBJS	= memory.o page.o

all: mm.o

mm.o: $(OBJS)
	$(Q)$(LD) $(LDFLAGS) -o mm.o $(OBJS)

clean:
	$(Q)rm -f core *.o *.a tmp_make
	$(Q)for i in *.c;do rm -f `basename $$i .c`.s;done

### Dependencies:
memory.o: memory.c ../include/signal.h ../include/sys/types.h \
 ../include/asm/system.h ../include/linux/sched.h ../include/linux/head.h \
 ../include/linux/fs.h ../include/linux/mm.h ../include/linux/kernel.h
