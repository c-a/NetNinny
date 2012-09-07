CXX = g++

CXXFLAGS =	-O2 -g -Wall
LIBS =  -lnsl -lsocket -lresolv

OBJS =		NetNinny.o

TARGET =	NetNinny

SOURCES = NetNinnyProxy.cpp NetNinnyProxy.h NetNinny.cpp

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

NetNinny.o: $(SOURCES)
	$(CXX) -c $(CXXFLAGS) NetNinny.cpp
	
all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
