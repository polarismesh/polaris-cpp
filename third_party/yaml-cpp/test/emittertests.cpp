#include "tests.h"
#include "yaml-cpp/yaml.h"
#include <iostream>

namespace Test
{
	namespace Emitter {
		////////////////////////////////////////////////////////////////////////////////////////////////////////
		// correct emitting

		void SimpleScalar(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << "Hello, World!";
			desiredOutput = "Hello, World!";
		}
		
		void SimpleSeq(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginSeq;
			out << "eggs";
			out << "bread";
			out << "milk";
			out << YAML_0_3::EndSeq;

			desiredOutput = "- eggs\n- bread\n- milk";
		}
		
		void SimpleFlowSeq(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::Flow;
			out << YAML_0_3::BeginSeq;
			out << "Larry";
			out << "Curly";
			out << "Moe";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "[Larry, Curly, Moe]";
		}
		
		void EmptyFlowSeq(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::Flow;
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "[]";
		}
		
		void NestedBlockSeq(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginSeq;
			out << "item 1";
			out << YAML_0_3::BeginSeq << "subitem 1" << "subitem 2" << YAML_0_3::EndSeq;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- item 1\n-\n  - subitem 1\n  - subitem 2";
		}
		
		void NestedFlowSeq(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginSeq;
			out << "one";
			out << YAML_0_3::Flow << YAML_0_3::BeginSeq << "two" << "three" << YAML_0_3::EndSeq;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- one\n- [two, three]";
		}

		void SimpleMap(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "name";
			out << YAML_0_3::Value << "Ryan Braun";
			out << YAML_0_3::Key << "position";
			out << YAML_0_3::Value << "3B";
			out << YAML_0_3::EndMap;

			desiredOutput = "name: Ryan Braun\nposition: 3B";
		}
		
		void SimpleFlowMap(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::Flow;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "shape";
			out << YAML_0_3::Value << "square";
			out << YAML_0_3::Key << "color";
			out << YAML_0_3::Value << "blue";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "{shape: square, color: blue}";
		}
		
		void MapAndList(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "name";
			out << YAML_0_3::Value << "Barack Obama";
			out << YAML_0_3::Key << "children";
			out << YAML_0_3::Value << YAML_0_3::BeginSeq << "Sasha" << "Malia" << YAML_0_3::EndSeq;
			out << YAML_0_3::EndMap;

			desiredOutput = "name: Barack Obama\nchildren:\n  - Sasha\n  - Malia";
		}
		
		void ListAndMap(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginSeq;
			out << "item 1";
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "pens" << YAML_0_3::Value << 8;
			out << YAML_0_3::Key << "pencils" << YAML_0_3::Value << 14;
			out << YAML_0_3::EndMap;
			out << "item 2";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- item 1\n- pens: 8\n  pencils: 14\n- item 2";
		}

		void NestedBlockMap(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "name";
			out << YAML_0_3::Value << "Fred";
			out << YAML_0_3::Key << "grades";
			out << YAML_0_3::Value;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "algebra" << YAML_0_3::Value << "A";
			out << YAML_0_3::Key << "physics" << YAML_0_3::Value << "C+";
			out << YAML_0_3::Key << "literature" << YAML_0_3::Value << "B";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndMap;
			
			desiredOutput = "name: Fred\ngrades:\n  algebra: A\n  physics: C+\n  literature: B";
		}

		void NestedFlowMap(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::Flow;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "name";
			out << YAML_0_3::Value << "Fred";
			out << YAML_0_3::Key << "grades";
			out << YAML_0_3::Value;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "algebra" << YAML_0_3::Value << "A";
			out << YAML_0_3::Key << "physics" << YAML_0_3::Value << "C+";
			out << YAML_0_3::Key << "literature" << YAML_0_3::Value << "B";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndMap;
			
			desiredOutput = "{name: Fred, grades: {algebra: A, physics: C+, literature: B}}";
		}
		
		void MapListMix(YAML_0_3::Emitter& out, std::string& desiredOutput) {
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "name";
			out << YAML_0_3::Value << "Bob";
			out << YAML_0_3::Key << "position";
			out << YAML_0_3::Value;
			out << YAML_0_3::Flow << YAML_0_3::BeginSeq << 2 << 4 << YAML_0_3::EndSeq;
			out << YAML_0_3::Key << "invincible" << YAML_0_3::Value << YAML_0_3::OnOffBool << false;
			out << YAML_0_3::EndMap;
			
			desiredOutput = "name: Bob\nposition: [2, 4]\ninvincible: off";
		}

		void SimpleLongKey(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::LongKey;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "height";
			out << YAML_0_3::Value << "5'9\"";
			out << YAML_0_3::Key << "weight";
			out << YAML_0_3::Value << 145;
			out << YAML_0_3::EndMap;
			
			desiredOutput = "? height\n: 5'9\"\n? weight\n: 145";
		}
		
		void SingleLongKey(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "age";
			out << YAML_0_3::Value << "24";
			out << YAML_0_3::LongKey << YAML_0_3::Key << "height";
			out << YAML_0_3::Value << "5'9\"";
			out << YAML_0_3::Key << "weight";
			out << YAML_0_3::Value << 145;
			out << YAML_0_3::EndMap;
			
			desiredOutput = "age: 24\n? height\n: 5'9\"\nweight: 145";
		}
		
