#ifdef _WIN32
#include <Windows.h>
#endif

#include "ImGuiContext.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_vulkan.h"

imgui::Context::Context(GLFWwindow *window)
{
	IMGUI_CHECKVERSION();
	m_Members = std::make_shared<Members>();
	m_Members->context = ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 330");
}

// Context::Context(vk::Instance vulkanInstance)
//{
//	IMGUI_CHECKVERSION();
//	m_Members = std::make_shared<Members>();
//	m_Members->context = ImGui::CreateContext();
//	// TODO
//}

imgui::Context::~Context() = default;

void imgui::Context::newFrame()
{
	switch (m_Members->type)
	{
	case (GLFW):
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		break;
	case (VULKAN):
		ImGui_ImplVulkan_NewFrame();
		break;
	default:
		break;
	}

	ImGui::NewFrame();
}

void imgui::Context::render()
{
	ImGui::Render();

	switch (m_Members->type)
	{
	case (GLFW):
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		break;
	default:
		break;
	}
}

imgui::Context::Members::~Members()
{
	switch (type)
	{
	case (GLFW):
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		break;
	case (VULKAN):
		ImGui_ImplVulkan_Shutdown();
		break;
	default:
		break;
	}

	if (context)
		ImGui::DestroyContext(context);
}
