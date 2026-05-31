#include "overlay.h"

#include "../../backend/mem/memory.h"
#include "../globals/globals.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3d9.h>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <algorithm>

#include "../../backend/imgui/backends/imgui_impl_dx9.h"
#include "../../backend/imgui/backends/imgui_impl_win32.h"
#include "../../backend/imgui/imgui.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

namespace
{
	constexpr auto class_name = "cs_source_ext_overlay";
	constexpr auto color_key = RGB(0, 0, 0);

	HWND game_hwnd = nullptr;
	HWND overlay_hwnd = nullptr;
	IDirect3D9* d3d = nullptr;
	IDirect3DDevice9* device = nullptr;
	D3DPRESENT_PARAMETERS params{};
	bool imgui_ready = false;
	bool menu_open = false;

	void click_through(bool enabled)
	{
		const auto style = GetWindowLongA(overlay_hwnd, GWL_EXSTYLE);
		SetWindowLongA(
			overlay_hwnd,
			GWL_EXSTYLE,
			enabled ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT
		);
	}

	LRESULT CALLBACK wnd_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
	{
		if (imgui_ready && ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam))
			return true;

		if (message == WM_DESTROY)
		{
			PostQuitMessage(0);
			return 0;
		}

		return DefWindowProcA(window, message, wparam, lparam);
	}

	HWND get_game()
	{
		for (auto hwnd = FindWindowExA(nullptr, nullptr, "Valve001", nullptr);
			hwnd;
			hwnd = FindWindowExA(nullptr, hwnd, "Valve001", nullptr))
		{
			DWORD pid = 0;
			GetWindowThreadProcessId(hwnd, &pid);

			if (pid == memory->get_process_id() && IsWindowVisible(hwnd))
				return hwnd;
		}

		return nullptr;
	}

	bool game_active()
	{
		const auto foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
		return foreground == game_hwnd || foreground == overlay_hwnd;
	}

	bool client_rect(RECT& rect)
	{
		if (!game_hwnd || !IsWindow(game_hwnd))
			game_hwnd = get_game();

		if (!game_hwnd || IsIconic(game_hwnd))
			return false;

		RECT client{};
		if (!GetClientRect(game_hwnd, &client))
			return false;

		POINT top_left{ 0, 0 };
		POINT bottom_right{ client.right, client.bottom };
		if (!ClientToScreen(game_hwnd, &top_left) || !ClientToScreen(game_hwnd, &bottom_right))
			return false;

		rect = { top_left.x, top_left.y, bottom_right.x, bottom_right.y };
		return rect.right > rect.left && rect.bottom > rect.top;
	}

	bool make_window()
	{
		WNDCLASSEXA wc{};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = wnd_proc;
		wc.hInstance = GetModuleHandleA(nullptr);
		wc.lpszClassName = class_name;

		if (!RegisterClassExA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			return false;

		RECT rect{};
		if (!client_rect(rect))
			return false;

		overlay_hwnd = CreateWindowExA(
			WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
			class_name,
			"",
			WS_POPUP,
			rect.left,
			rect.top,
			rect.right - rect.left,
			rect.bottom - rect.top,
			nullptr,
			nullptr,
			wc.hInstance,
			nullptr
		);

		if (!overlay_hwnd)
			return false;

		SetLayeredWindowAttributes(overlay_hwnd, color_key, 0, LWA_COLORKEY);
		ShowWindow(overlay_hwnd, SW_SHOW);
		UpdateWindow(overlay_hwnd);
		return true;
	}

	bool make_device(int width, int height)
	{
		d3d = Direct3DCreate9(D3D_SDK_VERSION);
		if (!d3d || width <= 0 || height <= 0)
			return false;

		params = {};
		params.Windowed = TRUE;
		params.SwapEffect = D3DSWAPEFFECT_DISCARD;
		params.hDeviceWindow = overlay_hwnd;
		params.BackBufferWidth = width;
		params.BackBufferHeight = height;
		params.BackBufferFormat = D3DFMT_A8R8G8B8;
		params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

		auto result = d3d->CreateDevice(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			overlay_hwnd,
			D3DCREATE_HARDWARE_VERTEXPROCESSING,
			&params,
			&device
		);

		if (FAILED(result))
		{
			result = d3d->CreateDevice(
				D3DADAPTER_DEFAULT,
				D3DDEVTYPE_HAL,
				overlay_hwnd,
				D3DCREATE_SOFTWARE_VERTEXPROCESSING,
				&params,
				&device
			);
		}

		return SUCCEEDED(result);
	}

	bool make_imgui()
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;

		ImGui::StyleColorsDark();

		if (!ImGui_ImplWin32_Init(overlay_hwnd))
		{
			ImGui::DestroyContext();
			return false;
		}

		if (!ImGui_ImplDX9_Init(device))
		{
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			return false;
		}

		imgui_ready = true;
		return true;
	}

	void cleanup()
	{
		if (imgui_ready)
		{
			ImGui_ImplDX9_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			imgui_ready = false;
		}

		if (device)
		{
			device->Release();
			device = nullptr;
		}

		if (d3d)
		{
			d3d->Release();
			d3d = nullptr;
		}

		if (overlay_hwnd)
		{
			DestroyWindow(overlay_hwnd);
			overlay_hwnd = nullptr;
		}

		UnregisterClassA(class_name, GetModuleHandleA(nullptr));
	}

	bool sync_window()
	{
		RECT rect{};
		if (!client_rect(rect) || !game_active())
		{
			ShowWindow(overlay_hwnd, SW_HIDE);
			return false;
		}

		const auto width = rect.right - rect.left;
		const auto height = rect.bottom - rect.top;

		SetWindowPos(
			overlay_hwnd,
			HWND_TOPMOST,
			rect.left,
			rect.top,
			width,
			height,
			SWP_NOACTIVATE | SWP_SHOWWINDOW
		);

		if (static_cast<UINT>(width) == params.BackBufferWidth
			&& static_cast<UINT>(height) == params.BackBufferHeight)
		{
			return true;
		}

		params.BackBufferWidth = width;
		params.BackBufferHeight = height;
		ImGui_ImplDX9_InvalidateDeviceObjects();
		if (SUCCEEDED(device->Reset(&params)))
			ImGui_ImplDX9_CreateDeviceObjects();

		return true;
	}

	void clamp_menu()
	{
		const auto display = ImGui::GetIO().DisplaySize;
		const auto size = ImGui::GetWindowSize();
		const auto max_x = std::max(0.0f, display.x - size.x);
		const auto max_y = std::max(0.0f, display.y - size.y);
		auto pos = ImGui::GetWindowPos();

		pos.x = std::clamp(pos.x, 0.0f, max_x);
		pos.y = std::clamp(pos.y, 0.0f, max_y);
		ImGui::SetWindowPos(pos);
	}

	void frame()
	{
		const auto state = device->TestCooperativeLevel();
		if (state == D3DERR_DEVICELOST)
			return;

		if (state == D3DERR_DEVICENOTRESET)
		{
			ImGui_ImplDX9_InvalidateDeviceObjects();
			if (FAILED(device->Reset(&params)))
				return;

			ImGui_ImplDX9_CreateDeviceObjects();
		}

		device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 0.0f, 0);

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		if (menu_open)
		{
			ImGui::SetNextWindowPos({ 20.0f, 20.0f }, ImGuiCond_Once);
			ImGui::SetNextWindowSize({ 260.0f, 120.0f }, ImGuiCond_Once);
			ImGui::Begin("CS:S external", &menu_open, ImGuiWindowFlags_NoCollapse);

			bool bhop = game::bhop_enabled;
			if (ImGui::Checkbox("BunnyHop", &bhop))
				game::bhop_enabled = bhop;

			clamp_menu();
			ImGui::End();

			if (!menu_open)
				click_through(true);
		}

		ImGui::EndFrame();
		ImGui::Render();

		if (SUCCEEDED(device->BeginScene()))
		{
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			device->EndScene();
		}

		device->Present(nullptr, nullptr, nullptr, nullptr);
	}
}

namespace overlay
{
	bool run()
	{
		game_hwnd = get_game();
		if (!game_hwnd || !make_window())
			return false;

		RECT rect{};
		GetClientRect(overlay_hwnd, &rect);

		if (!make_device(rect.right - rect.left, rect.bottom - rect.top) || !make_imgui())
		{
			cleanup();
			return false;
		}

		MSG message{};
		auto insert_was_down = false;

		while (game::running && memory->is_process_alive() && !(GetAsyncKeyState(VK_END) & 0x8000))
		{
			while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE))
			{
				if (message.message == WM_QUIT)
				{
					game::running = false;
					break;
				}

				TranslateMessage(&message);
				DispatchMessageA(&message);
			}

			const auto insert_down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
			if (insert_down && !insert_was_down)
			{
				menu_open = !menu_open;
				click_through(!menu_open);
			}

			insert_was_down = insert_down;

			if (sync_window())
				frame();

			Sleep(8);
		}

		game::running = false;
		cleanup();
		return true;
	}
}
