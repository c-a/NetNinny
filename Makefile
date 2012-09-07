CXX = g++

CXXFLAGS =	-O2 -g -Wall -fmessage-length=0
LDFLAGS = -lnsl -lsocket -lresolv  
LIBS =

OBJS =		NetNinny.o

TARGET =	NetNinny

SOURCES = NetNinny.cpp
	
$(TARGET):	$(OBJS)
	$(CXX) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LIBS)
	
NetNinny.o: $(SOURCES)
	$(CXX) -c $(CXXFLAGS) $(SOURCES)
	
all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
