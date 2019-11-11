
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <iostream>
#include <getopt.h>
#include <unistd.h>
#include "irslinger.h"

std::map<std::string,int> config;
std::map<std::string,std::string> codes;
bool in_codes = false;

const char * delimiters = " \t\r\n";
int handle_line(const std::string & line)
{
	size_t start = line.find_first_not_of(delimiters);
	size_t end = start;
	std::vector<std::string> tokens;

	while (start != std::string::npos){
		// Find next occurence of delimiter
		end = line.find_first_of(delimiters, start);
		// Push back the token found into vector
		tokens.push_back(line.substr(start, end-start));
		// Skip all occurences of the delimiter to find new start
		start = line.find_first_not_of(delimiters, end);
	}
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
		// ignore
		return 0;
	}
	if (tokens[0] == "end" && tokens[1] == "remote") {
		// ignore
		return 0;
	}
	if (tokens[0] == "name") {
		// ignore
		return 0;
	}
	if (tokens[0] == "flags") {
		// ignore for now, TODO need to check for rc5 etc.
		return 0;
	}
	try
	{
		if (tokens[0] == "header") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			config["header.on"] = std::stoi(tokens[1]);
			config["header.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "one") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			config["one.on"] = std::stoi(tokens[1]);
			config["one.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "zero") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			config["zero.on"] = std::stoi(tokens[1]);
			config["zero.off"] = std::stoi(tokens[2]);
			return 0;
		}
		if (tokens[0] == "repeat") {
			if (maxtoken == 1) {
				std::cerr << "Missing off time in line: "<<line<<std::endl;
				return -1;
			}
			config["repeat.on"] = std::stoi(tokens[1]);
			config["repeat.off"] = std::stoi(tokens[2]);
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
			snprintf(xx,sizeof(xx),"%0*x",(3+config["bits"])/4,std::stoi(tokens[1],nullptr,0));
			codes[tokens[0]] = std::string(xx);
			return 0;
		}

		// just save the numeric value
		config[tokens[0]] = std::stoi(tokens[1],nullptr,0);
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
	while( ( c = getopt (argc, argv, "dp:f:") ) != -1 ) 
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
		}			    
	}
	if (parse_config(configfile)) {
		std::cerr<<"Failed to parse config"<<std::endl;
		exit(1);
	}

	if (dumpconfig) {
		for (auto k:config)
		{
			std::cout << "config: "<<k.first<<" "<<k.second<<std::endl;
		}
		for (auto k:codes)
		{
			std::cout << "code  : "<<k.first<<" "<<k.second<<std::endl;
		}
	}
	int result = -1;
	transmitWavePre(pin);
	for (c=optind ; c < argc ; ++c)
	{
		std::map<std::string,std::string>::iterator code = codes.find(argv[c]);
		if (code == codes.end()) {
			std::cerr << "Button \"" << argv[c] << "\" is unknown." << std::endl;
			result |= 1;
			continue;
		}
		if (result == -1) { result = 0; }

		std::string pattern("0x");
		if (config.find("pre_data") != config.end()) {
			char xx[50];
			snprintf(xx,sizeof(xx),"%0*x",(3+config["bits"])/4,config["pre_data"]);
			pattern += xx;
		}
		pattern.append(code->second);
		int gap = config["gap"];
		if (config.find("repeat_gap") != config.end()) {
			gap = config["repeat_gap"];
		}
		//std::cout << "pattern is "<<pattern<<std::endl;
		gpioPulse_t irSignal[MAX_PULSES];
		unsigned int pulseCount = 0;
		if (irSlingPrepare(irSignal, &pulseCount,
				pin,
				config["frequency"],
				double(config["dutycycle"])/100,
				config["header.on"],
				config["header.off"],
				config["one.on"],
				config["zero.on"],
				config["one.off"],
				config["zero.off"],
				config["ptrail"],
				pattern.c_str(),
				config["bits"] + config["pre_data_bits"])) {
			std::cerr << "Failed to prepare signal" <<std::endl;
			continue;
		}

		for(int i=0 ; i<=config["min_repeat"]; ++i) {
			transmitWave(irSignal, pulseCount);
			// only delay if repeating this press
			if (i < config["min_repeat"]) {
				usleep(gap);
			}
		}

		// only delay if more buttons
		if (c < (argc-1)) {
			usleep(config["gap"]);
		}
	}

	transmitWavePost();
	return result;
}

