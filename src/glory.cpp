// Glory - ReShade add-on: تبويب "Glory" فيه تغيير لون الخلفية + صورة خلفية
// غير مجرَّب (لم يُبنَ في بيئة الإنشاء). مبني على واجهة ReShade add-on API.
#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID ImU64
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <imgui.h>
#include <reshade.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include "stb_image.h"

#include <cstdio>
#include <cstring>

using namespace reshade::api;

extern "C" __declspec(dllexport) const char *NAME = "Glory";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Glory menu: custom background color and image.";

namespace
{
	struct glory_state
	{
		float bg_color[3] = { 0.08f, 0.08f, 0.10f };
		bool  use_color = true;
		bool  use_image = false;
		float image_opacity = 0.6f;
		char  image_path[512] = "";
		bool  reload = false;
		resource tex = { 0 };
		resource_view srv = { 0 };
		device *dev = nullptr;
	} g;

	void save_settings()
	{
		char buf[128];
		std::snprintf(buf, sizeof(buf), "%f,%f,%f", g.bg_color[0], g.bg_color[1], g.bg_color[2]);
		reshade::set_config_value(nullptr, "GLORY", "BgColor", buf);
		reshade::set_config_value(nullptr, "GLORY", "UseColor", g.use_color ? "1" : "0");
		reshade::set_config_value(nullptr, "GLORY", "UseImage", g.use_image ? "1" : "0");
		std::snprintf(buf, sizeof(buf), "%f", g.image_opacity);
		reshade::set_config_value(nullptr, "GLORY", "ImageOpacity", buf);
		reshade::set_config_value(nullptr, "GLORY", "ImagePath", g.image_path);
	}

	void load_settings()
	{
		char buf[512]; size_t sz;
		sz = sizeof(buf);
		if (reshade::get_config_value(nullptr, "GLORY", "BgColor", buf, &sz))
			std::sscanf(buf, "%f,%f,%f", &g.bg_color[0], &g.bg_color[1], &g.bg_color[2]);
		sz = sizeof(buf);
		if (reshade::get_config_value(nullptr, "GLORY", "UseColor", buf, &sz)) g.use_color = buf[0] == '1';
		sz = sizeof(buf);
		if (reshade::get_config_value(nullptr, "GLORY", "UseImage", buf, &sz)) g.use_image = buf[0] == '1';
		sz = sizeof(buf);
		if (reshade::get_config_value(nullptr, "GLORY", "ImageOpacity", buf, &sz)) std::sscanf(buf, "%f", &g.image_opacity);
		sz = sizeof(g.image_path);
		if (reshade::get_config_value(nullptr, "GLORY", "ImagePath", g.image_path, &sz)) g.reload = g.image_path[0] != '\0';
	}

	void unload_image()
	{
		if (!g.dev) return;
		if (g.srv.handle) { g.dev->destroy_resource_view(g.srv); g.srv = { 0 }; }
		if (g.tex.handle) { g.dev->destroy_resource(g.tex);      g.tex = { 0 }; }
	}

	bool load_image(const char *utf8_path)
	{
		unload_image();
		wchar_t wpath[1024];
		if (!MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wpath, 1024)) return false;
		FILE *f = nullptr;
		if (_wfopen_s(&f, wpath, L"rb") != 0 || !f) return false;
		int w = 0, h = 0, c = 0;
		stbi_uc *px = stbi_load_from_file(f, &w, &h, &c, 4);
		std::fclose(f);
		if (!px) return false;

		subresource_data data = {};
		data.data = px;
		data.row_pitch = static_cast<uint32_t>(w) * 4;
		data.slice_pitch = static_cast<uint32_t>(w) * static_cast<uint32_t>(h) * 4;

		const resource_desc desc(w, h, 1, 1, format::r8g8b8a8_unorm, 1, memory_heap::gpu_only, resource_usage::shader_resource);
		bool ok = g.dev->create_resource(desc, &data, resource_usage::shader_resource, &g.tex) &&
			g.dev->create_resource_view(g.tex, resource_usage::shader_resource,
				resource_view_desc(resource_view_type::texture_2d, format::r8g8b8a8_unorm, 0, 1, 0, 1), &g.srv);
		stbi_image_free(px);
		return ok;
	}

	bool browse_for_image(char *out_utf8, int out_size)
	{
		wchar_t file[1024] = L"";
		OPENFILENAMEW ofn = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0";
		ofn.lpstrFile = file;
		ofn.nMaxFile = 1024;
		ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
		if (!GetOpenFileNameW(&ofn)) return false;
		return WideCharToMultiByte(CP_UTF8, 0, file, -1, out_utf8, out_size, nullptr, nullptr) > 0;
	}

	void draw_overlay(effect_runtime *runtime)
	{
		g.dev = runtime->get_device();

		if (g.reload) { g.reload = false; if (!load_image(g.image_path)) g.use_image = false; }

		// لون الخلفية: يطبّق على نافذة القائمة كلها
		if (g.use_color)
		{
			ImVec4 col(g.bg_color[0], g.bg_color[1], g.bg_color[2], 1.0f);
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = col;
			ImGui::GetStyle().Colors[ImGuiCol_ChildBg] = col;
		}

		// صورة الخلفية: ترسم خلف محتوى تبويب Glory
		if (g.use_image && g.srv.handle)
		{
			const ImVec2 pmin = ImGui::GetWindowPos();
			const ImVec2 pmax(pmin.x + ImGui::GetWindowWidth(), pmin.y + ImGui::GetWindowHeight());
			ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(g.srv.handle), pmin, pmax,
				ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(g.image_opacity * 255.f)));
		}

		bool changed = false;
		changed |= ImGui::Checkbox("Use background color", &g.use_color);
		changed |= ImGui::ColorEdit3("Background color", g.bg_color);
		ImGui::Separator();
		changed |= ImGui::Checkbox("Use background image", &g.use_image);
		changed |= ImGui::SliderFloat("Image opacity", &g.image_opacity, 0.05f, 1.0f);
		ImGui::InputText("Image path", g.image_path, sizeof(g.image_path));
		ImGui::SameLine();
		if (ImGui::Button("Browse..."))
		{
			if (browse_for_image(g.image_path, sizeof(g.image_path))) { g.reload = true; g.use_image = true; changed = true; }
		}
		if (ImGui::Button("Load image")) { g.reload = true; g.use_image = true; changed = true; }

		if (changed) save_settings();
	}

	void on_destroy_device(device *dev)
	{
		if (dev == g.dev) { unload_image(); g.dev = nullptr; }
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		if (!reshade::register_addon(hModule)) return FALSE;
		reshade::register_overlay("Glory", draw_overlay);
		reshade::register_event<reshade::addon_event::destroy_device>(on_destroy_device);
		load_settings();
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_addon(hModule);
		break;
	}
	return TRUE;
}
