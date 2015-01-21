LDFLAGS=-lpthread
OBJECTS         = hdpvrd.o 

hdpvrd: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o hdpvrd
hdpvrd.o: hdpvrd.c
	$(CC) $(CFLAGS) -c hdpvrd.c

# remove object files and executable when user executes "make clean"
clean:
	rm *.o hdpvrd
