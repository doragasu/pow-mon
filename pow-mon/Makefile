TARGET=pow-mon
SRCS=$(wildcard *.c)
CC?=gcc
#CFLAGS=-O2 -Wall -march=armv7-a -mfloat-abi=hard -mfpu=vfpv3-d16 -O2 -pipe -fstack-protector-strong -fno-plt -ffunction-sections -I/opt/vc/include -D__DEBUG
CFLAGS=-O2 -Wall -march=armv7-a -mfloat-abi=hard -mfpu=vfpv3-d16 -O2 -pipe -fstack-protector-strong -fno-plt -ffunction-sections -I/opt/vc/include
LIBS=-L/opt/vc/lib -lbcm_host -Wl,-gc-sections

OBJDIR=obj
OBJS=$(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

# Main target
all: $(TARGET)

install: $(TARGET)
	install -D -m 755 pow-mon $(DESTDIR)/usr/bin/pow-mon
	install -D -m 755 power-up $(DESTDIR)/etc/pow-mon/power-up
	install -D -m 755 power-down $(DESTDIR)/etc/pow-mon/power-down
	install -D -m 644 pow-mon\@.service $(DESTDIR)/usr/lib/systemd/system/pow-mon\@.service


$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LIBS) -o $(TARGET)

# Build object and dependency files
$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) -c -MMD -MP $(CFLAGS) $< -o $@
               
$(OBJDIR):     
	mkdir -p $(OBJDIR)

# Delete intermediate files
.PHONY: clean
clean:
	rm -rf $(OBJDIR)

# Delete intermediate and output files
.PHONY: mrproper
mrproper: | clean
	rm -f $(TARGET)

# Include auto-generated dependencies
-include $(SRCS:%.c=$(OBJDIR)/%.d)

