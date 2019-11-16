#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <getopt.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <signal.h>
#include "irslinger.h"

// inspired by https://github.com/bschwind/ir-slinger

// LIRC config not supported:
// http://www.lirc.org/html/lircd.conf.html
// include
// manual_sort
// suppress_repeat
// flags RC6 / RC-MM / REVERSE / NO_HEAD_REP / NO_FOOT_REP / CONST_LENGTH / REPEAT_HEADER
// driver
// eps
// aeps
// three / two / RC-MM
// plead
// foot
// repeat
// toggle_bit
// toggle_bit_mask
// repeat_mask
// dutycycle (no option to set in Pi)

// read lirc config file
class remote_config {
	public:
		remote_config ()
		{
			config["bits"] = 16;
			config["frequency"] = 38000;
			config["dutycycle"] = 50;
			config["header.on"] = 9000;
			config["header.off"] = 4500;
			config["one.on"] = 562;
			config["one.off"] = 1688;
			config["zero.on"] = 562;
			config["zero.off"] = 562;
			config["pre.on"] = 0;
			config["pre.off"] = 0;
			config["post.on"] = 0;
			config["post.off"] = 0;
			config["ptrail"] = 562;
			config["min_repeat"] = 0;
			config["gap"] = 108000;
			config["pre_data_bits"] = 0;
			config["post_data_bits"] = 0;
		};
		// config parameters: name -> value
		std::unordered_map<std::string,int> config;
		// key codes: button label -> hex code
		std::unordered_map<std::string,std::string> codes;
		// other arbitrary flags
		std::unordered_set<std::string> flags;
		// raw codes
		std::unordered_map<std::string, std::vector<int> > rawcodes;
};

// map of config for each known remote
std::unordered_map<std::string, remote_config> remotes;

// statefulness while parsing remote
bool in_codes = false;
std::string remotename = "default";
bool in_rawcodes = false;
std::string buttonname;

// break a line into a vector of tokens
void tokenise(const std::string in, const char * delimiters, std::vector<std::string> &out)
{
	size_t start = in.find_first_not_of(delimiters);
	size_t end = start;

	while (start != std::string::npos){
		// Find next occurence of delimiter
		end = in.find_first_of(delimiters, start);
		// Push back the token found into vector
		out.push_back(in.substr(start, end-start));
		// Skip all occurences of the delimiter to find new start
		start = in.find_first_not_of(delimiters, end);
	}
}

