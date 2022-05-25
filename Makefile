#makefile for assignment 3

CC = gcc
CFLAGS = -g -Wall
LDFLAGS = -lpthread
OBJFILES = list.o lets-talk.o
TARGET = lets-talk

all: 	$(TARGET)
		
$(TARGET): 	$(OBJFILES)
		$(CC) $(CFLAGS) -o $(TARGET) $(OBJFILES) $(LDFLAGS)
clean:
	$(RM) *.o $(TARGET)
