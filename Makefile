
build: main.o libso_stdio.so
	gcc main.c -o main -lso_stdio -L . -ggdb3


libso_stdio.so: so_stdio.o
	gcc -shared so_stdio.o -o libso_stdio.so


main.o: main.c
	gcc -fPIC -c main.c -ggdb3
so_stdio.o: so_stdio.c
	gcc -fPIC -c so_stdio.c -ggdb3

clean:
	rm -f *.o  main libso_stdio.so