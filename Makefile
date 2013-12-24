CC = i586-mingw32msvc-gcc
CFLAGS = -Wall -DUNICODE -mwindows
OBJS = fswanderer
all: $(OBJS)

clean:
	$(RM) $(OBJS)