		void ComplexLongKey(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::LongKey;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << YAML_0_3::BeginSeq << 1 << 3 << YAML_0_3::EndSeq;
			out << YAML_0_3::Value << "monster";
			out << YAML_0_3::Key << YAML_0_3::Flow << YAML_0_3::BeginSeq << 2 << 0 << YAML_0_3::EndSeq;
			out << YAML_0_3::Value << "demon";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "?\n  - 1\n  - 3\n: monster\n? [2, 0]\n: demon";
		}

		void AutoLongKey(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << YAML_0_3::BeginSeq << 1 << 3 << YAML_0_3::EndSeq;
			out << YAML_0_3::Value << "monster";
			out << YAML_0_3::Key << YAML_0_3::Flow << YAML_0_3::BeginSeq << 2 << 0 << YAML_0_3::EndSeq;
			out << YAML_0_3::Value << "demon";
			out << YAML_0_3::Key << "the origin";
			out << YAML_0_3::Value << "angel";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "?\n  - 1\n  - 3\n: monster\n? [2, 0]\n: demon\nthe origin: angel";
		}
		
		void ScalarFormat(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << "simple scalar";
			out << YAML_0_3::SingleQuoted << "explicit single-quoted scalar";
			out << YAML_0_3::DoubleQuoted << "explicit double-quoted scalar";
			out << "auto-detected\ndouble-quoted scalar";
			out << "a non-\"auto-detected\" double-quoted scalar";
			out << YAML_0_3::Literal << "literal scalar\nthat may span\nmany, many\nlines and have \"whatever\" crazy\tsymbols that we like";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- simple scalar\n- 'explicit single-quoted scalar'\n- \"explicit double-quoted scalar\"\n- \"auto-detected\\x0adouble-quoted scalar\"\n- a non-\"auto-detected\" double-quoted scalar\n- |\n  literal scalar\n  that may span\n  many, many\n  lines and have \"whatever\" crazy\tsymbols that we like";
		}

		void AutoLongKeyScalar(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << YAML_0_3::Literal << "multi-line\nscalar";
			out << YAML_0_3::Value << "and its value";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "? |\n  multi-line\n  scalar\n: and its value";
		}
		
		void LongKeyFlowMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "simple key";
			out << YAML_0_3::Value << "and value";
			out << YAML_0_3::LongKey << YAML_0_3::Key << "long key";
			out << YAML_0_3::Value << "and its value";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "{simple key: and value, ? long key: and its value}";
		}

		void BlockMapAsKey(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "key" << YAML_0_3::Value << "value";
			out << YAML_0_3::Key << "next key" << YAML_0_3::Value << "next value";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::Value;
			out << "total value";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "?\n  key: value\n  next key: next value\n: total value";
		}
		
		void AliasAndAnchor(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Anchor("fred");
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "name" << YAML_0_3::Value << "Fred";
			out << YAML_0_3::Key << "age" << YAML_0_3::Value << 42;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::Alias("fred");
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- &fred\n  name: Fred\n  age: 42\n- *fred";
		}

		void AliasAndAnchorWithNull(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Anchor("fred") << YAML_0_3::Null;
			out << YAML_0_3::Alias("fred");
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- &fred ~\n- *fred";
		}
		
		void AliasAndAnchorInFlow(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginSeq;
			out << YAML_0_3::Anchor("fred");
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "name" << YAML_0_3::Value << "Fred";
			out << YAML_0_3::Key << "age" << YAML_0_3::Value << 42;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::Alias("fred");
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "[&fred {name: Fred, age: 42}, *fred]";
		}
		
		void SimpleVerbatimTag(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::VerbatimTag("!foo") << "bar";
			
			desiredOutput = "!<!foo> bar";
		}

		void VerbatimTagInBlockSeq(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::VerbatimTag("!foo") << "bar";
			out << "baz";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- !<!foo> bar\n- baz";
		}

		void VerbatimTagInFlowSeq(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginSeq;
			out << YAML_0_3::VerbatimTag("!foo") << "bar";
			out << "baz";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "[!<!foo> bar, baz]";
		}

		void VerbatimTagInFlowSeqWithNull(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginSeq;
			out << YAML_0_3::VerbatimTag("!foo") << YAML_0_3::Null;
			out << "baz";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "[!<!foo> ~, baz]";
		}

