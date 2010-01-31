#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define FREQUENCY 44100
#define SAMPLES 8192

#define DEBUG 0

#define DEFAULT_NUM_INSTRUMENTS 5
/* This is the number of beats every minute */
#define DEFAULT_TEMPO 30

/* All positions and durations are in term of beats */
#define LOWER_START_BEAT 0
#define UPPER_START_BEAT 6
#define LOWER_DURATION 2
#define UPPER_DURATION 10

#define MAX_VOLUME 127
#define MIN_VOLUME 0

/* This is a global flag to tell the program to wrap up */
/* All notes should fade out, and when the beat is over, the program should exit */
/* If this is set to -1, that means the next beat will be the last */
int lastBeat;

/* This structure represents a tone */
typedef struct Tone {
	char* name; /* This is a string name of the tone, like C# */
	float freq; /* This is an integer that is the frequency of the tone */
	int period; /* This is the period of the tone, in 'ticks' */
	float* sample; /* This is an array of length 'period' that is the value of this wave at each 'tick' */
} Tone;

/* This holds each instrument */
typedef struct Instrument {
	Tone* tone;
	long position; /* This is where in the note's wave we are measured in 'ticks' */

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
} Instrument;

/* This is the global registry of all Tones available */
typedef struct Tones {
	Tone* tone; /* Array of precomputed tones */
	int num; /* Number of precomputed tones in array */
} Tones;

/* This is the global registery of all instruments */
typedef struct Instruments {
	Instrument* instrument; /* Array of instruments */
	int num; /* Number of instruments in array */
} Instruments;

/* This is the struture that represents all data in this arrangement. */
typedef struct GlobalData {
	Instruments instruments; /* This references all the instruments playing in this piece. */
	Tones tones; /* This refrences all the tones those instruments can play. */
	long beat_position; /* This is where in a beat we've played to thus far, measured in 'ticks'*/
	long beat_length; /* This is how long each beat is, measured in 'ticks' */
} GlobalData;

float freqToFreqRate(float freq) {
	return ((2 * M_PI * freq) / FREQUENCY);
}

float freqToPeriod(float freq) {
	return (FREQUENCY / freq);
}

/* This function computes the data for a given tone */
void genTone(Tone* tone, float freq, char* name) {
	int i;
	float freqRate = freqToFreqRate(freq);

	tone->freq = freq;
	tone->name = name;
	tone->period = freqToPeriod(freq);

	tone->sample = malloc(sizeof(float) * tone->period);
	for (i=0; i<=tone->period; i++) {
		tone->sample[i] = sinf(i * freqRate);
	}
}

/* This function takes in the current instrument and makes the next tone for it to play */
void nextNote(Instrument* currentInstrument, Tones* tones) {
	currentInstrument->tone = tones->tone + (random() % tones->num);
	currentInstrument->start_beat = (random() % (UPPER_START_BEAT - LOWER_START_BEAT)) + LOWER_START_BEAT;
	currentInstrument->end_beat = currentInstrument->start_beat + (random() % (UPPER_DURATION - LOWER_DURATION)) + LOWER_DURATION;
	currentInstrument->volume = MAX_VOLUME;
	currentInstrument->position = 0;

	if (DEBUG) {
		printf("Generated Instrument:\nnote: %s\nfreq: %f\nstart_beat: %d\nend_beat: %d\n", currentInstrument->tone->name, currentInstrument->tone->freq, currentInstrument->start_beat, currentInstrument->end_beat);
	}
}

/* This function goes through the list of instruments and moves each one closer to now */
void nextBeat(Instruments* instruments) {
	int n;
	Instrument* currentInstrument;
	for (n=0; n < instruments->num; n++) {
		currentInstrument = instruments->instrument + n;
		currentInstrument->start_beat--;
		currentInstrument->end_beat--;
	}
	if (DEBUG) {
		puts("Next Beat");
	}
}

/* This function returns whether or not an instrument is active (Making a sound this beat) */
int isActive(Instrument* instr) {
	return (instr->start_beat <=0 && instr->end_beat>=0);
}

