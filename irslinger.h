#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

/*****************************************************************
 * This part taken from http://abyz.me.uk/rpi/pigpio/code/minimal_clk.zip
 */

/*****************************************************************/

static volatile uint32_t piModel = 1;

static volatile uint32_t piPeriphBase = 0x20000000;

#define CLK_BASE   (piPeriphBase + 0x101000)
#define GPIO_BASE  (piPeriphBase + 0x200000)

#define CLK_LEN   0xA8
#define GPIO_LEN  0xB4

#define CLK_PASSWD  (0x5A<<24)

#define CLK_CTL_MASH(x)((x)<<9)
#define CLK_CTL_BUSY    (1 <<7)
#define CLK_CTL_KILL    (1 <<5)
#define CLK_CTL_ENAB    (1 <<4)
#define CLK_CTL_SRC(x) ((x)<<0)

#define CLK_SRCS 4

#define CLK_CTL_SRC_OSC  1  /* 19.2 MHz */
#define CLK_CTL_SRC_PLLC 5  /* 1000 MHz */
#define CLK_CTL_SRC_PLLD 6  /*  500 MHz */
#define CLK_CTL_SRC_HDMI 7  /*  216 MHz */

#define CLK_DIV_DIVI(x) ((x)<<12)
#define CLK_DIV_DIVF(x) ((x)<< 0)

#define CLK_GP0_CTL 28
#define CLK_GP0_DIV 29
#define CLK_GP1_CTL 30
#define CLK_GP1_DIV 31
#define CLK_GP2_CTL 32
#define CLK_GP2_DIV 33

#define CLK_PCM_CTL 38
#define CLK_PCM_DIV 39

#define CLK_PWM_CTL 40
#define CLK_PWM_DIV 41

static volatile uint32_t  *gpioReg = ((volatile uint32_t  *)MAP_FAILED);
static volatile uint32_t  *clkReg  = ((volatile uint32_t  *)MAP_FAILED);

/* gpio modes. */

#define PI_INPUT  0
#define PI_ALT0   4

void gpioSetMode(unsigned gpio, unsigned mode)
{
   int reg, shift;

   reg   =  gpio/10;
   shift = (gpio%10) * 3;

   gpioReg[reg] = (gpioReg[reg] & ~(7<<shift)) | (mode<<shift);
}

unsigned gpioHardwareRevision(void)
{
   static unsigned rev = 0;

   FILE * filp;
   char buf[512];
   char term;
   int chars=4; /* number of chars in revision string */

   if (rev) return rev;

   piModel = 0;

   filp = fopen ("/proc/cpuinfo", "r");

   if (filp != NULL)
   {
      while (fgets(buf, sizeof(buf), filp) != NULL)
      {
         if (piModel == 0)
         {
            if (!strncasecmp("model name", buf, 10))
            {
               if (strstr (buf, "ARMv6") != NULL)
               {
                  piModel = 1;
                  chars = 4;
                  piPeriphBase = 0x20000000;
               }
               else if (strstr (buf, "ARMv7") != NULL)
               {
                  piModel = 2;
                  chars = 6;
                  piPeriphBase = 0x3F000000;
               }
            }
         }

         if (!strncasecmp("revision", buf, 8))
         {
            if (sscanf(buf+strlen(buf)-(chars+1),
               "%x%c", &rev, &term) == 2)
            {
               if (term != '\n') rev = 0;
            }
         }
      }

      fclose(filp);
   }
   return rev;
}

