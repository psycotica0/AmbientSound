#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FREQUENCY 44100
#define SAMPLES 8192

#define DEBUG 0

#define NUM_INSTRUMENTS 3
#define LOWER_DURATION FREQUENCY*2
#define UPPER_DURATION FREQUENCY*7
#define LOWER_POSITION 0
#define UPPER_POSITION -FREQUENCY*6
#define MAX_VOLUME 127
#define MIN_VOLUME 0

/* This holds each instrument */
typedef struct instrument {
	float freqRate; /* This represents the frequency, it is what the position means. 2*PI*(freq/sampleFreq) */
	long duration; /* This is the point at which an instrument will change */
	long position; /* This is where in the note we are */
	int volume; /* This is what the volume of the note is */
} instrument;

float freqToFreqRate(float freq) {
	return ((2 * M_PI * freq) / FREQUENCY);
}

/* This function takes in the current instrument and makes the next tone for it to play */
void generateTone(instrument* currentInstrument) {
	const static float freqs[] = {103.83, 110.0, 116.54, 123.47, 130.81, 146.83, 164.81, 174.61, 196.0, 220.0, 233.08, 246.94};
	const static int numFreqs = sizeof(freqs) / sizeof(float);
	const static int durationMod = (UPPER_DURATION-LOWER_DURATION)>0? (UPPER_DURATION-LOWER_DURATION): -(UPPER_DURATION-LOWER_DURATION);
	const static int durationSign = (UPPER_DURATION-LOWER_DURATION)>0? 1: -1;
	const static int positionMod = (UPPER_POSITION-LOWER_POSITION)>0? (UPPER_POSITION-LOWER_POSITION): -(UPPER_POSITION-LOWER_POSITION);
	const static int positionSign = (UPPER_POSITION-LOWER_POSITION)>0? 1: -1;

	currentInstrument->duration = durationSign * (random() % durationMod) + LOWER_DURATION;
	currentInstrument->position = positionSign * (random() % positionMod) + LOWER_POSITION;
	currentInstrument->volume = MAX_VOLUME;
	/* Generate Random Frequency */
	currentInstrument->freqRate = freqToFreqRate(freqs[random() % numFreqs]);

	if (DEBUG) {
		printf("Generated Instrument\nDuration: %d\nPosition: %d\nfreq%f\nfreqRate%f\n-------\n", currentInstrument->duration/FREQUENCY, currentInstrument->position/FREQUENCY, (FREQUENCY*currentInstrument->freqRate)/(2*M_PI),currentInstrument->freqRate);
	}
}

void populate(void* data, Uint8* stream, int len) {
	int i, n;
	int activeInstruments;
	long tempTotal;
	instrument* currentInstrument;
	instrument* instruments = (instrument*)data;
	for(i=0; i<len; i++) {
		tempTotal = 0;
		activeInstruments = 0;
		/* Total up each instrument */
		for(n=0; n<NUM_INSTRUMENTS; n++) {
			currentInstrument = instruments + n;
			if (currentInstrument->position > 0) {
				/* This one is currently making sound */
				tempTotal += currentInstrument->volume * sinf(currentInstrument->freqRate * currentInstrument->position);
				activeInstruments++;
			}
			currentInstrument->position++;

			if (currentInstrument->position >= currentInstrument->duration) {
				generateTone(currentInstrument);
			}
		}
		/* Take the average of the active instruments */
		if (activeInstruments > 0) {
			stream[i] = (Uint8) ((tempTotal / activeInstruments) + 127);
		} else {
			stream[i] = 127;
		}
	}
}

int main(int argc, char* argv[]) {
	int i;
	instrument instruments[NUM_INSTRUMENTS];
	SDL_AudioSpec spec;

	spec.freq = FREQUENCY;
	spec.format = AUDIO_U8;
	spec.channels = 1;
	spec.samples = SAMPLES;
	spec.callback = (*populate);
	spec.userdata = instruments;

	if (SDL_OpenAudio(&spec, NULL) < 0) {
		fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
		exit(1);
	}
	srandom(time());

	/* Set each instrument to be generated */
	for(i=0; i<NUM_INSTRUMENTS; i++) {
		instruments[i].duration = 0;
		instruments[i].position = 0;
	}

	SDL_PauseAudio(0);
	sleep(20);
	SDL_PauseAudio(1);
	SDL_CloseAudio();

	return 0;
}
