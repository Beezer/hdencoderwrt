
hdpvrd: hdpvrd.o
	$(CC) -lpthread hdpvrd.o -o hdpvrd
hdpvrd.o: hdpvrd.c
	$(CC) $(CFLAGS) -c hdpvrd.c

hdpvrdrtp: hdpvrdrtp.o
	$(CC) hdpvrdrtp.o -o hdpvrdrtp
hdpvrdrtp.o: hdpvrdrtp.c
	$(CC) $(CFLAGS) -c hdpvrdrtp.c

# remove object files and executable when user executes "make clean"
clean:
	rm *.o hdpvrd hdpvrdrtp