static int initClock(int clock, int source, int divI, int divF, int MASH)
{
   int ctl[] = {CLK_GP0_CTL, CLK_GP2_CTL};
   int div[] = {CLK_GP0_DIV, CLK_GP2_DIV};
   int src[CLK_SRCS] =
      {CLK_CTL_SRC_PLLD,
       CLK_CTL_SRC_OSC,
       CLK_CTL_SRC_HDMI,
       CLK_CTL_SRC_PLLC};

   int clkCtl, clkDiv, clkSrc;
   uint32_t setting;

   if ((clock  < 0) || (clock  > 1))    return -1;
   if ((source < 0) || (source > 3 ))   return -2;
   if ((divI   < 2) || (divI   > 4095)) return -3;
   if ((divF   < 0) || (divF   > 4095)) return -4;
   if ((MASH   < 0) || (MASH   > 3))    return -5;

   clkCtl = ctl[clock];
   clkDiv = div[clock];
   clkSrc = src[source];

   clkReg[clkCtl] = CLK_PASSWD | CLK_CTL_KILL;

   /* wait for clock to stop */

   while (clkReg[clkCtl] & CLK_CTL_BUSY)
   {
      usleep(10);
   }

   clkReg[clkDiv] =
      (CLK_PASSWD | CLK_DIV_DIVI(divI) | CLK_DIV_DIVF(divF));

   usleep(10);

   clkReg[clkCtl] =
      (CLK_PASSWD | CLK_CTL_MASH(MASH) | CLK_CTL_SRC(clkSrc));

   usleep(10);

   clkReg[clkCtl] |= (CLK_PASSWD | CLK_CTL_ENAB);
}

static int termClock(int clock)
{
   int ctl[] = {CLK_GP0_CTL, CLK_GP2_CTL};

   int clkCtl;

   if ((clock  < 0) || (clock  > 1))    return -1;

   clkCtl = ctl[clock];

   clkReg[clkCtl] = CLK_PASSWD | CLK_CTL_KILL;

   /* wait for clock to stop */

   while (clkReg[clkCtl] & CLK_CTL_BUSY)
   {
      usleep(10);
   }
}


/* Map in registers. */

static uint32_t * initMapMem(int fd, uint32_t addr, uint32_t len)
{
    return (uint32_t *) mmap(0, len,
       PROT_READ|PROT_WRITE|PROT_EXEC,
       MAP_SHARED|MAP_LOCKED,
       fd, addr);
}

struct timespec lastedge;

int gpioInitialise(void)
{
   int fd;

   gpioHardwareRevision(); /* sets piModel, needed for peripherals address */

   fd = open("/dev/mem", O_RDWR | O_SYNC) ;

   if (fd < 0)
   {
      fprintf(stderr,
         "This program needs root privileges.  Try using sudo\n");
      return -1;
   }

   gpioReg  = initMapMem(fd, GPIO_BASE, GPIO_LEN);
   clkReg   = initMapMem(fd, CLK_BASE,  CLK_LEN);

   close(fd);

   if ((gpioReg == MAP_FAILED) ||
       (clkReg == MAP_FAILED))
   {
      fprintf(stderr,
         "Bad, mmap failed\n");
      return -1;
   }
   // force initialisation at transmit time
   lastedge.tv_sec = 0;
   return 0;
}

static inline int gpclkWavePre(uint32_t outPin, int frequency)
{
	double temp = (19200000.0/double(frequency));
	int divi = int(temp);
	int divf = int(4096.0 * (temp - double(divi)));
	int gpclk = 0;
	if ((divi < 2) || (divi > 4095))
	{
		fprintf(stderr, "Frequency %d is not supported\n",frequency);
		return -1;
	}

	if (outPin == 4)
	{
		gpclk = 0;
	}
	else if (outPin == 6)
	{
		// means gpclk2 really
		gpclk = 1;
	}
	else
	{
		fprintf(stderr, "Pin %d is not supported\n",outPin);
		return -1;
	}
	initClock(gpclk, 1 /*osc*/, divi, divf, 0 /*mash*/);
	gpioSetMode(outPin, PI_INPUT);
	return 0;
}

static inline int gpclkWavePost(uint32_t outPin)
{
	int gpclk = 0;
	if (outPin == 6)
	{
		// means gpclk2 really
		gpclk = 1;
	}
	gpioSetMode(outPin, PI_INPUT);
	termClock(gpclk);
}

/*****************************************************************
 * This part derived from / inspired by
 * https://github.com/bschwind/ir-slinger
 */
