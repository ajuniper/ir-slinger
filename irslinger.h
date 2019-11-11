#ifndef IRSLINGER_H
#define IRSLINGER_H

#include <string.h>
#include <math.h>
#include <pigpio.h>
#include <stdio.h>

#define MAX_COMMAND_SIZE 512
#define MAX_PULSES 12000

static inline void addPulse(uint32_t onPins, uint32_t offPins, uint32_t duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
	int index = *pulseCount;

	irSignal[index].gpioOn = onPins;
	irSignal[index].gpioOff = offPins;
	irSignal[index].usDelay = duration;

	(*pulseCount)++;
}

// Generates a square wave for duration (microseconds) at frequency (Hz)
// on GPIO pin outPin. dutyCycle is a floating value between 0 and 1.
static inline void carrierFrequency(uint32_t outPin, double frequency, double dutyCycle, double duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
	double oneCycleTime = 1000000.0 / frequency; // 1000000 microseconds in a second
	int onDuration = (int)round(oneCycleTime * dutyCycle);
	int offDuration = (int)round(oneCycleTime * (1.0 - dutyCycle));

	int totalCycles = (int)round(duration / oneCycleTime);
	int totalPulses = totalCycles * 2;

	int i;
	for (i = 0; i < totalPulses; i++)
	{
		if (i % 2 == 0)
		{
			// High pulse
			addPulse(1 << outPin, 0, onDuration, irSignal, pulseCount);
		}
		else
		{
			// Low pulse
			addPulse(0, 1 << outPin, offDuration, irSignal, pulseCount);
		}
	}
}

static inline int getbit(const char * code, int bitnum)
{
	if (code[0] == '0' && code[1] == 'x') {
		char x = code[2+(bitnum/4)];
		int n;
		if (x >= '0' && x<= '9') {
			n=x - '0';
		} else if (x >= 'a' && x<='f') {
			n=x - 'a' + 10;
		} else if (x >= 'A' && x<='F') {
			n=x - 'A' + 10;
		} else {
			printf("Character %c is not valid hex\n",x);
			return -1;
		}
		//printf("Bit %d from digit %c n %d\n",(n & (1<<(3-(bitnum%4))))?1:0,x,n);
		return (n & (1<<(3-(bitnum%4))))?1:0;
	}
	if (code[bitnum] == '0') {
		return 0;
	}
	if (code[bitnum] == '1') {
		return 1;
	}
	printf("Do not recognise bit code %c at position %d\n",code[bitnum],bitnum);
	return -1;
}

// Generates a low signal gap for duration, in microseconds, on GPIO pin outPin
static inline void gap(uint32_t outPin, double duration, gpioPulse_t *irSignal, unsigned int *pulseCount)
{
	addPulse(0, 0, duration, irSignal, pulseCount);
}

// Transmit generated wave
static inline int transmitWavePre(uint32_t outPin)
{
	if (outPin > 31)
	{
		// Invalid pin number
		return 1;
	}

	// Init pigpio
	if (gpioInitialise() < 0)
	{
		// Initialization failed
		printf("GPIO Initialization failed\n");
		return 1;
	}

	// Setup the GPIO pin as an output pin
	gpioSetMode(outPin, PI_OUTPUT);
	return 0;
}

static inline int transmitWave(gpioPulse_t *irSignal, unsigned int pulseCount)
{
	// Start a new wave
	gpioWaveClear();

	gpioWaveAddGeneric(pulseCount, irSignal);
	int waveID = gpioWaveCreate();

	if (waveID >= 0)
	{
		int result = gpioWaveTxSend(waveID, PI_WAVE_MODE_ONE_SHOT);

		printf("Result: %i\n", result);
	}
	else
	{
		printf("Wave creation failure!\n %i", waveID);
		return 1;
	}

	// Wait for the wave to finish transmitting
	while (gpioWaveTxBusy())
	{
		time_sleep(0.1);
	}

	// Delete the wave if it exists
	if (waveID >= 0)
	{
		gpioWaveDelete(waveID);
	}
	return 0;
}

static inline int transmitWavePost()
{
	// Cleanup
	gpioTerminate();
	return 0;
}

static inline int irSlingPrepareRC5(gpioPulse_t *irSignal,
	unsigned int * pulseCount,
	int outPin,
	int frequency,
	double dutyCycle,
	int pulseDuration,
	const char *code,
	size_t codeLen)
{
	if (codeLen > MAX_COMMAND_SIZE)
	{
		// Command is too big
		return 1;
	}

	// Generate Code
	int i;
	for (i = 0; i < codeLen; i++)
	{
		switch (getbit(code,i))
		{
			case 0:
				carrierFrequency(outPin, frequency, dutyCycle, pulseDuration, irSignal, pulseCount);
				gap(outPin, pulseDuration, irSignal, pulseCount);
				break;
			case 1:
				gap(outPin, pulseDuration, irSignal, pulseCount);
				carrierFrequency(outPin, frequency, dutyCycle, pulseDuration, irSignal, pulseCount);
				break;
		}
	}

	printf("pulse count is %i\n", *pulseCount);
	// End Generate Code
}

