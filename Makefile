ssu_mntr: clear main.o prompt.o core.o test 
	gcc main.o prompt.o core.o -o ssu_mntr -lpthread

main.o: main.c
	gcc -c main.c -o main.o

prompt.o: prompt.c 
	gcc -c prompt.c -o prompt.o

core.o: core.c
	gcc -c core.c -o core.o -lpthread

clear: 
	rm -f *.o ssu_mntr

test:
	mkdir -p dir
	touch dir/1.c
	echo 1.c > dir/1.c
	touch dir/2.c
	echo 2.c > dir/2.c
	touch dir/3.c
	echo 3.c > dir/3.c
	mkdir -p dir/dir1
	touch dir/dir1/1.c
	touch dir/dir1/2.c

show:
	echo "<< trash file list >>"
	ls trash/files
	cat trash/files/*
	
	echo "<< trash info list >>"
	ls trash/info
	cat trash/info/*
