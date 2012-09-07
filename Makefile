CXXFLAGS =	-O2 -g -Wall -fmessage-length=0

OBJS =		NetNinny.o

LIBS =

TARGET =	NetNinny

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
