#include <SDL/SDL.h>
#include <SDL/SDL_audio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

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
	int log; /* If this is true, log to stdout, in CSV format, all the sound data */
	int showcase; /* If this is true, the instruments play each tone in order for two beats */
	int lastShowcased; /* This is the last tone showcased */
} GlobalData;

int isActiveDuringInterval(Instrument*, int, int);

/* This function is called to exit from the program */
void cleanExit() {
	/* Turn off the audio, clean up */
	SDL_PauseAudio(1);
	SDL_CloseAudio();

	exit(0);
}

float freqToFreqRate(float freq) {
	return ((2 * M_PI * freq) / FREQUENCY);
}

float freqToPeriod(float freq) {
	return (FREQUENCY / freq);
}

/* This function populates the samples array with a sinusoidal waveform */
void sineWave(Tone* tone) {
	int i;
	float freqRate = freqToFreqRate(tone->freq);

	for (i=0; i<=tone->period; i++) {
		tone->sample[i] = sinf(i * freqRate);
	}
}

/* This function populates the samples array with a trianglular waveform */
void triangleWave(Tone* tone) {
	int i;
	float freqRate = 1.0 / (2.0 * tone->period);

	for (i=0; i <= (tone->period / 2); i++) {
		/* Symmetry! */
		tone->sample[i] = tone->sample[tone->period - i] = i * freqRate;
	}
}

/* This function populates the samples with a square wave */
void squareWave(Tone* tone) {
	int i;

	for(i=0; i<= (tone->period/4); i++) {
		/* Either side of the square */
		tone->sample[i] = tone->sample[tone->period -1] = 0;
	}
	for(; i <= 3 * (tone->period/4); i++) {
		/* Centre of the square */
		tone->sample[i] = 1;
	}
}

/* This function populates the samples with a sawtooth wave */
void sawtoothWave(Tone* tone) {
	int i;
	float freqRate = 1.0 / (2.0 * tone->period);

	for (i=0; i <= (tone->period / 2); i++) {
		tone->sample[i] =  i * freqRate;
		tone->sample[tone->period - i] = 0;
	}
}


/* This function computes the data for a given tone */
void genTone(Tone* tone, float freq, char* name, void (*toneFunc)(Tone*)) {
	tone->freq = freq;
	tone->name = name;
	tone->period = freqToPeriod(freq);

	tone->sample = malloc(sizeof(float) * tone->period);
	toneFunc(tone);
}

/* This function multiplies a note's probability array by the given array */
void multiplyProbability(int* currentArray, int arraySize, int noteToAdd) {
	int i;
	/* For a discussion of how this works, see the file toneProbability */
	static const int probArray[36] = {
		0,
		1,
		1,
		3,
		4,
		4,
		3,
		4,
		3,
		3,
		3,
		1,
		5,
		1,
		1,
		3,
		4,
		4,
		3,
		4,
		3,
		3,
		3,
		1,
		5,
		1,
		1,
		3,
		4,
		4,
		4,
		3,
		3,
		3,
		3,
		1,
	};
	for (i=0; i<arraySize; i++) {
		currentArray[(noteToAdd + i)%arraySize] *= probArray[i];
	}
}

/* This function uses the probability functions to choose a note to play for the given period */
int pickANote(GlobalData* globalData, Instrument* instrumentToPickNoteFor) {
	Instrument* activeInstrument[globalData->instruments.num];
	Instrument* currentInstrument;
	int totalArray;
	int randomNumber;
	int n;
	int probArray[36] = {
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
		1,
	};
	/* Build up the probability matrix for the interval */
	for (n=0; n < globalData->instruments.num; n++) {
		currentInstrument = (globalData->instruments.instrument + n);
		if ((currentInstrument != instrumentToPickNoteFor) && isActiveDuringInterval(currentInstrument, instrumentToPickNoteFor->start_beat, instrumentToPickNoteFor->end_beat)) {
			/* Figure out the number of this instrument and affect the prob matrix with it */
			multiplyProbability(probArray, 36, currentInstrument->tone - globalData->tones.tone);
		}
	}
	/* Find the total probability of the matrix */
	totalArray = 0;
	for (n=0; n < 36; n++) {
		totalArray += probArray[n];
	}
	/* Pick a number between 1 and the total of the probability matrix */
	randomNumber = (random() % (totalArray-1)) + 1;
	/* Now go through each item in the prob array subtracting its value from randomNumber */
	/* When randomNumber <= 0, that's the note we'll choose */
	for (n=0; n < 36; n++) {
		randomNumber -= probArray[n];
		if (randomNumber <= 0) {
			return n;
		}
	}
}

