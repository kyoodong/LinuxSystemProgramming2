ssu_mntr: clear daemon main.o prompt.o core.o test
	gcc main.o prompt.o core.o -o ssu_mntr -lpthread

main.o: main.c
	gcc -c main.c -o main.o

prompt.o: prompt.c 
	gcc -c prompt.c -o prompt.o

core.o: core.c
	gcc -c core.c -o core.o -lpthread

debug: clear main-debug.o prompt-debug.o core-debug.o test 
	gcc -g main-debug.o prompt-debug.o core-debug.o -o ssu_mntr_debug -lpthread
	gdb ssu_mntr_debug

ddebug: clear test
	gcc -g daemon.c -o mdebug
	gdb mdebug

main-debug.o: main.c
	gcc -g -c main.c -o main-debug.o

prompt-debug.o: prompt.c 
	gcc -g -c prompt.c -o prompt-debug.o

core-debug.o: core.c
	gcc -g -c core.c -o core-debug.o -lpthread

clear: 
	rm -f *.o ssu_mntr
	rm -f mdaemon
	rm -f mdebug
	rm -f log.txt
	rm -rf trash

kill:
	killall -9 mdaemon mdebug

daemon:
	gcc daemon.c -o mdaemon

test:
	rm -rf dir
	mkdir -p dir
	touch dir/1.c
	echo 1.cfehwjkhfaskdfhjgkqawejhfagskdjhgfkewjshahjkcdbahjksbk > dir/1.c
	touch dir/2.c
	echo 2.cfwqjkhekfjqwhelkajndcnaksjlqawhekjfhaskjdhflas > dir/2.c
	touch dir/3.c
	echo 3.cfqwkljehfkaljsdhflkasehfquweolfiqhjklahsdklfjhasjdkfhlas > dir/3.c
	mkdir -p dir/dir1
	touch dir/dir1/1.c
	touch dir/dir1/2.c
	mkdir -p dir/dir1/dir2
	touch dir/dir1/dir2/1.c
	mkdir dir/dir_empty

show:
	echo "<< trash file list >>"
	ls trash/files
	cat trash/files/*
	
	echo "<< trash info list >>"
	ls trash/info
	cat trash/info/*

log:
	tail -150 /var/log/syslog | grep "SSUMonitor"
