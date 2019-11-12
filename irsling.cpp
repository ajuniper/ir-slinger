#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <getopt.h>
#include <unistd.h>
#include "irslinger.h"

// compile with:
// g++ irsling.cpp -lpigpio -lm -lpthread

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
		if (tokens[0] == "header")
		{
			if (maxtoken == 1)
			{
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["header.on"] = std::stoi(tokens[1]);
			remote.config["header.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "one")
		{
			if (maxtoken == 1)
			{
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["one.on"] = std::stoi(tokens[1]);
			remote.config["one.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "zero")
		{
			if (maxtoken == 1)
			{
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["zero.on"] = std::stoi(tokens[1]);
			remote.config["zero.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "repeat")
		{
			if (maxtoken == 1)
			{
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["repeat.on"] = std::stoi(tokens[1]);
			remote.config["repeat.off"] = std::stoi(tokens[2]);
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
			// TODO what if bits is not multiple of 4?
			//snprintf(xx,sizeof(xx),"%0*x",(3+remote.config["bits"])/4,std::stoi(tokens[1],nullptr,0));
			//remote.codes[tokens[0]] = std::string(xx);
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

// -p pin
// -f config
// * button name(s)
int main(int argc, char *argv[])
{
	const char * configfile = nullptr;
	int pin = 23;
	int c ;
	bool dumpconfig=false;
	std::string thisremote;
	std::string defaultremote;
	while( ( c = getopt (argc, argv, "r:dp:f:") ) != -1 ) 
	{
		switch(c)
		{
			case 'd':
				dumpconfig = true;
				break;
			case 'f':
				if (parse_config(optarg))
				{
					std::cerr<<"Failed to parse config from "<<optarg<<std::endl;
					exit(1);
				}
				break;
			case 'p':
				if(optarg) pin = std::atoi(optarg) ;
				break;
			case 'r':
				if(optarg) defaultremote = optarg;
				break;
			default:
				std::cerr<<"Unexpected argument "<<c<<std::endl
				         <<"Valid arguments are:"<<std::endl
					 <<"    -p pinnumber"<<std::endl
					 <<"    -f lircremotefile"<<std::endl
					 <<"    -r defaultremotename"<<std::endl
					 <<"    -d = dumpconfig"<<std::endl
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

	// if only one remote defined then use that
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

	int result = -1;
	transmitWavePre(pin);
	for (c=optind ; c < argc ; ++c)
	{
		// try to break button into remote.button
		std::string button;
		char * dot = strchr(argv[c],'.');
		if (dot == nullptr)
		{
			// no remote mentioned
			thisremote = defaultremote;
			button = argv[c];
		}
		else
		{
			thisremote = std::string(argv[c],dot - argv[c]);
			button = dot+1;
		}

		if (remotes.find(thisremote) == remotes.end())
		{
			std::cerr << "Remote " << thisremote << " does not exist" << std::endl;
			exit(1);
		}
		remote_config &remote(remotes[thisremote]);

		gpioPulse_t irSignal[MAX_PULSES];
		unsigned int pulseCount = 0;

		if (remote.flags.find("RAW_CODES") != remote.flags.end())
		{
			std::unordered_map<std::string,std::vector<int>>::iterator raw = remote.rawcodes.find(button);
			if (raw == remote.rawcodes.end())
			{
				std::cerr << "Button \"" << button << "\" is unknown on remote " << thisremote << std::endl;
				result |= 1;
				continue;
			}
			if (result == -1) { result = 0; }
			int pulses[raw->second.size()];
			int j = 0;
			for (auto r:raw->second)
			{
				pulses[j++] = r;
			}
			// ensure we always end in an off
			if ((j % 2) == 1)
			{
				pulses[j] = pulses[j-1];
			}
			if (irSlingPrepareRaw(irSignal, &pulseCount,
						pin,
						remote.config["frequency"],
						double(remote.config["dutycycle"])/100,
						pulses,
						raw->second.size()))
			{
				std::cerr << "Failed to prepare signal" <<std::endl;
				continue;
			}
		}
		else
		{
			std::unordered_map<std::string,std::string>::iterator code = remote.codes.find(button);
			if (code == remote.codes.end())
			{
				std::cerr << "Button \"" << button << "\" is unknown on remote " << thisremote << std::endl;
				result |= 1;
				continue;
			}
			if (result == -1) { result = 0; }

			// nec type
			if (remote.flags.find("SPACE_ENC") != remote.flags.end())
			{
				// insert the header
				if (irSlingPrepare(irSignal, &pulseCount,
							pin,
							remote.config["frequency"],
							double(remote.config["dutycycle"])/100,
							remote.config["header.on"],
							remote.config["header.off"],
							0, 0, 0, 0, 0, nullptr, 0)) // no more content
				{
					std::cerr << "Failed to prepare header" <<std::endl;
					continue;
				}

				// insert the pre-data
				if ((remote.config.find("pre_data") != remote.config.end()) &&
					(remote.config.find("pre_data_bits") != remote.config.end()))
				{
					int pre_len = (3+remote.config["pre_data_bits"])/4;
					char pre_data_bits[pre_len+4];
					snprintf(pre_data_bits,pre_len+3,"0x%0*x",pre_len,remote.config["pre_data"]);
					if (irSlingPrepare(irSignal, &pulseCount,
								pin,
								remote.config["frequency"],
								double(remote.config["dutycycle"])/100,
								0, 0, // no header
								remote.config["one.on"],
								remote.config["zero.on"],
								remote.config["one.off"],
								remote.config["zero.off"],
								0, // no trailer yet
								pre_data_bits,
								remote.config["pre_data_bits"]))
					{
						std::cerr << "Failed to prepare pre_data" <<std::endl;
						continue;
					}
				}
				// insert the actual data
				if (irSlingPrepare(irSignal, &pulseCount,
							pin,
							remote.config["frequency"],
							double(remote.config["dutycycle"])/100,
							0, 0, /* no header */
							remote.config["one.on"],
							remote.config["zero.on"],
							remote.config["one.off"],
							remote.config["zero.off"],
							0, // no trailer
							code->second.c_str(),
							remote.config["bits"]))
				{
					std::cerr << "Failed to prepare signal" <<std::endl;
					continue;
				}
				// insert any trailer as required
				// both trailer data and ptrail
				int post_len = (3+remote.config["post_data_bits"])/4;
				char post_data_bits[post_len+4];
				if ((remote.config.find("post_data") != remote.config.end()) && (post_len > 0))
				{
					snprintf(post_data_bits,post_len+3,"0x%0*x",post_len,remote.config["post_data"]);
				}
				if (irSlingPrepare(irSignal, &pulseCount,
							pin,
							remote.config["frequency"],
							double(remote.config["dutycycle"])/100,
							0, 0, /* no header */
							remote.config["one.on"],
							remote.config["zero.on"],
							remote.config["one.off"],
							remote.config["zero.off"],
							remote.config["ptrail"],
							post_data_bits,
							remote.config["post_data_bits"]))
				{
					std::cerr << "Failed to prepare post_data" <<std::endl;
					continue;
				}
			}
			else if (remote.flags.find("RC5") != remote.flags.end())
			{
				std::string pattern("0x");
				if (remote.config.find("pre_data") != remote.config.end())
				{
					// TODO support pre_data_bits not modulo 4
					char xx[50];
					snprintf(xx,sizeof(xx),"%0*x",(3+remote.config["bits"])/4,remote.config["pre_data"]);
					pattern += xx;
				}
				pattern.append(code->second);
				//std::cout << "pattern is "<<pattern<<std::endl;
				if (irSlingPrepareRC5(irSignal, &pulseCount,
							pin,
							remote.config["frequency"],
							double(remote.config["dutycycle"])/100,
							remote.config["one.on"] + remote.config["one.off"],
							pattern.c_str(),
							remote.config["bits"] + remote.config["pre_data_bits"]))
				{
					std::cerr << "Failed to prepare signal" <<std::endl;
					continue;
				}
			}
		}

		int gap = remote.config["gap"];
		if (remote.config.find("repeat_gap") != remote.config.end())
		{
			gap = remote.config["repeat_gap"];
		}

		for(int i=0 ; i<=remote.config["min_repeat"]; ++i)
		{
			transmitWave(irSignal, pulseCount);
			// only delay if repeating this press
			if (i < remote.config["min_repeat"])
			{
				usleep(gap);
			}
		}

		// only delay if more buttons
		if (c < (argc-1))
		{
			usleep(remote.config["gap"]);
		}
	}

	transmitWavePost();
	return result;
}

