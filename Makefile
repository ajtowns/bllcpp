
test: test.o
	clang++ -I. -Wdangling -Wall -W -std=c++20 -O2 -o $@ $^

ALL: main

main: main.o element.o workitem.o arena.o
	clang++ -I. -Wdangling -Wall -W -std=c++20 -O2 -o $@ $^

%.o: %.cpp
	clang++ -I. -Wdangling -Wall -W -std=c++20 -O2 -c -o $@ $<

include Makefile.deps