		void VerbatimTagInBlockMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << YAML_0_3::VerbatimTag("!foo") << "bar";
			out << YAML_0_3::Value << YAML_0_3::VerbatimTag("!waz") << "baz";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "!<!foo> bar: !<!waz> baz";
		}

		void VerbatimTagInFlowMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << YAML_0_3::VerbatimTag("!foo") << "bar";
			out << YAML_0_3::Value << "baz";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "{!<!foo> bar: baz}";
		}

		void VerbatimTagInFlowMapWithNull(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << YAML_0_3::VerbatimTag("!foo") << YAML_0_3::Null;
			out << YAML_0_3::Value << "baz";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "{!<!foo> ~: baz}";
		}
		
		void VerbatimTagWithEmptySeq(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::VerbatimTag("!foo") << YAML_0_3::BeginSeq << YAML_0_3::EndSeq;
			
			desiredOutput = "!<!foo>\n[]";
		}

		void VerbatimTagWithEmptyMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::VerbatimTag("!bar") << YAML_0_3::BeginMap << YAML_0_3::EndMap;
			
			desiredOutput = "!<!bar>\n{}";
		}

		void VerbatimTagWithEmptySeqAndMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::VerbatimTag("!foo") << YAML_0_3::BeginSeq << YAML_0_3::EndSeq;
			out << YAML_0_3::VerbatimTag("!bar") << YAML_0_3::BeginMap << YAML_0_3::EndMap;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- !<!foo>\n  []\n- !<!bar>\n  {}";
		}

		void ByKindTagWithScalar(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::DoubleQuoted << "12";
			out << "12";
			out << YAML_0_3::TagByKind << "12";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- \"12\"\n- 12\n- ! 12";
		}

		void LocalTagWithScalar(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::LocalTag("foo") << "bar";
			
			desiredOutput = "!foo bar";
		}

		void BadLocalTag(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			out << YAML_0_3::LocalTag("e!far") << "bar";
			
			desiredError = "invalid tag";
		}

		void ComplexDoc(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "receipt";
			out << YAML_0_3::Value << "Oz-Ware Purchase Invoice";
			out << YAML_0_3::Key << "date";
			out << YAML_0_3::Value << "2007-08-06";
			out << YAML_0_3::Key << "customer";
			out << YAML_0_3::Value;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "given";
			out << YAML_0_3::Value << "Dorothy";
			out << YAML_0_3::Key << "family";
			out << YAML_0_3::Value << "Gale";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::Key << "items";
			out << YAML_0_3::Value;
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "part_no";
			out << YAML_0_3::Value << "A4786";
			out << YAML_0_3::Key << "descrip";
			out << YAML_0_3::Value << "Water Bucket (Filled)";
			out << YAML_0_3::Key << "price";
			out << YAML_0_3::Value << 1.47;
			out << YAML_0_3::Key << "quantity";
			out << YAML_0_3::Value << 4;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "part_no";
			out << YAML_0_3::Value << "E1628";
			out << YAML_0_3::Key << "descrip";
			out << YAML_0_3::Value << "High Heeled \"Ruby\" Slippers";
			out << YAML_0_3::Key << "price";
			out << YAML_0_3::Value << 100.27;
			out << YAML_0_3::Key << "quantity";
			out << YAML_0_3::Value << 1;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndSeq;
			out << YAML_0_3::Key << "bill-to";
			out << YAML_0_3::Value << YAML_0_3::Anchor("id001");
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "street";
			out << YAML_0_3::Value << YAML_0_3::Literal << "123 Tornado Alley\nSuite 16";
			out << YAML_0_3::Key << "city";
			out << YAML_0_3::Value << "East Westville";
			out << YAML_0_3::Key << "state";
			out << YAML_0_3::Value << "KS";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::Key << "ship-to";
			out << YAML_0_3::Value << YAML_0_3::Alias("id001");
			out << YAML_0_3::EndMap;
			
			desiredOutput = "receipt: Oz-Ware Purchase Invoice\ndate: 2007-08-06\ncustomer:\n  given: Dorothy\n  family: Gale\nitems:\n  - part_no: A4786\n    descrip: Water Bucket (Filled)\n    price: 1.47\n    quantity: 4\n  - part_no: E1628\n    descrip: High Heeled \"Ruby\" Slippers\n    price: 100.27\n    quantity: 1\nbill-to: &id001\n  street: |\n    123 Tornado Alley\n    Suite 16\n  city: East Westville\n  state: KS\nship-to: *id001";
		}

		void STLContainers(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			std::vector <int> primes;
			primes.push_back(2);
			primes.push_back(3);
			primes.push_back(5);
			primes.push_back(7);
			primes.push_back(11);
			primes.push_back(13);
			out << YAML_0_3::Flow << primes;
			std::map <std::string, int> ages;
			ages["Daniel"] = 26;
			ages["Jesse"] = 24;
			out << ages;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- [2, 3, 5, 7, 11, 13]\n- Daniel: 26\n  Jesse: 24";
		}

		void SimpleComment(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "method";
			out << YAML_0_3::Value << "least squares" << YAML_0_3::Comment("should we change this method?");
			out << YAML_0_3::EndMap;
			
			desiredOutput = "method: least squares  # should we change this method?";
		}

		void MultiLineComment(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << "item 1" << YAML_0_3::Comment("really really long\ncomment that couldn't possibly\nfit on one line");
			out << "item 2";
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- item 1  # really really long\n          # comment that couldn't possibly\n          # fit on one line\n- item 2";
		}

		void ComplexComments(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::LongKey << YAML_0_3::Key << "long key" << YAML_0_3::Comment("long key");
			out << YAML_0_3::Value << "value";
			out << YAML_0_3::EndMap;
			
			desiredOutput = "? long key  # long key\n: value";
		}
		
		void InitialComment(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Comment("A comment describing the purpose of the file.");
			out << YAML_0_3::BeginMap << YAML_0_3::Key << "key" << YAML_0_3::Value << "value" << YAML_0_3::EndMap;
			
			desiredOutput = "# A comment describing the purpose of the file.\nkey: value";
		}

		void InitialCommentWithDocIndicator(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginDoc << YAML_0_3::Comment("A comment describing the purpose of the file.");
			out << YAML_0_3::BeginMap << YAML_0_3::Key << "key" << YAML_0_3::Value << "value" << YAML_0_3::EndMap;
			
			desiredOutput = "---\n# A comment describing the purpose of the file.\nkey: value";
		}

		void CommentInFlowSeq(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginSeq << "foo" << YAML_0_3::Comment("foo!") << "bar" << YAML_0_3::EndSeq;
			
			desiredOutput = "[foo  # foo!\n, bar]";
		}

		void CommentInFlowMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "foo" << YAML_0_3::Value << "foo value";
			out << YAML_0_3::Key << "bar" << YAML_0_3::Value << "bar value" << YAML_0_3::Comment("bar!");
			out << YAML_0_3::Key << "baz" << YAML_0_3::Value << "baz value" << YAML_0_3::Comment("baz!");
			out << YAML_0_3::EndMap;
			
			desiredOutput = "{foo: foo value, bar: bar value  # bar!\n, baz: baz value  # baz!\n}";
		}

		void Indentation(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Indent(4);
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "key 1" << YAML_0_3::Value << "value 1";
			out << YAML_0_3::Key << "key 2" << YAML_0_3::Value << YAML_0_3::BeginSeq << "a" << "b" << "c" << YAML_0_3::EndSeq;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "-   key 1: value 1\n    key 2:\n        - a\n        - b\n        - c";
		}

		void SimpleGlobalSettings(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out.SetIndent(4);
			out.SetMapFormat(YAML_0_3::LongKey);

			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "key 1" << YAML_0_3::Value << "value 1";
			out << YAML_0_3::Key << "key 2" << YAML_0_3::Value << YAML_0_3::Flow << YAML_0_3::BeginSeq << "a" << "b" << "c" << YAML_0_3::EndSeq;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "-   ? key 1\n    : value 1\n    ? key 2\n    : [a, b, c]";
		}
		
		void ComplexGlobalSettings(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Block;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "key 1" << YAML_0_3::Value << "value 1";
			out << YAML_0_3::Key << "key 2" << YAML_0_3::Value;
			out.SetSeqFormat(YAML_0_3::Flow);
			out << YAML_0_3::BeginSeq << "a" << "b" << "c" << YAML_0_3::EndSeq;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << YAML_0_3::BeginSeq << 1 << 2 << YAML_0_3::EndSeq;
			out << YAML_0_3::Value << YAML_0_3::BeginMap << YAML_0_3::Key << "a" << YAML_0_3::Value << "b" << YAML_0_3::EndMap;
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- key 1: value 1\n  key 2: [a, b, c]\n- ? [1, 2]\n  :\n    a: b";
		}

		void Null(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Null;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "null value" << YAML_0_3::Value << YAML_0_3::Null;
			out << YAML_0_3::Key << YAML_0_3::Null << YAML_0_3::Value << "null key";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- ~\n- null value: ~\n  ~: null key";
		}
		
		void EscapedUnicode(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::EscapeNonAscii << "\x24 \xC2\xA2 \xE2\x82\xAC \xF0\xA4\xAD\xA2";
			
			desiredOutput = "\"$ \\xa2 \\u20ac \\U00024b62\"";
		}
		
		void Unicode(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << "\x24 \xC2\xA2 \xE2\x82\xAC \xF0\xA4\xAD\xA2";
			desiredOutput = "\x24 \xC2\xA2 \xE2\x82\xAC \xF0\xA4\xAD\xA2";
		}
		
		void DoubleQuotedUnicode(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::DoubleQuoted << "\x24 \xC2\xA2 \xE2\x82\xAC \xF0\xA4\xAD\xA2";
			desiredOutput = "\"\x24 \xC2\xA2 \xE2\x82\xAC \xF0\xA4\xAD\xA2\"";
		}
		
		struct Foo {
			Foo(): x(0) {}
			Foo(int x_, const std::string& bar_): x(x_), bar(bar_) {}
			
			int x;
			std::string bar;
		};
		
		YAML_0_3::Emitter& operator << (YAML_0_3::Emitter& out, const Foo& foo) {
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "x" << YAML_0_3::Value << foo.x;
			out << YAML_0_3::Key << "bar" << YAML_0_3::Value << foo.bar;
			out << YAML_0_3::EndMap;
			return out;
		}
		
		void UserType(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << Foo(5, "hello");
			out << Foo(3, "goodbye");
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- x: 5\n  bar: hello\n- x: 3\n  bar: goodbye";
		}
		
		void UserTypeInContainer(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			std::vector<Foo> fv;
			fv.push_back(Foo(5, "hello"));
			fv.push_back(Foo(3, "goodbye"));
			out << fv;
			
			desiredOutput = "- x: 5\n  bar: hello\n- x: 3\n  bar: goodbye";
		}
		
		template <typename T>
		YAML_0_3::Emitter& operator << (YAML_0_3::Emitter& out, const T *v) {
			if(v)
				out << *v;
			else
				out << YAML_0_3::Null;
			return out;
		}
		
		void PointerToInt(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			int foo = 5;
			int *bar = &foo;
			int *baz = 0;
			out << YAML_0_3::BeginSeq;
			out << bar << baz;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- 5\n- ~";
		}

		void PointerToUserType(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			Foo foo(5, "hello");
			Foo *bar = &foo;
			Foo *baz = 0;
			out << YAML_0_3::BeginSeq;
			out << bar << baz;
			out << YAML_0_3::EndSeq;
			
			desiredOutput = "- x: 5\n  bar: hello\n- ~";
		}
		
		void NewlineAtEnd(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << "Hello" << YAML_0_3::Newline << YAML_0_3::Newline;
			desiredOutput = "Hello\n\n";
		}
		
		void NewlineInBlockSequence(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << "a" << YAML_0_3::Newline << "b" << "c" << YAML_0_3::Newline << "d";
			out << YAML_0_3::EndSeq;
			desiredOutput = "- a\n\n- b\n- c\n\n- d";
		}
		
		void NewlineInFlowSequence(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginSeq;
			out << "a" << YAML_0_3::Newline << "b" << "c" << YAML_0_3::Newline << "d";
			out << YAML_0_3::EndSeq;
			desiredOutput = "[a\n, b, c\n, d]";
		}

		void NewlineInBlockMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "a" << YAML_0_3::Value << "foo" << YAML_0_3::Newline;
			out << YAML_0_3::Key << "b" << YAML_0_3::Newline << YAML_0_3::Value << "bar";
			out << YAML_0_3::LongKey << YAML_0_3::Key << "c" << YAML_0_3::Newline << YAML_0_3::Value << "car";
			out << YAML_0_3::EndMap;
			desiredOutput = "a: foo\n\nb: bar\n? c\n\n: car";
		}
		
		void NewlineInFlowMap(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "a" << YAML_0_3::Value << "foo" << YAML_0_3::Newline;
			out << YAML_0_3::Key << "b" << YAML_0_3::Value << "bar";
			out << YAML_0_3::EndMap;
			desiredOutput = "{a: foo\n, b: bar}";
		}
		
		void LotsOfNewlines(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << "a" << YAML_0_3::Newline;
			out << YAML_0_3::BeginSeq;
			out << "b" << "c" << YAML_0_3::Newline;
			out << YAML_0_3::EndSeq;
			out << YAML_0_3::Newline;
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Newline << YAML_0_3::Key << "d" << YAML_0_3::Value << YAML_0_3::Newline << "e";
			out << YAML_0_3::LongKey << YAML_0_3::Key << "f" << YAML_0_3::Newline << YAML_0_3::Value << "foo";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndSeq;
			desiredOutput = "- a\n\n-\n  - b\n  - c\n\n\n-\n  d: e\n  ? f\n\n  : foo";
		}
		
		void Binary(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Binary(reinterpret_cast<const unsigned char*>("Hello, World!"), 13);
			desiredOutput = "!!binary \"SGVsbG8sIFdvcmxkIQ==\"";
		}

		void LongBinary(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Binary(reinterpret_cast<const unsigned char*>("Man is distinguished, not only by his reason, but by this singular passion from other animals, which is a lust of the mind, that by a perseverance of delight in the continued and indefatigable generation of knowledge, exceeds the short vehemence of any carnal pleasure.\n"), 270);
			desiredOutput = "!!binary \"TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dCBieSB0aGlzIHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIGx1c3Qgb2YgdGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aGUgY29udGludWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleGNlZWRzIHRoZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4K\"";
		}

		void EmptyBinary(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Binary(reinterpret_cast<const unsigned char *>(""), 0);
			desiredOutput = "!!binary \"\"";
		}
		
		void ColonAtEndOfScalar(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << "a:";
			desiredOutput = "\"a:\"";
		}

		void ColonAsScalar(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "apple" << YAML_0_3::Value << ":";
			out << YAML_0_3::Key << "banana" << YAML_0_3::Value << ":";
			out << YAML_0_3::EndMap;
			desiredOutput = "apple: \":\"\nbanana: \":\"";
		}
		
		void ColonAtEndOfScalarInFlow(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::Flow << YAML_0_3::BeginMap << YAML_0_3::Key << "C:" << YAML_0_3::Value << "C:" << YAML_0_3::EndMap;
			desiredOutput = "{\"C:\": \"C:\"}";
		}
		
		void BoolFormatting(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::TrueFalseBool << YAML_0_3::UpperCase << true;
			out << YAML_0_3::TrueFalseBool << YAML_0_3::CamelCase << true;
			out << YAML_0_3::TrueFalseBool << YAML_0_3::LowerCase << true;
			out << YAML_0_3::TrueFalseBool << YAML_0_3::UpperCase << false;
			out << YAML_0_3::TrueFalseBool << YAML_0_3::CamelCase << false;
			out << YAML_0_3::TrueFalseBool << YAML_0_3::LowerCase << false;
			out << YAML_0_3::YesNoBool << YAML_0_3::UpperCase << true;
			out << YAML_0_3::YesNoBool << YAML_0_3::CamelCase << true;
			out << YAML_0_3::YesNoBool << YAML_0_3::LowerCase << true;
			out << YAML_0_3::YesNoBool << YAML_0_3::UpperCase << false;
			out << YAML_0_3::YesNoBool << YAML_0_3::CamelCase << false;
			out << YAML_0_3::YesNoBool << YAML_0_3::LowerCase << false;
			out << YAML_0_3::OnOffBool << YAML_0_3::UpperCase << true;
			out << YAML_0_3::OnOffBool << YAML_0_3::CamelCase << true;
			out << YAML_0_3::OnOffBool << YAML_0_3::LowerCase << true;
			out << YAML_0_3::OnOffBool << YAML_0_3::UpperCase << false;
			out << YAML_0_3::OnOffBool << YAML_0_3::CamelCase << false;
			out << YAML_0_3::OnOffBool << YAML_0_3::LowerCase << false;
			out << YAML_0_3::ShortBool << YAML_0_3::UpperCase << true;
			out << YAML_0_3::ShortBool << YAML_0_3::CamelCase << true;
			out << YAML_0_3::ShortBool << YAML_0_3::LowerCase << true;
			out << YAML_0_3::ShortBool << YAML_0_3::UpperCase << false;
			out << YAML_0_3::ShortBool << YAML_0_3::CamelCase << false;
			out << YAML_0_3::ShortBool << YAML_0_3::LowerCase << false;
			out << YAML_0_3::EndSeq;
			desiredOutput =
			"- TRUE\n- True\n- true\n- FALSE\n- False\n- false\n"
			"- YES\n- Yes\n- yes\n- NO\n- No\n- no\n"
			"- ON\n- On\n- on\n- OFF\n- Off\n- off\n"
			"- Y\n- Y\n- y\n- N\n- N\n- n";
		}
		
		void DocStartAndEnd(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginDoc;
			out << YAML_0_3::BeginSeq << 1 << 2 << 3 << YAML_0_3::EndSeq;
			out << YAML_0_3::BeginDoc;
			out << "Hi there!";
			out << YAML_0_3::EndDoc;
			out << YAML_0_3::EndDoc;
			out << YAML_0_3::EndDoc;
			out << YAML_0_3::BeginDoc;
			out << YAML_0_3::VerbatimTag("foo") << "bar";
			desiredOutput = "---\n- 1\n- 2\n- 3\n---\nHi there!\n...\n...\n...\n---\n!<foo> bar";
		}
		
		void ImplicitDocStart(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << "Hi";
			out << "Bye";
			out << "Oops";
			desiredOutput = "Hi\n---\nBye\n---\nOops";
		}
		
		void EmptyString(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "key" << YAML_0_3::Value << "";
			out << YAML_0_3::EndMap;
			desiredOutput = "key: \"\"";
		}
		
		void SingleChar(YAML_0_3::Emitter& out, std::string& desiredOutput)
		{
			out << YAML_0_3::BeginSeq;
			out << 'a';
			out << ':';
			out << (char)0x10;
			out << '\n';
			out << ' ';
			out << '\t';
			out << YAML_0_3::EndSeq;
			desiredOutput = "- a\n- \":\"\n- \"\\x10\"\n- \"\\n\"\n- \" \"\n- \"\\t\"";
		}
        
        void DefaultPrecision(YAML_0_3::Emitter& out, std::string& desiredOutput)
        {
            out << YAML_0_3::BeginSeq;
            out << 1.234f;
            out << 3.14159265358979;
            out << YAML_0_3::EndSeq;
            desiredOutput = "- 1.234\n- 3.14159265358979";
        }

        void SetPrecision(YAML_0_3::Emitter& out, std::string& desiredOutput)
        {
            out << YAML_0_3::BeginSeq;
            out << YAML_0_3::FloatPrecision(3) << 1.234f;
            out << YAML_0_3::DoublePrecision(6) << 3.14159265358979;
            out << YAML_0_3::EndSeq;
            desiredOutput = "- 1.23\n- 3.14159";
        }
        
        void DashInBlockContext(YAML_0_3::Emitter& out, std::string& desiredOutput)
        {
            out << YAML_0_3::BeginMap;
            out << YAML_0_3::Key << "key" << YAML_0_3::Value << "-";
            out << YAML_0_3::EndMap;
            desiredOutput = "key: \"-\"";
        }
        
        void HexAndOct(YAML_0_3::Emitter& out, std::string& desiredOutput)
        {
            out << YAML_0_3::Flow << YAML_0_3::BeginSeq;
            out << 31;
            out << YAML_0_3::Hex << 31;
            out << YAML_0_3::Oct << 31;
            out << YAML_0_3::EndSeq;
            desiredOutput = "[31, 0x1f, 037]";
        }
        
        ////////////////////////////////////////////////////////////////////////////////////////////////////////
		// incorrect emitting
		
		void ExtraEndSeq(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::UNEXPECTED_END_SEQ;

			out << YAML_0_3::BeginSeq;
			out << "Hello";
			out << "World";
			out << YAML_0_3::EndSeq;
			out << YAML_0_3::EndSeq;
		}

		void ExtraEndMap(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::UNEXPECTED_END_MAP;
			
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "Hello" << YAML_0_3::Value << "World";
			out << YAML_0_3::EndMap;
			out << YAML_0_3::EndMap;
		}

		void BadSingleQuoted(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::SINGLE_QUOTED_CHAR;
			
			out << YAML_0_3::SingleQuoted << "Hello\nWorld";
		}

		void InvalidAnchor(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::INVALID_ANCHOR;
			
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Anchor("new\nline") << "Test";
			out << YAML_0_3::EndSeq;
		}
		
		void InvalidAlias(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::INVALID_ALIAS;
			
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Alias("new\nline");
			out << YAML_0_3::EndSeq;
		}

		void MissingKey(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::EXPECTED_KEY_TOKEN;
			
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "key" << YAML_0_3::Value << "value";
			out << "missing key" << YAML_0_3::Value << "value";
			out << YAML_0_3::EndMap;
		}
		
		void MissingValue(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::EXPECTED_VALUE_TOKEN;
			
			out << YAML_0_3::BeginMap;
			out << YAML_0_3::Key << "key" << "value";
			out << YAML_0_3::EndMap;
		}

		void UnexpectedKey(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::UNEXPECTED_KEY_TOKEN;
			
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Key << "hi";
			out << YAML_0_3::EndSeq;
		}
		
		void UnexpectedValue(YAML_0_3::Emitter& out, std::string& desiredError)
		{
			desiredError = YAML_0_3::ErrorMsg::UNEXPECTED_VALUE_TOKEN;
			
			out << YAML_0_3::BeginSeq;
			out << YAML_0_3::Value << "hi";
			out << YAML_0_3::EndSeq;
		}
	}
	
	namespace {
		void RunEmitterTest(void (*test)(YAML_0_3::Emitter&, std::string&), const std::string& name, int& passed, int& total) {
			YAML_0_3::Emitter out;
			std::string desiredOutput;
			test(out, desiredOutput);
			std::string output = out.c_str();
			std::string lastError = out.GetLastError();

			if(output == desiredOutput) {
				try {
					std::stringstream stream(output);
					YAML_0_3::Parser parser;
					YAML_0_3::Node node;
					parser.GetNextDocument(node);
					passed++;
				} catch(const YAML_0_3::Exception& e) {
					std::cout << "Emitter test failed: " << name << "\n";
					std::cout << "Parsing output error: " << e.what() << "\n";
				}
			} else {
				std::cout << "Emitter test failed: " << name << "\n";
				std::cout << "Output:\n";
				std::cout << output << "<<<\n";
				std::cout << "Desired output:\n";
				std::cout << desiredOutput << "<<<\n";				
				if(!out.good())
					std::cout << "Emitter error: " << lastError << "\n";
			}
			total++;
		}
		
		void RunEmitterErrorTest(void (*test)(YAML_0_3::Emitter&, std::string&), const std::string& name, int& passed, int& total) {
			YAML_0_3::Emitter out;
			std::string desiredError;
			test(out, desiredError);
			std::string lastError = out.GetLastError();
			if(!out.good() && lastError == desiredError) {
				passed++;
			} else {
				std::cout << "Emitter test failed: " << name << "\n";
				if(out.good())
					std::cout << "No error detected\n";
				else
					std::cout << "Detected error: " << lastError << "\n";
				std::cout << "Expected error: " << desiredError << "\n";
			}
			total++;
		}
	}
	
	bool RunEmitterTests()
	{
		int passed = 0;
		int total = 0;
		RunEmitterTest(&Emitter::SimpleScalar, "simple scalar", passed, total);
		RunEmitterTest(&Emitter::SimpleSeq, "simple seq", passed, total);
		RunEmitterTest(&Emitter::SimpleFlowSeq, "simple flow seq", passed, total);
		RunEmitterTest(&Emitter::EmptyFlowSeq, "empty flow seq", passed, total);
		RunEmitterTest(&Emitter::NestedBlockSeq, "nested block seq", passed, total);
		RunEmitterTest(&Emitter::NestedFlowSeq, "nested flow seq", passed, total);
		RunEmitterTest(&Emitter::SimpleMap, "simple map", passed, total);
		RunEmitterTest(&Emitter::SimpleFlowMap, "simple flow map", passed, total);
		RunEmitterTest(&Emitter::MapAndList, "map and list", passed, total);
		RunEmitterTest(&Emitter::ListAndMap, "list and map", passed, total);
		RunEmitterTest(&Emitter::NestedBlockMap, "nested block map", passed, total);
		RunEmitterTest(&Emitter::NestedFlowMap, "nested flow map", passed, total);
		RunEmitterTest(&Emitter::MapListMix, "map list mix", passed, total);
		RunEmitterTest(&Emitter::SimpleLongKey, "simple long key", passed, total);
		RunEmitterTest(&Emitter::SingleLongKey, "single long key", passed, total);
		RunEmitterTest(&Emitter::ComplexLongKey, "complex long key", passed, total);
		RunEmitterTest(&Emitter::AutoLongKey, "auto long key", passed, total);
		RunEmitterTest(&Emitter::ScalarFormat, "scalar format", passed, total);
		RunEmitterTest(&Emitter::AutoLongKeyScalar, "auto long key scalar", passed, total);
		RunEmitterTest(&Emitter::LongKeyFlowMap, "long key flow map", passed, total);
		RunEmitterTest(&Emitter::BlockMapAsKey, "block map as key", passed, total);
		RunEmitterTest(&Emitter::AliasAndAnchor, "alias and anchor", passed, total);
		RunEmitterTest(&Emitter::AliasAndAnchorWithNull, "alias and anchor with null", passed, total);
		RunEmitterTest(&Emitter::AliasAndAnchorInFlow, "alias and anchor in flow", passed, total);
		RunEmitterTest(&Emitter::SimpleVerbatimTag, "simple verbatim tag", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagInBlockSeq, "verbatim tag in block seq", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagInFlowSeq, "verbatim tag in flow seq", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagInFlowSeqWithNull, "verbatim tag in flow seq with null", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagInBlockMap, "verbatim tag in block map", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagInFlowMap, "verbatim tag in flow map", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagInFlowMapWithNull, "verbatim tag in flow map with null", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagWithEmptySeq, "verbatim tag with empty seq", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagWithEmptyMap, "verbatim tag with empty map", passed, total);
		RunEmitterTest(&Emitter::VerbatimTagWithEmptySeqAndMap, "verbatim tag with empty seq and map", passed, total);
		RunEmitterTest(&Emitter::ByKindTagWithScalar, "by-kind tag with scalar", passed, total);
		RunEmitterTest(&Emitter::LocalTagWithScalar, "local tag with scalar", passed, total);
		RunEmitterTest(&Emitter::ComplexDoc, "complex doc", passed, total);
		RunEmitterTest(&Emitter::STLContainers, "STL containers", passed, total);
		RunEmitterTest(&Emitter::SimpleComment, "simple comment", passed, total);
		RunEmitterTest(&Emitter::MultiLineComment, "multi-line comment", passed, total);
		RunEmitterTest(&Emitter::ComplexComments, "complex comments", passed, total);
		RunEmitterTest(&Emitter::InitialComment, "initial comment", passed, total);
		RunEmitterTest(&Emitter::InitialCommentWithDocIndicator, "initial comment with doc indicator", passed, total);
		RunEmitterTest(&Emitter::CommentInFlowSeq, "comment in flow seq", passed, total);
		RunEmitterTest(&Emitter::CommentInFlowMap, "comment in flow map", passed, total);
		RunEmitterTest(&Emitter::Indentation, "indentation", passed, total);
		RunEmitterTest(&Emitter::SimpleGlobalSettings, "simple global settings", passed, total);
		RunEmitterTest(&Emitter::ComplexGlobalSettings, "complex global settings", passed, total);
		RunEmitterTest(&Emitter::Null, "null", passed, total);
		RunEmitterTest(&Emitter::EscapedUnicode, "escaped unicode", passed, total);
		RunEmitterTest(&Emitter::Unicode, "unicode", passed, total);
		RunEmitterTest(&Emitter::DoubleQuotedUnicode, "double quoted unicode", passed, total);
		RunEmitterTest(&Emitter::UserType, "user type", passed, total);
		RunEmitterTest(&Emitter::UserTypeInContainer, "user type in container", passed, total);
		RunEmitterTest(&Emitter::PointerToInt, "pointer to int", passed, total);
		RunEmitterTest(&Emitter::PointerToUserType, "pointer to user type", passed, total);
		RunEmitterTest(&Emitter::NewlineAtEnd, "newline at end", passed, total);
		RunEmitterTest(&Emitter::NewlineInBlockSequence, "newline in block sequence", passed, total);
		RunEmitterTest(&Emitter::NewlineInFlowSequence, "newline in flow sequence", passed, total);
		RunEmitterTest(&Emitter::NewlineInBlockMap, "newline in block map", passed, total);
		RunEmitterTest(&Emitter::NewlineInFlowMap, "newline in flow map", passed, total);
		RunEmitterTest(&Emitter::LotsOfNewlines, "lots of newlines", passed, total);
		RunEmitterTest(&Emitter::Binary, "binary", passed, total);
		RunEmitterTest(&Emitter::LongBinary, "long binary", passed, total);
		RunEmitterTest(&Emitter::EmptyBinary, "empty binary", passed, total);
		RunEmitterTest(&Emitter::ColonAtEndOfScalar, "colon at end of scalar", passed, total);
		RunEmitterTest(&Emitter::ColonAsScalar, "colon as scalar", passed, total);
		RunEmitterTest(&Emitter::ColonAtEndOfScalarInFlow, "colon at end of scalar in flow", passed, total);
		RunEmitterTest(&Emitter::BoolFormatting, "bool formatting", passed, total);
		RunEmitterTest(&Emitter::DocStartAndEnd, "doc start and end", passed, total);
		RunEmitterTest(&Emitter::ImplicitDocStart, "implicit doc start", passed, total);
		RunEmitterTest(&Emitter::EmptyString, "empty string", passed, total);
		RunEmitterTest(&Emitter::SingleChar, "single char", passed, total);
		RunEmitterTest(&Emitter::DefaultPrecision, "default precision", passed, total);
		RunEmitterTest(&Emitter::SetPrecision, "set precision", passed, total);
		RunEmitterTest(&Emitter::DashInBlockContext, "dash in block context", passed, total);
		RunEmitterTest(&Emitter::HexAndOct, "hex and oct", passed, total);
		
		RunEmitterErrorTest(&Emitter::ExtraEndSeq, "extra EndSeq", passed, total);
		RunEmitterErrorTest(&Emitter::ExtraEndMap, "extra EndMap", passed, total);
		RunEmitterErrorTest(&Emitter::BadSingleQuoted, "bad single quoted string", passed, total);
		RunEmitterErrorTest(&Emitter::InvalidAnchor, "invalid anchor", passed, total);
		RunEmitterErrorTest(&Emitter::InvalidAlias, "invalid alias", passed, total);
		RunEmitterErrorTest(&Emitter::MissingKey, "missing key", passed, total);
		RunEmitterErrorTest(&Emitter::MissingValue, "missing value", passed, total);
		RunEmitterErrorTest(&Emitter::UnexpectedKey, "unexpected key", passed, total);
		RunEmitterErrorTest(&Emitter::UnexpectedValue, "unexpected value", passed, total);
		RunEmitterErrorTest(&Emitter::BadLocalTag, "bad local tag", passed, total);

		std::cout << "Emitter tests: " << passed << "/" << total << " passed\n";
		return passed == total;
	}
}

