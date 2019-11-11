#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <iostream>
#include <getopt.h>
#include <unistd.h>
#include "irslinger.h"

// TODO
// support raw
// support bit counts not %4
// support multiple concurrent remotes

// LIRC config not supported:
// http://www.lirc.org/html/lircd.conf.html
// include
// manual_sort
// suppress_repeat
// flags RC6 / RC-MM / REVERSE / NO_HEAD_REP / NO_FOOT_REP / CONST_LENGTH / RAW_CODES / REPEAT_HEADER
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
		};
		std::unordered_map<std::string,int> config;
		std::unordered_map<std::string,std::string> codes;
		std::unordered_set<std::string> flags;
};
std::unordered_map<std::string, remote_config> remotes;
bool in_codes = false;
std::string remotename = "default";


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
	if (tokens.empty()) { return 0; }
	if (tokens[0].at(0) == '#') { return 0; }
	// find number of valid tokens
	size_t i=1;
	size_t maxtoken = tokens.size()-1;
	while (i < tokens.size()) {
		if (tokens[i].at(0) == '#') {
			maxtoken = i-1;
			break;
		}
		++i;
	}
	// all lines in >=2 tokens
	if (maxtoken == 0) {
		std::cerr << "Invalid line: "<<line<<std::endl;
		return -1;
	}

	char xx[50];
	// special case config
	if (tokens[0] == "begin" && tokens[1] == "remote") {
		// no action required
		return 0;
	}
	if (tokens[0] == "end" && tokens[1] == "remote") {
		// no action required
		return 0;
	}
	if (tokens[0] == "name") {
		remotename = tokens[1];
		return 0;
	}

	remote_config &remote(remotes[remotename]);

	if (tokens[0] == "flags") {
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
		if (tokens[0] == "header") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["header.on"] = std::stoi(tokens[1]);
			remote.config["header.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "one") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["one.on"] = std::stoi(tokens[1]);
			remote.config["one.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "zero") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["zero.on"] = std::stoi(tokens[1]);
			remote.config["zero.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "repeat") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			remote.config["repeat.on"] = std::stoi(tokens[1]);
			remote.config["repeat.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "begin" && tokens[1] == "codes") {
			in_codes = true;
			return 0;
		}
		if (tokens[0] == "end" && tokens[1] == "codes") {
			in_codes = false;
			return 0;
		}
		if (in_codes) {
			// TODO what if bits is not multiple of 4?
			snprintf(xx,sizeof(xx),"%0*x",(3+remote.config["bits"])/4,std::stoi(tokens[1],nullptr,0));
			remote.codes[tokens[0]] = std::string(xx);
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

	while (configfile.good()) {
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
	std::string thisremote = "";
	while( ( c = getopt (argc, argv, "r:dp:f:") ) != -1 ) 
	{
		switch(c)
		{
			case 'd':
				dumpconfig = true;
				break;
			case 'f':
				if(optarg) configfile = optarg;
				break;
			case 'p':
				if(optarg) pin = std::atoi(optarg) ;
				break;
			case 'r':
				if(optarg) thisremote = optarg;
				break;
		}			    
	}
	if (parse_config(configfile)) {
		std::cerr<<"Failed to parse config"<<std::endl;
		exit(1);
	}

	// if only one remote defined then use that
	if ((remotes.size() == 1) && (thisremote.size() == 0))
	{
		thisremote = remotes.begin()->first;
	}
	if (remotes.find(thisremote) == remotes.end())
	{
		std::cerr << "Remote " << thisremote << " does not exist" << std::endl;
		exit(1);
	}
	// TODO move this inside the loop so that can send commands to multiple remotes
	remote_config &remote(remotes[thisremote]);

	if (dumpconfig) {
		std::cout <<"Config for remote "<<thisremote<<std::endl;
		for (auto k:remote.config)
		{
			std::cout << "config: "<<k.first<<" "<<k.second<<std::endl;
		}
		for (auto k:remote.codes)
		{
			std::cout << "code  : "<<k.first<<" "<<k.second<<std::endl;
		}
	}

	int result = -1;
	transmitWavePre(pin);
	for (c=optind ; c < argc ; ++c)
	{

		std::unordered_map<std::string,std::string>::iterator code = remote.codes.find(argv[c]);
		if (code == remote.codes.end()) {
			std::cerr << "Button \"" << argv[c] << "\" is unknown." << std::endl;
			result |= 1;
			continue;
		}
		if (result == -1) { result = 0; }

		std::string pattern("0x");
		if (remote.config.find("pre_data") != remote.config.end()) {
			char xx[50];
			snprintf(xx,sizeof(xx),"%0*x",(3+remote.config["bits"])/4,remote.config["pre_data"]);
			pattern += xx;
		}
		pattern.append(code->second);
		int gap = remote.config["gap"];
		if (remote.config.find("repeat_gap") != remote.config.end()) {
			gap = remote.config["repeat_gap"];
		}
		//std::cout << "pattern is "<<pattern<<std::endl;
		gpioPulse_t irSignal[MAX_PULSES];
		unsigned int pulseCount = 0;
		if (remote.flags.find("SPACE_ENC") != remote.flags.end())
		{
			// nec type
			if (irSlingPrepare(irSignal, &pulseCount,
					pin,
					remote.config["frequency"],
					double(remote.config["dutycycle"])/100,
					remote.config["header.on"],
					remote.config["header.off"],
					remote.config["one.on"],
					remote.config["zero.on"],
					remote.config["one.off"],
					remote.config["zero.off"],
					remote.config["ptrail"],
					pattern.c_str(),
					remote.config["bits"] + remote.config["pre_data_bits"]))
			{
				std::cerr << "Failed to prepare signal" <<std::endl;
				continue;
			}
		} else if (remote.flags.find("RC5") != remote.flags.end())
		{
			// nec type
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

		for(int i=0 ; i<=remote.config["min_repeat"]; ++i) {
			transmitWave(irSignal, pulseCount);
			// only delay if repeating this press
			if (i < remote.config["min_repeat"]) {
				usleep(gap);
			}
		}

		// only delay if more buttons
		if (c < (argc-1)) {
			usleep(remote.config["gap"]);
		}
	}

	transmitWavePost();
	return result;
}