/* This function returns whether or not an instrument is finished playing */
int isFinished(Instrument* instr) {
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
float currentVolume(Instrument* currentInstrument, long currentPos, long totalPos) {
	if (currentInstrument->start_beat == 0) {
		/* We've just started on this beat, should be fading in */
		if (DEBUG) {
			puts("Fading In");
		}
		return fadeInVolume(currentPos, totalPos) * currentInstrument->volume;
	} else if (currentInstrument->end_beat == 0 || lastBeat == 1) {
		/* We're ending this beat, should be fading out */
		if (DEBUG) {
			puts("Fading Out");
		}
		return fadeOutVolume(currentPos, totalPos) * currentInstrument->volume;
	} else {
		return currentInstrument->volume;
	}
}

void populate(void* data, Uint8* stream, int len) {
	int n;
	int numActiveInstruments;
	int tempVolume;
	long tempTotal;
	GlobalData* globalData = (GlobalData*)data;
	Instrument* activeInstruments[globalData->instruments.num];
	Instrument* currentInstrument;

	/* Figure out the active instruments this beat */
	numActiveInstruments = 0;
	for(n=0; n < globalData->instruments.num; n++) {
		if (isActive(globalData->instruments.instrument + n)) {
			if (DEBUG) {
				puts("This is an Active Instrument");
			}
			activeInstruments[numActiveInstruments] = globalData->instruments.instrument + n;
			numActiveInstruments++;
		}
	}

	for (; len>0; len--) {
		tempTotal = 0;
		for (n=0; n<numActiveInstruments; n++) {
			currentInstrument = activeInstruments[n];
			tempTotal += (currentVolume(currentInstrument, globalData->beat_position, globalData->beat_length) / numActiveInstruments) * (currentInstrument->tone->sample[currentInstrument->position]);
			currentInstrument->position++;
			currentInstrument->position %= currentInstrument->tone->period;
		}
		*stream = (Sint8) (tempTotal);
		stream++;

		(globalData->beat_position)++;
		if (globalData->beat_position > globalData->beat_length) {
			if (lastBeat == 1) {
				/* We're done */
				/* Turn off the audio, clean up */
				SDL_PauseAudio(1);
				SDL_CloseAudio();

				exit(0);
			} else if (lastBeat == -1) {
				/* This is the last beat */
				lastBeat = 1;
			}
			/* This is the next beat */
			nextBeat(&(globalData->instruments));
			numActiveInstruments = 0;
			for (n=0; n < globalData->instruments.num; n++) {
				currentInstrument = globalData->instruments.instrument + n;
				if (isFinished(currentInstrument)) {
					if (DEBUG) {
						puts("This is a Finished Instrument");
					}
					nextNote(currentInstrument, &(globalData->tones));
				}
				if (isActive(currentInstrument)) {
					if (DEBUG) {
						puts("This is an Active Instrument");
					}
					activeInstruments[numActiveInstruments] = currentInstrument;
					numActiveInstruments++;
				}
			}
			if (numActiveInstruments == 0) {
				/* In this case we're about to play silence. Lame! */
				/* All we have to do is make one of the tones play now instead */
				globalData->instruments.instrument->end_beat -= globalData->instruments.instrument->start_beat;
				globalData->instruments.instrument->start_beat = 0;
				activeInstruments[0] = globalData->instruments.instrument;
				numActiveInstruments = 1;
			}
			globalData->beat_position = 0;
		}
	}
}

void help() {
	puts("-n NUM   Sets the number of instruments");
	puts("-t TEMPO Sets the tempo in beats per minute");
}

/* This function is called when we get an interrupt */
void stop(int signal) {
	lastBeat = -1;
}


int main(int argc, char* argv[]) {
	GlobalData data;
	int i;
	SDL_AudioSpec spec;
	char flag;
	int tempo = DEFAULT_TEMPO;

	lastBeat=0;

	signal(SIGINT, &stop);

	spec.freq = FREQUENCY;
	spec.format = AUDIO_S8;
	spec.channels = 1;
	spec.samples = SAMPLES;
	spec.callback = (*populate);
	spec.userdata = (&data);

	data.instruments.num = DEFAULT_NUM_INSTRUMENTS;

	while ((flag = getopt(argc, argv, "ht:n:")) != -1) {
		switch (flag) {
			case 't':
				tempo = strtol(optarg, NULL, 10);
				break;
			case 'n':
				data.instruments.num = strtol(optarg, NULL, 10);
				break;
			case 'h':
			case '?':
			default:
				help();
				return 0;
				break;
		}
	}

	if (tempo <= 0) {
		fputs("Tempo must be a postitive integer\n", stderr);
		return 1;
	}

	if (data.instruments.num <= 0) {
		fputs("There must be at least one instrument\n", stderr);
		return 1;
	}

	if (SDL_OpenAudio(&spec, NULL) < 0) {
		fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
		exit(1);
	}
	srandom(time(NULL));

	/* Generate all the tones */
	data.tones.num = 37;
	data.tones.tone = malloc(sizeof(Tone) * data.tones.num);
	genTone(data.tones.tone + 0, 65.41, "C2");
	genTone(data.tones.tone + 1, 69.30, "C#2/Db2");
	genTone(data.tones.tone + 2, 73.42, "D2");
	genTone(data.tones.tone + 3, 77.78, "D#2/Eb2");
	genTone(data.tones.tone + 4, 82.41, "E2");
	genTone(data.tones.tone + 5, 87.31, "F2");
	genTone(data.tones.tone + 6, 92.50, "F#2/Gb2");
	genTone(data.tones.tone + 7, 98.00, "G2");
	genTone(data.tones.tone + 8, 103.83, "G#2/Ab2");
	genTone(data.tones.tone + 9, 110.00, "A2");
	genTone(data.tones.tone + 10, 116.54, "A#2/Bb2");
	genTone(data.tones.tone + 11, 123.47, "B2");
	genTone(data.tones.tone + 12, 130.81, "C3");
	genTone(data.tones.tone + 13, 138.59, "C#3/Db3");
	genTone(data.tones.tone + 14, 146.83, "D3");
	genTone(data.tones.tone + 15, 155.56, "D#3/Eb3");
	genTone(data.tones.tone + 16, 164.81, "E3");
	genTone(data.tones.tone + 17, 174.61, "F3");
	genTone(data.tones.tone + 18, 185.00, "F#3/Gb3");
	genTone(data.tones.tone + 19, 196.00, "G3");
	genTone(data.tones.tone + 20, 207.65, "G#3/Ab3");
	genTone(data.tones.tone + 21, 220.00, "A3");
	genTone(data.tones.tone + 22, 233.08, "A#3/Bb3");
	genTone(data.tones.tone + 23, 246.94, "B3");
	genTone(data.tones.tone + 24, 261.63, "C4");
	genTone(data.tones.tone + 25, 277.18, "C#4/Db4");
	genTone(data.tones.tone + 26, 293.66, "D4");
	genTone(data.tones.tone + 27, 311.13, "D#4/Eb4");
	genTone(data.tones.tone + 28, 329.63, "E4");
	genTone(data.tones.tone + 29, 349.23, "F4");
	genTone(data.tones.tone + 30, 369.99, "F#4/Gb4");
	genTone(data.tones.tone + 31, 392.00, "G4");
	genTone(data.tones.tone + 32, 415.30, "G#4/Ab4");
	genTone(data.tones.tone + 33, 440.00, "A4");
	genTone(data.tones.tone + 34, 466.16, "A#4/Bb4");
	genTone(data.tones.tone + 35, 493.88, "B4");
	genTone(data.tones.tone + 36, 523.25, "C5");

	/* Generate all the instruments */
	data.instruments.instrument = malloc(sizeof(Instrument) * data.instruments.num);
	/* Generate Each Instrument */
	for(i=0; i<data.instruments.num; i++) {
		nextNote(data.instruments.instrument + i, &(data.tones));
	}

	/* Set myself to the beginnning of the beat */
	data.beat_position = 0;
	/* Compute the length of a beat at this tempo */
	data.beat_length = FREQUENCY / (tempo / 60.0);


	SDL_PauseAudio(0);
	/* Sit here until we're terminated */
	while(1);
}
