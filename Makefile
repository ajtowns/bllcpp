
ALL: main

main: main.o element.o workitem.o arena.o funcel.o funcimpl.o buddy.o execution.o func.o
	clang++ -g -I. -Wdangling -Wall -W -std=c++20 -O0 -o $@ $^

%.o: %.cpp
	clang++ -g -I. -Wdangling -Wall -W -std=c++20 -O0 -c -o $@ $<

include Makefile.deps
