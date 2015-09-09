# export V8_VERSION=3.27.0

CC			= g++
INC			= -I/usr/local/v8-$(V8_VERSION)/include
LIBS		= -L/usr/local/v8-$(V8_VERSION)/lib64 -l:libv8.so.$(V8_VERSION)
CFLAGS		= -fpic -g -Wall
TARGET		= v8monoctx
TARGET_LIB	= lib$(TARGET).$(V8_VERSION).so
TARGET_DIR	= /usr/local/lib64

all: $(TARGET)

$(TARGET):
	$(CC) -c $(CFLAGS) $(INC) $(TARGET).cpp
	$(CC) -shared $(LIBS) -o $(TARGET_LIB) $(TARGET).o

install:
	install -m 0755 $(TARGET_LIB) $(TARGET_DIR)
	install -m 0644 -T $(TARGET).h /usr/local/include/$(TARGET).h
	echo "/usr/local/lib64" > /etc/ld.so.conf.d/$(TARGET).conf
	ldconfig -n $(TARGET_DIR)

clean:
	rm -rf *.o *.so
