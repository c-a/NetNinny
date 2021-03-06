CXX = g++

CXXFLAGS =	-O2 -g -Wall
LIBS =

OBJS =		NetNinny.o NetNinnyProxy.o

TARGET =	NetNinny

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

NetNinny.o: NetNinny.cpp
	$(CXX) $(CXXFLAGS) -c NetNinny.cpp

NetNinnyProxy.o: NetNinnyProxy.cpp NetNinnyProxy.h
	$(CXX) $(CXXFLAGS) -c NetNinnyProxy.cpp

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
