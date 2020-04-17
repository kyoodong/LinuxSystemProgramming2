ssu_mntr: main.o prompt.o
	gcc main.o prompt.o -o ssu_mntr

main.o: main.c
	gcc -c main.c -o main.o

prompt.o: prompt.c 
	gcc -c prompt.c -o prompt.o

clear: 
	rm *.o ssu_mntr
