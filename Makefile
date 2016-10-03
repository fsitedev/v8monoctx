# export V8_VERSION=xxx
# export V8_PREFIX=xxx

VERSION     = 1.3
CC          = g++
INC         = -I$(V8_PREFIX)/include
LIBS        = -L$(V8_PREFIX)/lib64 -l:libv8.so.$(V8_VERSION)

CFLAGS      = -fpic -g -Wall
TARGET      = v8monoctx
TARGET_LIB  = lib$(TARGET).so.$(VERSION) 
TARGET_DIR  = /usr/local/lib64
INCLUDE_DIR = /usr/local/include
DESTDIR     =

all: $(TARGET)

$(TARGET):
	$(CC) -c $(CFLAGS) $(INC) $(TARGET).cpp
	$(CC) -shared -Wl,-soname,$(TARGET_LIB) $(LIBS) -o $(TARGET_LIB) $(TARGET).o

install:
	install -m 0755 -D -T $(TARGET_LIB) $(DESTDIR)$(TARGET_DIR)/$(TARGET_LIB)
	install -m 0644 -D -T $(TARGET).h $(DESTDIR)$(INCLUDE_DIR)/$(TARGET).h
	ln -fs $(TARGET_LIB) $(DESTDIR)$(TARGET_DIR)/lib$(TARGET).so
	mkdir -p $(DESTDIR)/etc/ld.so.conf.d
	echo "$(TARGET_DIR)" > $(DESTDIR)/etc/ld.so.conf.d/$(TARGET).conf

clean:
	rm -rf *.o *.so
