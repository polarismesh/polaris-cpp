#include "yaml-cpp/null.h"
#include "yaml-cpp/node.h"

namespace YAML_0_3
{
	_Null Null;

	bool IsNull(const Node& node)
	{
		return node.Read(Null);
	}
}
