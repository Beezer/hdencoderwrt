LDFLAGS=-lpthread
hdpvrd: hdpvrd.o
	$(CC) $(LDFLAGS) hdpvrd.o -o hdpvrd
hdpvrd.o: hdpvrd.c
	$(CC) $(CFLAGS) -c hdpvrd.c

# remove object files and executable when user executes "make clean"
clean:
	rm *.o hdpvrd
