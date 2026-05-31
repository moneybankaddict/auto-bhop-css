#include "../backend/mem/memory.h"
#include "features/bhop.h"
#include "globals/globals.h"
#include "overlay/overlay.h"
#include "offsets/offsets.h"

#include <cstdint>
#include <iostream>
#include <thread>

int main()
{
	constexpr auto process_name = "cstrike_win64.exe";

	if (!memory->attach_to_process(process_name))
	{
		std::cout << "failed to find or open " << process_name << ".\n";
		return 1;
	}

	game::client = memory->find_module_address("client.dll");
	game::engine = memory->find_module_address("engine.dll");

	if (!game::client || !game::engine)
	{
		std::cout << "failed to find client.dll or engine.dll.\n";
		return 1;
	}

	game::entity_list = game::client + offsets::entity_list;
	game::localplayer = memory->read<std::uintptr_t>(game::entity_list + 0x10);

	std::cout << "localplayer: 0x" << std::hex << std::uppercase << game::localplayer << '\n';

	game::running = true;
	std::thread bhop_thread(features::run_bhop);

	if (!overlay::run())
		std::cout << "failed to create overlay.\n";

	game::running = false;

	if (bhop_thread.joinable())
		bhop_thread.join();

	return 0;
}
