#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FREQUENCY 44100
#define SAMPLES 8192

#define DEBUG 0

#define NUM_INSTRUMENTS 3
/* This is the number of beats every minute */
#define TEMPO 60

/* All positions and durations are in term of beats */
#define LOWER_START_BEAT 0
#define UPPER_START_BEAT 6
#define LOWER_DURATION 2
#define UPPER_DURATION 10

#define MAX_VOLUME 127
#define MIN_VOLUME 0

/* This holds each instrument */
typedef struct instrument {
	float freqRate; /* This represents the frequency, it is what the position means. 2*PI*(freq/sampleFreq) */
	long position; /* This is where in the note's wave we are measured in 'ticks' */
	int period; /* This is the period of the wave measured in 'ticks' */

	/* The arrangment of notes is done in terms of beats */
	/* At the end of a beat, the song doesn't really move forward a beat, it's just that every event moves closer a beat */
	/* So, if some event has a value of 2, that means that event will occur in 2 beats. */
	/* At the end of this beat, that number will be reduced to 1. This means next beat, the event will occur */
	/* At the end of the next beat the number will be reduced to 0, meaning this event is occuring this beat */
	/* After this beat it will again be reduced to -1, meaning the event occured last beat. */
	/* So, if start_beat is <=0 then the note is playing */
	/* If end_beat is <0, then this note is done, and another one should be generated. */
	int start_beat; /* This is when this instrument starts. A */
	int end_beat;

	int volume; /* This is what the volume of the note is */
} instrument;

float freqToFreqRate(float freq) {
	return ((2 * M_PI * freq) / FREQUENCY);
}

float freqToPeriod(float freq) {
	return (FREQUENCY / freq);
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
	float tempFreq;

	currentInstrument->start_beat = (random() % (UPPER_START_BEAT - LOWER_START_BEAT)) + LOWER_START_BEAT;
	currentInstrument->end_beat = currentInstrument->start_beat + (random() % (UPPER_DURATION - LOWER_DURATION)) + LOWER_DURATION;
	currentInstrument->volume = MAX_VOLUME;
	/* Generate Random Frequency and Period */
	tempFreq = freqs[random() % numFreqs];
	currentInstrument->freqRate = freqToFreqRate(tempFreq);
	currentInstrument->period = freqToPeriod(tempFreq);

	if (DEBUG) {
		printf("Generated Instrument:\nfreq: %f\nstart_beat: %d\nend_beat: %d\n", tempFreq, currentInstrument->start_beat, currentInstrument->end_beat);
	}
}

/* This function goes through the list of instruments and moves each one closer to now */
void nextBeat(instrument* instruments) {
	int n;
	instrument* currentInstrument;
	for (n=0; n<NUM_INSTRUMENTS; n++) {
		currentInstrument = instruments + n;
		currentInstrument->start_beat--;
		currentInstrument->end_beat--;
	}
	if (DEBUG) {
		puts("Next Beat");
	}
}

/* This function returns whether or not an instrument is active (Making a sound this beat) */
int isActive(instrument* instr) {
	return (instr->start_beat <=0 && instr->end_beat>=0);
}

/* This function returns whether or not an instrument is finished playing */
int isFinished(instrument* instr) {
	return (instr->end_beat<0);
}

/* These functions takes in the current beat position and the total beat length and returns the volume the tone should be at that point */
float fadeInVolume(long currentPos, long totalPos) {
	return (((float)currentPos) / totalPos);
}

float fadeOutVolume(long currentPos, long totalPos) {
	return 1 - (((float)currentPos) / totalPos);
}

/* This function returns the appropriate volume for the given instrument */
float currentVolume(instrument* currentInstrument, long currentPos, long totalPos) {
	if (currentInstrument->start_beat == 0) {
		/* We've just started on this beat, should be fading in */
		if (DEBUG) {
			puts("Fading In");
		}
		return fadeInVolume(currentPos, totalPos) * currentInstrument->volume;
	} else if (currentInstrument->end_beat == 0) {
		/* We're ending this beat, should be fading out */
		if (DEBUG) {
			puts("Fading Out");
		}
		return fadeOutVolume(currentPos, totalPos) * currentInstrument->volume;
	} else {
		return currentInstrument->volume;
	}
}

long beat_position; /* This is where we are in a given beat. */
long beat_length; /* This is how many 'ticks' a given beat is. */

void populate(void* data, Uint8* stream, int len) {
	int n;
	int numActiveInstruments;
	int tempVolume;
	long tempTotal;
	instrument* activeInstruments[NUM_INSTRUMENTS];
	instrument* currentInstrument;
	instrument* instruments = (instrument*)data;

	/* Figure out the active instruments this beat */
	numActiveInstruments = 0;
	for(n=0; n<NUM_INSTRUMENTS; n++) {
		if (isActive(instruments + n)) {
			if (DEBUG) {
				puts("This is an Active Instrument");
			}
			activeInstruments[numActiveInstruments] = instruments+n;
			numActiveInstruments++;
		}
	}

	for (; len>0; len--) {
		tempTotal = 0;
		for (n=0; n<numActiveInstruments; n++) {
			currentInstrument = activeInstruments[n];
			tempTotal += currentVolume(currentInstrument, beat_position, beat_length) * sinf(currentInstrument->freqRate * currentInstrument->position);
			currentInstrument->position++;
			currentInstrument->position %= currentInstrument->period;
		}
		if (numActiveInstruments > 0) {
			*stream = (Uint8) ((tempTotal / numActiveInstruments) + 127);
		} else {
			*stream = 127;
		}
		stream++;

		beat_position++;
		if (beat_position > beat_length) {
			/* This is the next beat */
			nextBeat(instruments);
			numActiveInstruments = 0;
			for (n=0; n<NUM_INSTRUMENTS; n++) {
				currentInstrument = instruments + n;
				if (isFinished(currentInstrument)) {
					if (DEBUG) {
						puts("This is a Finished Instrument");
					}
					generateTone(currentInstrument);
				}
				if (isActive(currentInstrument)) {
					if (DEBUG) {
						puts("This is an Active Instrument");
					}
					activeInstruments[numActiveInstruments] = currentInstrument;
					numActiveInstruments++;
				}
			}
			beat_position = 0;
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
	beat_position = 0;
	beat_length = FREQUENCY / (TEMPO / 60);

	/* Generate Each Instrument */
	for(i=0; i<NUM_INSTRUMENTS; i++) {
		generateTone(instruments + i);
	}

	SDL_PauseAudio(0);
	sleep(20);
	SDL_PauseAudio(1);
	SDL_CloseAudio();

	return 0;
}
