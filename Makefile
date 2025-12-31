
ALL: main

main: main.o element.o workitem.o arena.o funcel.o funcimpl.o buddy.o
	clang++ -g -I. -Wdangling -Wall -W -std=c++20 -O2 -o $@ $^

%.o: %.cpp
	clang++ -g -I. -Wdangling -Wall -W -std=c++20 -O2 -c -o $@ $<

include Makefile.deps
