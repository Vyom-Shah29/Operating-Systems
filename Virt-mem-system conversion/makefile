CC      = gcc
CFLAGS  = -std=c11 -Wall -O2
TARGET  = virtmem
SRCS    = virtmem.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)