.PHONY: clean

chords: chords.c
	$(CC) -o chords `sdl-config --cflags --libs` chords.c

clean:
	$(RM) chords
