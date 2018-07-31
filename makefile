CFLAGS=-Wextra -Wall -Wno-unused -pedantic -std=c99 -g3
#CFLAGS=-Wextra -Wall -Wno-unused -pedantic -std=c99 -DNDEBUG -O3
#CFLAGS=-Wextra -Wall -Wno-unused -pedantic -std=c99 -DNDEBUG -g3 -pg -fprofile-arcs -ftest-coverage -static


qbce-prepro: error.h qbce-prepro.h parse.c parse.h pcnf.h mem.c stack.h qbce-prepro.c
	$(CC) $(CFLAGS) parse.c qbce-prepro.c mem.c -o qbce-prepro

clean:
	rm -f *.gcno *.gcda *.gcov *~ gmon.out qbce-prepro