static inline int irSlingRC5(uint32_t outPin,
	int frequency,
	double dutyCycle,
	int pulseDuration,
	const char *code)
{
	gpioPulse_t irSignal[MAX_PULSES];
	unsigned int pulseCount = 0;

	size_t codeLen = strlen(code);
	if (code[0]=='0' && code[1]=='x') {
		codeLen=(codeLen-2)*4;
	}

	printf("code size is %zu\n", codeLen);

	if (irSlingPrepareRC5(irSignal, &pulseCount, outPin, frequency, dutyCycle, pulseDuration, code, codeLen)) return 1;
	int ret = 1;
	if (transmitWavePre(outPin)) return 1;
	ret = transmitWave(irSignal, pulseCount);
	transmitWavePost();
	return ret;
}

static inline int irSlingPrepare(gpioPulse_t *irSignal, unsigned int * pulseCount, uint32_t outPin,
	int frequency,
	double dutyCycle,
	int leadingPulseDuration,
	int leadingGapDuration,
	int onePulse,
	int zeroPulse,
	int oneGap,
	int zeroGap,
	int sendTrailingPulse,
	const char *code,
	size_t codeLen)
{
	if (outPin > 31)
	{
		// Invalid pin number
		return 1;
	}

	// Generate Code
	carrierFrequency(outPin, frequency, dutyCycle, leadingPulseDuration, irSignal, pulseCount);
	gap(outPin, leadingGapDuration, irSignal, pulseCount);

	int i;
	for (i = 0; i < codeLen; i++)
	{
		switch (getbit(code,i))
		{
			case 0:
				carrierFrequency(outPin, frequency, dutyCycle, zeroPulse, irSignal, pulseCount);
				gap(outPin, zeroGap, irSignal, pulseCount);
				break;
			case 1:
				carrierFrequency(outPin, frequency, dutyCycle, onePulse, irSignal, pulseCount);
				gap(outPin, oneGap, irSignal, pulseCount);
				break;
		}
	}

	if (sendTrailingPulse==1)
	{
		carrierFrequency(outPin, frequency, dutyCycle, onePulse, irSignal, pulseCount);
	}
	else if (sendTrailingPulse > 0)
	{
		carrierFrequency(outPin, frequency, dutyCycle, sendTrailingPulse, irSignal, pulseCount);
	}

	printf("pulse count is %i\n", *pulseCount);
	// End Generate Code
	return 0;
}

static inline int irSling(uint32_t outPin,
	int frequency,
	double dutyCycle,
	int leadingPulseDuration,
	int leadingGapDuration,
	int onePulse,
	int zeroPulse,
	int oneGap,
	int zeroGap,
	int sendTrailingPulse,
	const char *code)
{
	size_t codeLen = strlen(code);
	if (code[0]=='0' && code[1]=='x') {
		codeLen=(codeLen-2)*4;
	}

	printf("code size is %zu\n", codeLen);

	if (codeLen > MAX_COMMAND_SIZE)
	{
		// Command is too big
		return 1;
	}

	gpioPulse_t irSignal[MAX_PULSES];
	unsigned int pulseCount = 0;

	if (irSlingPrepare(irSignal, &pulseCount, outPin, frequency, dutyCycle, leadingPulseDuration, leadingGapDuration, onePulse, zeroPulse, oneGap, zeroGap, sendTrailingPulse, code, codeLen)) return 1;
	int ret = 1;
	if (transmitWavePre(outPin)) return 1;
	ret = transmitWave(irSignal, pulseCount);
	transmitWavePost();
	return ret;
}

#if 0
static inline int irSlingRaw(uint32_t outPin,
	int frequency,
	double dutyCycle,
	const int *pulses,
	int numPulses)
{
	if (outPin > 31)
	{
		// Invalid pin number
		return 1;
	}

	// Generate Code
	gpioPulse_t irSignal[MAX_PULSES];
	int pulseCount = 0;

	int i;
	for (i = 0; i < numPulses; i++)
	{
		if (i % 2 == 0) {
			carrierFrequency(outPin, frequency, dutyCycle, pulses[i], irSignal, &pulseCount);
		} else {
			gap(outPin, pulses[i], irSignal, &pulseCount);
		}
	}

	printf("pulse count is %i\n", pulseCount);
	// End Generate Code

	return transmitWave(outPin, irSignal, &pulseCount);
}
#endif

#endif
