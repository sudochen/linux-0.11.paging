#
# Makefile for the FREAX-kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

include ../../Makefile.head

CFLAGS	+= -I../../include
CPP	+= -I../../include


OBJS  = math_emulate.o

math.a: $(OBJS)
	$(Q)$(AR) rcs math.a $(OBJS)
	$(Q)sync

clean:
	$(Q)rm -f core *.o *.a tmp_make
	$(Q)for i in *.c;do rm -f `basename $$i .c`.s;done

### Dependencies:
