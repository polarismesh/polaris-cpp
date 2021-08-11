#include "yaml-cpp/yaml.h"
#include "yaml-cpp/eventhandler.h"
#include <fstream>
#include <iostream>
#include <vector>

struct Params {
	bool hasFile;
	std::string fileName;
};

Params ParseArgs(int argc, char **argv) {
	Params p;

	std::vector<std::string> args(argv + 1, argv + argc);
	
	return p;
}

class NullEventHandler: public YAML_0_3::EventHandler
{
public:
	virtual void OnDocumentStart(const YAML_0_3::Mark&) {}
	virtual void OnDocumentEnd() {}
	
	virtual void OnNull(const YAML_0_3::Mark&, YAML_0_3::anchor_t) {}
	virtual void OnAlias(const YAML_0_3::Mark&, YAML_0_3::anchor_t) {}
	virtual void OnScalar(const YAML_0_3::Mark&, const std::string&, YAML_0_3::anchor_t, const std::string&) {}
	
	virtual void OnSequenceStart(const YAML_0_3::Mark&, const std::string&, YAML_0_3::anchor_t) {}
	virtual void OnSequenceEnd() {}
	
	virtual void OnMapStart(const YAML_0_3::Mark&, const std::string&, YAML_0_3::anchor_t) {}
	virtual void OnMapEnd() {}
};

void parse(std::istream& input)
{
	try {
		YAML_0_3::Parser parser(input);
		YAML_0_3::Node doc;
		while(parser.GetNextDocument(doc)) {
			YAML_0_3::Emitter emitter;
			emitter << doc;
			std::cout << emitter.c_str() << "\n";
		}
	} catch(const YAML_0_3::Exception& e) {
		std::cerr << e.what() << "\n";
	}
}

int main(int argc, char **argv)
{
	Params p = ParseArgs(argc, argv);

	if(argc > 1) {
		std::ifstream fin;
		fin.open(argv[1]);
		parse(fin);
	} else {
		parse(std::cin);
	}

	return 0;
}
