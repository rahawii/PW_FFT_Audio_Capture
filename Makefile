CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags libpipewire-0.3)
LIBS = $(shell pkg-config --libs libpipewire-0.3) -lfftw3f -lm

TARGET = ac
SRC = audio-capture.c

all: $(TARGET)

$(TARGET): $(SRC) audio-capture.h
	$(CC) $(SRC) -o $(TARGET) $(CFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)