/*****************************************************************/

#define MAX_PULSES 2000

// object stored for each stage of the waveform
class pincycle
{
	public:
		// default constructor is empty
		pincycle()
		{
			m_duration.tv_sec = 0;
			m_duration.tv_nsec = 0;
		}
		// create a stage of the cycle
		// assumes no stages are >=1s
		pincycle(int pin, bool state, long nsec)
		{
			m_state = state;
			m_pin = pin;
			m_duration.tv_sec = 0;
			m_duration.tv_nsec = nsec;
		}
		bool m_state = false; // 1 or 0
		int m_pin = -2;
		struct timespec m_duration;
};

// set pin state and busy loop wait for the prescribed time
int maxwait = 0;
void set_and_wait(pincycle * cycle)
{
    struct timespec now;
    int count = 0;
    // set the pin
    if (cycle->m_pin > 0)
    {
	    if (cycle->m_state == true)
	    {
		    gpioSetMode(cycle->m_pin, PI_ALT0);
	    }
	    else
	    {
		    gpioSetMode(cycle->m_pin, PI_INPUT);
	    }
    }

    // always count time from previous transition not now
    lastedge.tv_nsec += cycle->m_duration.tv_nsec;
    if (lastedge.tv_nsec > 1000000000) {
        lastedge.tv_nsec -= 1000000000;
        lastedge.tv_sec += 1;
    }

    // busy loop until done
    clock_gettime(CLOCK_REALTIME,&now);
    while ((now.tv_sec < lastedge.tv_sec) || (now.tv_nsec < lastedge.tv_nsec)) {
        ++count;
        clock_gettime(CLOCK_REALTIME,&now);
    }
    int wait=((now.tv_sec-lastedge.tv_sec)*1000000000)+(now.tv_nsec-lastedge.tv_nsec);
    if (wait > maxwait) { maxwait = wait; }
    //printf("count %d delay %d.%09ld\n",count,(now.tv_sec-lastedge.tv_sec),(now.tv_nsec-lastedge.tv_nsec));
}

// run the given waveform
int numrepeated = 0;
inline int gpclkWave(pincycle * wave, unsigned int cycleCount)
{
	if (lastedge.tv_sec == 0)
	{
		clock_gettime(CLOCK_REALTIME,&lastedge);
	}
	int i;
	// remember the start of the last code in case there is timing
	// overshoot
	pincycle * marker = wave;
	// record the max overshoot during this cycle
	maxwait = 0;
	// limit the number of times we will repeat something
	int numrepeats = 0;

	// keep going until we hit the end marker
	while(1)
	{
		if ((wave->m_pin == -3) || (wave->m_pin == -4))
		{
			// this is a marker indicating the start of a new code
			// check the max overshoot in this code and rewind if required
			if ((numrepeats < 5) && (maxwait > wave->m_duration.tv_nsec))
			{
				++numrepeated;
				++numrepeats;
				wave = marker;
			}
			else
			{
				// last code was good, are we done?
				if (wave->m_pin == -4)
				{
					break;
				}
				// last code was good, move on
				marker = wave;
				numrepeats = 0;
			}

			// skip over this code start marker
			++wave;
			maxwait = 0;

			// play nicely
			sched_yield();
			clock_gettime(CLOCK_REALTIME,&lastedge);

			continue;
		}
		set_and_wait(wave);
		++wave;
	}
	return 0;
}

// add a cycle to the set
// coalesces adjacent same-state stages
void gpclkAddCycle(int outPin, pincycle * irSignal, unsigned int * pulseCount, bool state, long usec)
{
	if (usec == 0)
	{
		return;
	}
	if (((*pulseCount) == 0) || (state != irSignal[(*pulseCount) - 1].m_state))
	{
		irSignal[*pulseCount] = pincycle(outPin, state, usec*1000);
		++(*pulseCount);
	}
	else
	{
		// this state is same as previous so reuse
		irSignal[(*pulseCount) - 1].m_duration.tv_nsec += (usec * 1000);
		if (irSignal[(*pulseCount) - 1].m_duration.tv_nsec > 1000000000)
		{
			++ irSignal[(*pulseCount) - 1].m_duration.tv_sec;
			irSignal[(*pulseCount) - 1].m_duration.tv_nsec -= 1000000000;
		}
	}
}