// parse a single line from remote config file
const char * space_delimiters = " \t\r\n";
int handle_line(const std::string & line)
{
	// tokenise the line
	std::vector<std::string> tokens;
	tokenise(line, space_delimiters, tokens);
	// bale if no tokens
	if (tokens.empty())
	{ return 0; }
	if (tokens[0].at(0) == '#')
	{ return 0; }
	// find number of valid tokens
	size_t i=1;
	size_t maxtoken = tokens.size()-1;
	while (i < tokens.size())
	{
		if (tokens[i].at(0) == '#')
		{
			maxtoken = i-1;
			break;
		}
		++i;
	}
	// all lines in >=2 tokens unless we are in raw codes
	if ((maxtoken == 0) && (in_rawcodes == false))
	{
		std::cerr << "Invalid line: "<<line<<std::endl;
		return -1;
	}

	char xx[50];
	// special case config
	if (tokens[0] == "begin" && tokens[1] == "remote")
	{
		// no action required
		return 0;
	}
	if (tokens[0] == "end" && tokens[1] == "remote")
	{
		// no action required
		return 0;
	}
	if (tokens[0] == "name")
	{
		if (in_rawcodes)
		{
			buttonname = tokens[1];
		} else {
			remotename = tokens[1];
		}
		return 0;
	}

	remote_config &remote(remotes[remotename]);

	if (tokens[0] == "flags")
	{
		std::vector<std::string> fv;
		tokenise(tokens[1],"| ", fv);
		// convert to a set for easy reference
		for (auto f:fv)
		{
			remote.flags.insert(f);
		}
		return 0;
	}
	try
	{
		if ((tokens[0] == "header") ||
			(tokens[0] == "one") ||
			(tokens[0] == "zero") ||
			(tokens[0] == "repeat") ||
			(tokens[0] == "pre") ||
			(tokens[0] == "post"))
		{
			if (maxtoken == 1)
			{
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config[tokens[0] + ".on"] = std::stoi(tokens[1]);
			remote.config[tokens[0] + ".off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "begin" && tokens[1] == "codes")
		{
			in_codes = true;
			in_rawcodes = false;
			return 0;
		}
		if (tokens[0] == "begin" && tokens[1] == "raw_codes")
		{
			in_codes = false;
			in_rawcodes = true;
			return 0;
		}
		if (tokens[0] == "end" && tokens[1] == "codes")
		{
			in_codes = false;
			return 0;
		}
		if (tokens[0] == "end" && tokens[1] == "raw_codes")
		{
			in_rawcodes = false;
			return 0;
		}
		if (in_rawcodes)
		{
			for(auto r:tokens)
			{
				remote.rawcodes[buttonname].push_back(std::stoi(r,nullptr,0));
			}
			return 0;
		}
		if (in_codes)
		{
			remote.codes[tokens[0]] = tokens[1];
			return 0;
		}

		// just save the numeric value
		remote.config[tokens[0]] = std::stoi(tokens[1],nullptr,0);
	}
	catch (...)
	{
		std::cerr << "Bad integer in line: "<<line<<std::endl;
		return -1;
	}
	return 0;
}

// parse a remote config file
int parse_config(const char * filename)
{
	if (filename == nullptr)
	{
		return 1;
	}

	std::ifstream configfile(filename);

	while (configfile.good())
	{
		std::string line;
		std::getline(configfile,line);
		handle_line(line);
	}

	return 0;
}

int running = 1;
void signalHandler(int sig)
{
    running = 0;
}

int prepareRC5(remote_config &remote,
		std::unordered_map<std::string,std::string>::iterator code,
		int pin,
		pincycle * pincycles,
		unsigned int & pulseCount
	      )
{
	std::string pattern("0x");
	if (remote.config.find("pre_data") != remote.config.end())
	{
		char xx[50];
		snprintf(xx,sizeof(xx),"%0*x",(3+remote.config["bits"])/4,remote.config["pre_data"]);
		pattern += xx;
	}
	pattern.append(code->second);
	//std::cout << "pattern is "<<pattern<<std::endl;
	if (gpclkPrepareRC5(pincycles, &pulseCount,
				pin,
				remote.config["frequency"],
				double(remote.config["dutycycle"])/100,
				remote.config["one.on"] + remote.config["one.off"],
				pattern.c_str(),
				remote.config["bits"] + remote.config["pre_data_bits"]))
	{
		std::cerr << "Failed to prepare signal" <<std::endl;
		return -1;
	}
	return 0;
}

int prepareNEC(remote_config &remote,
		std::unordered_map<std::string,std::string>::iterator code,
		int pin,
		pincycle * pincycles,
		unsigned int & pulseCount
	      )
{
	// insert the header
	gpclkAddCycle(pin, pincycles, &pulseCount, true, remote.config["header.on"]);
	gpclkAddCycle(pin, pincycles, &pulseCount, false, remote.config["header.off"]);

	// insert the pre-data
	if ((remote.config.find("pre_data") != remote.config.end()) &&
			(remote.config.find("pre_data_bits") != remote.config.end()))
	{
		int pre_len = (3+remote.config["pre_data_bits"])/4;
		char pre_data_bits[pre_len+4];
		snprintf(pre_data_bits,pre_len+3,"0x%0*x",pre_len,remote.config["pre_data"]);
		if (gpclkPrepare(pincycles, &pulseCount,
					pin,
					remote.config["frequency"],
					double(remote.config["dutycycle"])/100,
					remote.config["one.on"],
					remote.config["zero.on"],
					remote.config["one.off"],
					remote.config["zero.off"],
					pre_data_bits,
					remote.config["pre_data_bits"]))
		{
			std::cerr << "Failed to prepare pre_data" <<std::endl;
			return -1;
		}
	}
	// insert the pre-data pulse
	gpclkAddCycle(pin, pincycles, &pulseCount, true, remote.config["pre.on"]);
	gpclkAddCycle(pin, pincycles, &pulseCount, false, remote.config["pre.off"]);

	// insert the actual data
	if (gpclkPrepare(pincycles, &pulseCount,
				pin,
				remote.config["frequency"],
				double(remote.config["dutycycle"])/100,
				remote.config["one.on"],
				remote.config["zero.on"],
				remote.config["one.off"],
				remote.config["zero.off"],
				code->second.c_str(),
				remote.config["bits"]))
	{
		std::cerr << "Failed to prepare signal" <<std::endl;
		return -1;
	}

	// insert the post data pulse
	gpclkAddCycle(pin, pincycles, &pulseCount, true, remote.config["post.on"]);
	gpclkAddCycle(pin, pincycles, &pulseCount, false, remote.config["post.off"]);

	// insert the post-data
	if ((remote.config.find("post_data") != remote.config.end()) &&
			(remote.config.find("post_data_bits") != remote.config.end()))
	{
		int post_len = (3+remote.config["post_data_bits"])/4;
		char post_data_bits[post_len+4];
		snprintf(post_data_bits,post_len+3,"0x%0*x",post_len,remote.config["post_data"]);
		if (gpclkPrepare(pincycles, &pulseCount,
					pin,
					remote.config["frequency"],
					double(remote.config["dutycycle"])/100,
					remote.config["one.on"],
					remote.config["zero.on"],
					remote.config["one.off"],
					remote.config["zero.off"],
					post_data_bits,
					remote.config["post_data_bits"]))
		{
			std::cerr << "Failed to postpare post_data" <<std::endl;
			return -1;
		}
	}

	// insert ptrail
	gpclkAddCycle(pin, pincycles, &pulseCount, true, remote.config["ptrail"]);

	return 0;
}

int prepareRaw(remote_config &remote,
		const std::string & button,
		int pin,
		pincycle * pincycles,
		unsigned int & pulseCount
	      )
{
	std::unordered_map<std::string,std::vector<int>>::iterator raw = remote.rawcodes.find(button);
	if (raw == remote.rawcodes.end())
	{
		std::cerr << "Button \"" << button << "\" is unknown" << std::endl;
		return 1;
	}

	// convert vector into array for gpclkPrepareRaw
	int pulses[raw->second.size()+1];
	int j = 0;
	for (auto r:raw->second)
	{
		pulses[j++] = r;
	}
	// ensure we always end in an off
	if ((j % 2) == 1)
	{
		pulses[j] = pulses[j-1];
		++j;
	}
	if (gpclkPrepareRaw(pincycles, &pulseCount,
				pin,
				remote.config["frequency"],
				double(remote.config["dutycycle"])/100,
				pulses,
				j))
	{
		std::cerr << "Failed to prepare signal" <<std::endl;
		return -1;
	}
	return 0;
}

// program arguments
bool dumpcodes=false;
int maxovershoot = 10000; //ms

// send the waveform
int doTransmit(pincycle * pincycles, unsigned int & pulseCount)
{
	// insert the end marker
	pincycles[pulseCount++] = pincycle(-4, false, maxovershoot * 1000);

	// send the waveform
	gpclkWave(pincycles, pulseCount);

	if (dumpcodes)
	{
		for(int i=0; i<pulseCount; ++i)
		{
			printf("%4d %1d %d.%09d\n",i,pincycles[i].m_state, pincycles[i].m_duration.tv_sec, pincycles[i].m_duration.tv_nsec);
		}
	}
}

int main(int argc, char *argv[])
{
	const char * configfile = nullptr;
	int pin = 23;
	int c ;
	int lastfreq = 0;
	bool dumpconfig=false;
	std::string thisremote;
	std::string defaultremote;
	// msec warmup loop - ramp processor clock speed
	long warmup = 0;
	while( ( c = getopt (argc, argv, "o:Dr:dp:f:w:") ) != -1 ) 
	{
		switch(c)
		{
			case 'D':
				// dump out the on/off patterns we are going to send
				dumpcodes = true;
				break;
			case 'd':
				// dump the parsed remote control config
				dumpconfig = true;
				break;
			case 'f':
				// remote control file to use
				if (parse_config(optarg))
				{
					std::cerr<<"Failed to parse config from "<<optarg<<std::endl;
					exit(1);
				}
				break;
			case 'p':
				// pin to control
				// must be BCM 4 or 6 (physical 7 or 31)
				if(optarg) pin = std::atoi(optarg) ;
				break;
			case 'r':
				// default remote to use if >1 loaded
				if(optarg) defaultremote = optarg;
				break;
			case 'o':
				// max timing overshoot permitted before a code
				// will be repeated
				if(optarg) maxovershoot = std::atoi(optarg) ;
				break;
			case 'w':
				// warmup time - busy loop for this long before
				// starting, useful to crank processor out of power saving
				// before doing the real work (or at least was on an x86
				// system!)
				if(optarg) warmup = std::atoi(optarg) ;
				break;
			default:
				std::cerr<<"Unexpected argument "<<((char)c)<<std::endl
				         <<"Valid arguments are:"<<std::endl
					 <<"    -p pinnumber"<<std::endl
					 <<"    -f lircremotefile"<<std::endl
					 <<"    -r defaultremotename"<<std::endl
					 <<"    -d = dumpconfig"<<std::endl
					 <<"    -D = dump pattern"<<std::endl
					 <<"    -w msecs (warm processor)"<<std::endl
					 <<"    -o msecs = max overshoot"<<std::endl
					 <<"     button button button... or"<<std::endl
					 <<"     remotename.button remotename.button..."<<std::endl;
				exit(1);
		}			    
	}
	if (remotes.size() == 0)
	{
		std::cerr<<"No remotes defined!" << std::endl;
		exit(1);
	}

	// if only one remote defined then use that by default
	if ((remotes.size() == 1) && (thisremote.size() == 0))
	{
		defaultremote = remotes.begin()->first;
	}

	if (dumpconfig)
	{
		for (auto r:remotes)
		{
			remote_config &remote(r.second);
			std::cout <<"Config for remote "<<r.first<<std::endl;
			for (auto k:remote.config)
			{
				std::cout << "config: "<<k.first<<" "<<k.second<<std::endl;
			}
			for (auto k:remote.codes)
			{
				std::cout << "code  : "<<k.first<<" "<<k.second<<std::endl;
			}
			for (auto k:remote.rawcodes)
			{
				std::cout << "raw   : "<<k.first;
				for (auto l:k.second)
				{
					std::cout<<" "<<l;
				}
				std::cout<<std::endl;
			}
		}
	}

	// I am important
	struct sched_param sp;
	sp.sched_priority = 1;
	if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
		perror("Could not set scheduler, continuing");
	}

	// Set ctrl-c handler
	signal(SIGINT, signalHandler);

	int result = -1;
	int ret = gpioInitialise();
	if (ret < 0)
	{
		return ret;
	}

	// TODO bounds check the usage of this array
	pincycle pincycles[MAX_PULSES];
	unsigned int pulseCount = 0;

	// for each button given as an argument
	for (c=optind ; c < argc ; ++c)
	{
		// exit handling
		if (running == 0)
		{
			break;
		}

		// try to break button argument into remote.button
		std::string button;
		char * dot = strchr(argv[c],'.');
		if (dot == nullptr)
		{
			// no remote mentioned use the default
			thisremote = defaultremote;
			button = argv[c];
		}
		else
		{
			// find the specified remote
			thisremote = std::string(argv[c],dot - argv[c]);
			button = dot+1;
		}

		// do we have a remote?
		if (remotes.find(thisremote) == remotes.end())
		{
			std::cerr << "Remote " << thisremote << " does not exist" << std::endl;
			exit(1);
		}
		remote_config &remote(remotes[thisremote]);

		// does this remote operates at a different frequency to the previous remote
		if (lastfreq == remote.config["frequency"])
		{
			// same frequency, fall through and append this sequence to the previous
		}
		else
		{
			// go send the transmission
			if (pulseCount > 0)
			{
				doTransmit(pincycles, pulseCount);

				// reset codes buffer
				pulseCount = 0;
			}
			else
			{
				// this must be the first transmission
				// add any required warmup in
				if (warmup > 0)
				{
					gpclkAddCycle(pin, pincycles, &pulseCount, false, warmup*1000);
				}
			}

			// remember the transmission frequency
			lastfreq = remote.config["frequency"];
			// set the frequency
			gpclkWavePre(pin, lastfreq);
		}

		// prepare the transmission buffer
		for(int i=0 ; i<=remote.config["min_repeat"]; ++i)
		{
			// add a marker in to the sequence for rewinding to
			// if a transmission is corrupted due to scheduling
			pincycles[pulseCount++] = pincycle(-3, false, maxovershoot * 1000);

			// add the sequence according to the remote type
			if (remote.flags.find("RAW_CODES") != remote.flags.end())
			{
				if (prepareRaw(remote, button, pin, pincycles, pulseCount))
				{
					result |= 1;
					continue;
				}
			}
			else
			{
				// NEC and RC5 share the same code syntax
				std::unordered_map<std::string,std::string>::iterator code = remote.codes.find(button);
				if (code == remote.codes.end())
				{
					std::cerr << "Button \"" << button << "\" is unknown on remote " << thisremote << std::endl;
					result |= 1;
					continue;
				}
				if (result == -1) { result = 0; }

				// NEC type
				if (remote.flags.find("SPACE_ENC") != remote.flags.end())
				{
					if (prepareNEC(remote, code, pin, pincycles, pulseCount))
					{
						result |= 1;
						continue;
					}
				}
				// RC5
				else if (remote.flags.find("RC5") != remote.flags.end())
				{
					if (prepareRC5(remote, code, pin, pincycles, pulseCount))
					{
						result |= 1;
						continue;
					}
				}
				else
				{
					std::cerr << "Remote type is not supported" << std::endl;
					continue;
				}
			}

			// add the gap
			gpclkAddCycle(pin, pincycles, &pulseCount, false, remote.config["gap"]);
		}
	}

	// make final transmission
	if (pulseCount > 0)
	{
		doTransmit(pincycles, pulseCount);
	}

	gpclkWavePost(pin);
	if (numrepeated > 0)
	{
		//std::cerr << numrepeated << " codes had to be repeated." <<std::endl;
	}
	return result;
}

