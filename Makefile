CFLAGS := `pkg-config gtk+-3.0 gl gdk-x11-3.0 x11 --cflags`  -Wall -g
LDFLAGS := `pkg-config gtk+-3.0 gl gdk-x11-3.0 x11 --libs` -lXcomposite

all: glxdemo
	
glxdemo: main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
