#include "bhop.h"

#include "../../backend/mem/memory.h"
#include "../globals/globals.h"
#include "../offsets/offsets.h"

#include <cstdint>

namespace features
{
	void run_bhop()
	{
		const auto jump_address = game::client + offsets::jump;

		while (game::running && memory->is_process_alive() && !(GetAsyncKeyState(VK_END) & 0x8000))
		{
			if (!game::bhop_enabled)
			{
				memory->write_checked<std::int32_t>(jump_address, 4);
				Sleep(10);
				continue;
			}

			game::localplayer = memory->read<std::uintptr_t>(game::entity_list + 0x10);
			if (!game::localplayer)
			{
				Sleep(50);
				continue;
			}

			if (!(GetAsyncKeyState(VK_SPACE) & 0x8000))
			{
				memory->write_checked<std::int32_t>(jump_address, 4);
				Sleep(1);
				continue;
			}

			const auto velocity_z = memory->read<float>(game::localplayer + offsets::velocity);
			const auto on_ground = velocity_z > -5.0f && velocity_z < 5.0f;

			if (on_ground)
			{
				memory->write_checked<std::int32_t>(jump_address, 5);
				Sleep(15);
				memory->write_checked<std::int32_t>(jump_address, 4);
			}

			Sleep(1);
		}

		memory->write_checked<std::int32_t>(jump_address, 4);
	}
}
