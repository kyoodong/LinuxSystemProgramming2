ssu_mntr: main.o prompt.o core.o
	gcc main.o prompt.o core.o -o ssu_mntr

main.o: main.c
	gcc -c main.c -o main.o

prompt.o: prompt.c 
	gcc -c prompt.c -o prompt.o

core.o: core.c
	gcc -c core.c -o core.o

clear: 
	rm *.o ssu_mntr
