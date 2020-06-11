
# C Compiler
CC = gcc
# C Compiler flags
CFLAGS = -std=gnu11 -g -Wall -Wextra -Wpedantic -Wshadow -pthread
#Target executable name 
TARGET = httpserver
#QueueTarget
QUEUE = requestQueue

all: $(TARGET)

$(TARGET): $(TARGET).o $(QUEUE).o
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).o $(QUEUE).o

$(TARGET).o: $(TARGET).c
	$(CC) $(CFLAGS) -c $(TARGET).c

$(QUEUE).o: $(QUEUE).c $(QUEUE).h
	gcc $(CFLAGS) -c $(QUEUE).c

clean:
	rm *.o $(TARGET) $(TARGET).o $(QUEUE).o

spotless:
	clean rm $(TARGET).exe


