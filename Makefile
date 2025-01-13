
ALL: element.o workitem.o

element.o: element.h
workitem.o: workitem.h element.h

main: main.o element.o workitem.o
	clang++ -I. -Wdangling -Wall -W -std=c++20 -O2 -o $@ $^

%.o: %.cpp
	clang++ -I. -Wdangling -Wall -W -std=c++20 -O2 -c -o $@ $<

include Makefile.deps
