
ALL: main

main: main.o element.o workitem.o arena.o funcel.o funcimpl.o buddy.o execution.o func.o crypto/sha256.o
	clang++ -g -I. -Wshadow -Wdangling -Wall -W -std=c++20 -O0 -o $@ $^

%.o: %.cpp
	clang++ -g -I. -Wshadow -Wdangling -Wall -W -std=c++20 -O0 -c -o $@ $<

include Makefile.deps