// add a set of on/off cycles to the waveform
int gpclkPrepareRaw(pincycle * irSignal, unsigned int * pulseCount, uint32_t outPin,
	int frequency,
	double dutyCycle,
	const int *pulses,
	int numPulses)
{
	int i;
	for (i = 0; i < numPulses; i++)
	{
		gpclkAddCycle(outPin, irSignal, pulseCount, ((i%2)==0)?true:false, pulses[i]);
	}

	//printf("pulse count is %i from %i\n", *pulseCount, numPulses);
	// End Generate Code
	return 0;
}

// extract bit bitnum from string code
// deals with binary and 0x... forms
// bitnum is 0 based
// if total waveform length is not round number of nibbles in 0x... form
// then waveform is assumed to be right justified in those nibbles
static inline int getbit(const char * code, int bitnum, int totalLength)
{
	if (code[0] == '0' && code[1] == 'x') {
		size_t digitofs = strlen(code);
		// find the character/nibble offset from the end of the string
		// i.e. the final bit to transmit is the lsb of the final digit
		int nibblebit = totalLength - 1 - bitnum;
		digitofs -= 1+((nibblebit)/4);
		// check the string is long enough
		if (digitofs < 2)
		{
			printf("Code %s is not long enough for %d bits\n",code,totalLength);
			return -1;
		}

		char x = code[digitofs];
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
		int ret = (n & (1<<(nibblebit%4)));
		//printf("code %s bitnum %d totlen %d => nibblebit %d digitofs %d => %d\n",code,bitnum,totalLength,nibblebit,digitofs,ret);
		return (ret>0)?1:0;
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

// prepare waveform from an RC5 pattern
int gpclkPrepareRC5(pincycle *irSignal,
	unsigned int * pulseCount,
	int outPin,
	int frequency,
	double dutyCycle,
	int pulseDuration,
	const char *code,
	size_t codeLen)
{
	if (codeLen > MAX_PULSES)
	{
		// Command is too big
		return 1;
	}

	// Generate Code
	int i;
	for (i = 0; i < codeLen; i++)
	{
		switch (getbit(code,i,codeLen))
		{
			case 0:
				gpclkAddCycle(outPin, irSignal, pulseCount, true, pulseDuration);
				gpclkAddCycle(outPin, irSignal, pulseCount, false, pulseDuration);
				break;
			case 1:
				gpclkAddCycle(outPin, irSignal, pulseCount, false, pulseDuration);
				gpclkAddCycle(outPin, irSignal, pulseCount, true, pulseDuration);
				break;
		}
	}

	//printf("pulse count is %i\n", *pulseCount);
	// End Generate Code
}

// prepare a waveform from a pattern where 1 and 0 have distinct durations
int gpclkPrepare(pincycle *irSignal, unsigned int * pulseCount, uint32_t outPin,
	int frequency,
	double dutyCycle,
	int onePulse,
	int zeroPulse,
	int oneGap,
	int zeroGap,
	const char *code,
	size_t codeLen)
{
	// Generate Code
	int i;
	for (i = 0; i < codeLen; i++)
	{
		switch (getbit(code,i,codeLen))
		{
			case 0:
				gpclkAddCycle(outPin, irSignal, pulseCount, true, zeroPulse);
				gpclkAddCycle(outPin, irSignal, pulseCount, false, zeroGap);
				break;
			case 1:
				gpclkAddCycle(outPin, irSignal, pulseCount, true, onePulse);
				gpclkAddCycle(outPin, irSignal, pulseCount, false, oneGap);
				break;
		}
	}

	//printf("pulse count is %i\n", *pulseCount);
	// End Generate Code
	return 0;
}
