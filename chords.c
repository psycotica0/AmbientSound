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
	const static float freqs[] = {
		65.41, /* C2 */
		69.30, /* C#2/Db2 */
		73.42, /* D2 */
		77.78, /* D#2/Eb2 */
		82.41, /* E2 */
		87.31, /* F2 */
		92.50, /* F#2/Gb2 */
		98.00, /* G2 */
		103.83, /* G#2/Ab2 */
		110.00, /* A2 */
		116.54, /* A#2/Bb2 */
		123.47, /* B2 */
		130.81, /* C3 */
		138.59, /* C#3/Db3 */
		146.83, /* D3 */
		155.56, /* D#3/Eb3 */
		164.81, /* E3 */
		174.61, /* F3 */
		185.00, /* F#3/Gb3 */
		196.00, /* G3 */
		207.65, /* G#3/Ab3 */
		220.00, /* A3 */
		233.08, /* A#3/Bb3 */
		246.94, /* B3 */
		261.63, /* C4 */
		277.18, /* C#4/Db4 */
		293.66, /* D4 */
		311.13, /* D#4/Eb4 */
		329.63, /* E4 */
		349.23, /* F4 */
		369.99, /* F#4/Gb4 */
		392.00, /* G4 */
		415.30, /* G#4/Ab4 */
		440.00, /* A4 */
		466.16, /* A#4/Bb4 */
		493.88, /* B4 */
		523.25, /* C5 */
	};
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
