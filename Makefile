.PHONY: clean test

chords: chords.c
	$(CC) -o chords `sdl-config --cflags --libs` chords.c

test: chords
	./chords

clean:
	$(RM) chords
