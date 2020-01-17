fbkeyboard: fbkeyboard.c
	gcc -o fbkeyboard $(shell freetype-config --cflags) $(CPPFLAGS) $(CFLAGS) fbkeyboard.c $(LDFLAGS) $(shell freetype-config --libs)

clean:
	rm -f fbkeyboard

