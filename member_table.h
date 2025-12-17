#pragma once

#define DLL_EXPORT __declspec(dllexport)

#include <cstdint>
#include "memory_tools.h"

extern "C"
{
	namespace YS
	{
		class DLL_EXPORT MEMBER_TABLE
		{
		public:
			static char* MemberTable;
			static char* MemberStatsAnchor;
		};
	}
}