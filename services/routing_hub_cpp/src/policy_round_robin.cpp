#include "policy.hpp"

size_t next_round_robin_index(size_t current, size_t size)
{
	if (size == 0)
	{
		return 0;
	}

	return (current + 1) % size;
}