/* This function takes in the current instrument and makes the next tone for it to play */
void nextNote(GlobalData* globalData, Instrument* currentInstrument) {
	if (globalData->showcase) {
		currentInstrument->tone = globalData->tones.tone + globalData->lastShowcased;
		currentInstrument->start_beat = 0;
		currentInstrument->end_beat = 1;
		globalData->lastShowcased = (globalData->lastShowcased + 1) % globalData->tones.num;
	} else {
		currentInstrument->start_beat = (random() % (UPPER_START_BEAT - LOWER_START_BEAT)) + LOWER_START_BEAT;
		currentInstrument->end_beat = currentInstrument->start_beat + (random() % (UPPER_DURATION - LOWER_DURATION)) + LOWER_DURATION;
		currentInstrument->tone = globalData->tones.tone + pickANote(globalData, currentInstrument);
	}
	currentInstrument->volume = MAX_VOLUME;
	currentInstrument->position = 0;

	if (DEBUG) {
		printf("Generated Instrument:\nnote: %s\nfreq: %f\nstart_beat: %d\nend_beat: %d\n", currentInstrument->tone->name, currentInstrument->tone->freq, currentInstrument->start_beat, currentInstrument->end_beat);
	}
}

/* This function initializes an instrument to one beat of A 440 */
void initializeInstrument(GlobalData* globalData, Instrument* currentInstrument) {
	currentInstrument->tone = globalData->tones.tone + 33;
	currentInstrument->start_beat = 0;
	currentInstrument->end_beat = 1;
	currentInstrument->volume = MAX_VOLUME;
	currentInstrument->position = 0;
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

/* This function returns whether or not an instrument is active during a given interval */
int isActiveDuringInterval(Instrument* instr, int start_beat, int end_beat) {
	return (instr->start_beat <=start_beat && instr->end_beat>=start_beat) || ( start_beat<=instr->start_beat && end_beat>=instr->start_beat);
}
/* This is a shortcut that returns whether or not an instrument is active this beat */
int isActive(Instrument* instr) {
	return isActiveDuringInterval(instr, 0, 0);
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
		if (lastBeat == 1) {
			/* This is the last beat, don't bother coming in */
			return 0;
		} else {
			return fadeInVolume(currentPos, totalPos) * currentInstrument->volume;
		}
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

/* This function mixes the num instruments values and volumes and returns an int that is their mixed value */
Sint8 mixInstruments(int* volumes, float* values, int num, int log) {
	int totalVolume=0;
	Sint8 mixedTotal=0;
	int tempValue;
	int n;

	/* First find the total of the volumes */
	for(n=0; n<num; n++) {
		totalVolume += volumes[n];
	}
	/* Don't adjust up. If total is less than 127, don't adjust volumes */
	if (totalVolume < 127) {
		totalVolume = 127;
	}
	/* Then sum the adjusted volumes to the values */
	for(n=0; n<num; n++) {
		tempValue = (127.0 / totalVolume) * volumes[n] * values[n];
		if (log) {
			printf("%d,", tempValue);
		}
		mixedTotal += tempValue;
	}
	if (log) {
		printf("%d\n", mixedTotal);
	}
	return mixedTotal;
}

void populate(void* data, Uint8* stream, int len) {
	GlobalData* globalData = (GlobalData*)data;
	Instrument* currentInstrument;
	int instrumentVolume[globalData->instruments.num];
	float instrumentValue[globalData->instruments.num];
	int n;

	for (; len>0; len--) {
		for (n=0; n < globalData->instruments.num; n++) {
			currentInstrument = globalData->instruments.instrument + n;
			if (isActive(currentInstrument)) {
				instrumentVolume[n] = currentVolume(currentInstrument, globalData->beat_position, globalData->beat_length);
				instrumentValue[n] =  currentInstrument->tone->sample[currentInstrument->position];
				/* Now, move up the position of the instrument, bounded by the period of its tone */
				currentInstrument->position = (currentInstrument->position + 1) % (currentInstrument->tone->period);
			} else {
				instrumentVolume[n] = 0;
				instrumentValue[n] = 0;
			}
		}
		*stream = mixInstruments(instrumentVolume, instrumentValue, globalData->instruments.num, globalData->log);
		stream++;

		(globalData->beat_position)++;
		if (globalData->beat_position > globalData->beat_length) {
			if (lastBeat == 1) {
				/* We're done */
				cleanExit();
			} else if (lastBeat == -1) {
				/* This is the last beat */
				lastBeat = 1;
			}
			/* This is the next beat */
			nextBeat(&(globalData->instruments));
			{
				/* This temporarily stores the number of active instruments in the next beat */
				/* It's used to make sure there aren't periods of silence */
				int numActiveInstruments = 0;
				for (n=0; n < globalData->instruments.num; n++) {
					currentInstrument = globalData->instruments.instrument + n;
					if (isFinished(currentInstrument)) {
						if (DEBUG) {
							puts("This is a Finished Instrument");
						}
						nextNote(globalData, currentInstrument);
					}
					if (isActive(currentInstrument)) {
						if (DEBUG) {
							puts("This is an Active Instrument");
						}
						numActiveInstruments++;
					}
				}
				if (numActiveInstruments == 0) {
					/* In this case we're about to play silence. Lame! */
					/* All we have to do is make one of the tones play now instead */
					globalData->instruments.instrument->end_beat -= globalData->instruments.instrument->start_beat;
					globalData->instruments.instrument->start_beat = 0;
				}
			}
			globalData->beat_position = 0;
		}
	}
}

void help() {
	puts("-n NUM   Sets the number of instruments");
	puts("-t TEMPO Sets the tempo in beats per minute");
	puts("-w TYPE  Sets the waveform of the notes to TYPE");
	puts("         Accepted Types are: sine, triangle, square, sawtooth");
	puts("-l       If this is set, then log all sound data in CSV format to stdout");
	puts("-s       If this flag is set then the app goes into showcase mode where one instrument goes through all the tones in order");
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
	void (*waveform)(Tone*) = &sineWave;

	data.log = 0;
	data.showcase = 0;
	data.lastShowcased = 0;
	lastBeat=0;

	signal(SIGINT, &stop);

	spec.freq = FREQUENCY;
	spec.format = AUDIO_S8;
	spec.channels = 1;
	spec.samples = SAMPLES;
	spec.callback = (*populate);
	spec.userdata = (&data);

	data.instruments.num = DEFAULT_NUM_INSTRUMENTS;

	while ((flag = getopt(argc, argv, "slht:n:w:")) != -1) {
		switch (flag) {
			case 't':
				tempo = strtol(optarg, NULL, 10);
				break;
			case 'n':
				data.instruments.num = strtol(optarg, NULL, 10);
				break;
			case 'l':
				data.log = 1;
				break;
			case 's':
				data.showcase = 1;
				data.instruments.num = 1;
				break;
			case 'w':
				if (strcmp(optarg, "sine") == 0) {
					waveform = &sineWave;
				} else if (strcmp(optarg, "triangle") == 0) {
					waveform = &triangleWave;
				} else if (strcmp(optarg, "square") == 0) {
					waveform = &squareWave;
				} else if (strcmp(optarg, "sawtooth") == 0) {
					waveform = &sawtoothWave;
				} else {
					printf("Unrecognized Waveform: %s\n", optarg);
					return 1;
				}
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
	data.tones.num = 36;
	data.tones.tone = malloc(sizeof(Tone) * data.tones.num);
	genTone(data.tones.tone + 0, 65.41, "C2", waveform);
	genTone(data.tones.tone + 1, 69.30, "C#2/Db2", waveform);
	genTone(data.tones.tone + 2, 73.42, "D2", waveform);
	genTone(data.tones.tone + 3, 77.78, "D#2/Eb2", waveform);
	genTone(data.tones.tone + 4, 82.41, "E2", waveform);
	genTone(data.tones.tone + 5, 87.31, "F2", waveform);
	genTone(data.tones.tone + 6, 92.50, "F#2/Gb2", waveform);
	genTone(data.tones.tone + 7, 98.00, "G2", waveform);
	genTone(data.tones.tone + 8, 103.83, "G#2/Ab2", waveform);
	genTone(data.tones.tone + 9, 110.00, "A2", waveform);
	genTone(data.tones.tone + 10, 116.54, "A#2/Bb2", waveform);
	genTone(data.tones.tone + 11, 123.47, "B2", waveform);
	genTone(data.tones.tone + 12, 130.81, "C3", waveform);
	genTone(data.tones.tone + 13, 138.59, "C#3/Db3", waveform);
	genTone(data.tones.tone + 14, 146.83, "D3", waveform);
	genTone(data.tones.tone + 15, 155.56, "D#3/Eb3", waveform);
	genTone(data.tones.tone + 16, 164.81, "E3", waveform);
	genTone(data.tones.tone + 17, 174.61, "F3", waveform);
	genTone(data.tones.tone + 18, 185.00, "F#3/Gb3", waveform);
	genTone(data.tones.tone + 19, 196.00, "G3", waveform);
	genTone(data.tones.tone + 20, 207.65, "G#3/Ab3", waveform);
	genTone(data.tones.tone + 21, 220.00, "A3", waveform);
	genTone(data.tones.tone + 22, 233.08, "A#3/Bb3", waveform);
	genTone(data.tones.tone + 23, 246.94, "B3", waveform);
	genTone(data.tones.tone + 24, 261.63, "C4", waveform);
	genTone(data.tones.tone + 25, 277.18, "C#4/Db4", waveform);
	genTone(data.tones.tone + 26, 293.66, "D4", waveform);
	genTone(data.tones.tone + 27, 311.13, "D#4/Eb4", waveform);
	genTone(data.tones.tone + 28, 329.63, "E4", waveform);
	genTone(data.tones.tone + 29, 349.23, "F4", waveform);
	genTone(data.tones.tone + 30, 369.99, "F#4/Gb4", waveform);
	genTone(data.tones.tone + 31, 392.00, "G4", waveform);
	genTone(data.tones.tone + 32, 415.30, "G#4/Ab4", waveform);
	genTone(data.tones.tone + 33, 440.00, "A4", waveform);
	genTone(data.tones.tone + 34, 466.16, "A#4/Bb4", waveform);
	genTone(data.tones.tone + 35, 493.88, "B4", waveform);

	/* Generate all the instruments */
	data.instruments.instrument = malloc(sizeof(Instrument) * data.instruments.num);
	/* Initialize Each Instrument */
	for(i=0; i<data.instruments.num; i++) {
		initializeInstrument(&data, data.instruments.instrument + i);
		/* Stagger the endings */
		(data.instruments.instrument+i)->end_beat = i+1;
		if (data.log) {
			/* Log each instrument's label */
			printf("%d,",i);
		}
	}
	if (data.log) {
		/* And a column for the total */
		puts("Total");
	}

	/* Set myself to the beginnning of the beat */
	data.beat_position = 0;
	/* Compute the length of a beat at this tempo */
	data.beat_length = FREQUENCY / (tempo / 60.0);


	SDL_PauseAudio(0);
	/* Sit here until we're terminated */
	while(1);
}
