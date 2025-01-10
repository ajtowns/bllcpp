
ALL: element.o workitem.o

element.o: element.h
workitem.o: workitem.h element.h

%.o: %.cpp
	clang++ -I. -Wdangling -Wall -W -std=c++20 -O2 -c -o $@ $<
