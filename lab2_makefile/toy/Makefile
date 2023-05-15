TARGET = toy_system

OBJDIR = obj

SYSTEM = system
UI = ui
WEB = web_server

CC = gcc
CFLAGS = -Wall -g -I$(SYSTEM) -I$(UI) -I$(WEB)

OBJS = main.o system_server.o gui.o input.o web_server.o


.PHONY: clean

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $@ $^

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

system_server.o: $(SYSTEM)/system_server.c
	$(CC) $(CFLAGS) -c $<

gui.o: $(UI)/gui.c
	$(CC) $(CFLAGS) -c $<

input.o: $(UI)/input.c
	$(CC) $(CFLAGS) -c $<

web_server.o: $(WEB)/web_server.c
	$(CC) $(CFLAGS) -c $<

clean: 
	rm -rf *.o $(TARGET)
