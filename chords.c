#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <math.h>

void populate(void* data, Uint8* stream, int len) {
	int i;
	for(i = 0; i < len; stream[i++] = 0);
}

int main(int argc, char* argv[]) {
	SDL_AudioSpec spec;

	spec.freq = 44100;
	spec.format = AUDIO_U8;
	spec.channels = 1;
	spec.samples = 8192;
	spec.callback = (*populate);
	spec.userdata = NULL;

	if (SDL_OpenAudio(&spec, NULL) < 0) {
		fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
		exit(1);
	}

	SDL_PauseAudio(0);
	sleep(2);
	SDL_PauseAudio(1);
	SDL_CloseAudio();

	return 0;
}
