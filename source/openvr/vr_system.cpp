/*
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#include "dll_log.hpp"
#include "hook_manager.hpp"
#include "d3d11/runtime_d3d11.hpp"
#include "d3d12/runtime_d3d12.hpp"
#include "opengl/runtime_gl.hpp"
#include "vulkan/runtime_vk.hpp"
#include <openvr.h>

std::unordered_map<vr::ETextureType, std::shared_ptr<reshade::runtime>> s_vr_runtimes;

static void vr_submit_gl(vr::EVREye eye, GLuint object, bool rbo, const vr::VRTextureBounds_t *bounds)
{
	if (s_vr_runtimes.find(vr::TextureType_OpenGL) == s_vr_runtimes.end())
		s_vr_runtimes.emplace(vr::TextureType_OpenGL, std::make_unique<reshade::opengl::runtime_gl>());

	const auto runtime = std::static_pointer_cast<reshade::opengl::runtime_gl>(s_vr_runtimes.at(vr::TextureType_OpenGL));

	unsigned int width, height;
	if (rbo)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, object);
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, reinterpret_cast<GLint *>(&width));
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, reinterpret_cast<GLint *>(&height));
	}
	else
	{
		glBindTexture(GL_TEXTURE_2D, object);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, reinterpret_cast<GLint *>(&width));
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, reinterpret_cast<GLint *>(&height));
	}

	if (runtime->frame_width() != width ||
		runtime->frame_height() != height)
	{
		runtime->on_reset();
		runtime->on_init(nullptr, width, height);
	}

	runtime->on_present();
}
static void vr_submit_vk(vr::EVREye eye, const vr::VRVulkanTextureData_t *texture, const vr::VRTextureBounds_t *bounds)
{
	extern VkLayerDispatchTable &dispatch_table_from_device(VkDevice device);
	extern VkLayerInstanceDispatchTable &dispatch_table_from_instance(VkInstance instance);

	if (s_vr_runtimes.find(vr::TextureType_Vulkan) == s_vr_runtimes.end())
		s_vr_runtimes.emplace(vr::TextureType_Vulkan, std::make_unique<reshade::vulkan::runtime_vk>(
			texture->m_pDevice, texture->m_pPhysicalDevice, texture->m_nQueueFamilyIndex, dispatch_table_from_instance(texture->m_pInstance), dispatch_table_from_device(texture->m_pDevice)));

	const auto runtime = std::static_pointer_cast<reshade::vulkan::runtime_vk>(s_vr_runtimes.at(vr::TextureType_Vulkan));

	if (runtime->frame_width() != texture->m_nWidth ||
		runtime->frame_height() != texture->m_nHeight)
	{
		runtime->on_reset();

		VkSwapchainCreateInfoKHR swap_desc { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		swap_desc.imageExtent = { texture->m_nWidth, texture->m_nHeight };
		swap_desc.imageFormat = static_cast<VkFormat>(texture->m_nFormat);
		runtime->on_init(VK_NULL_HANDLE, swap_desc, nullptr);
	}

	VkSemaphore signal = VK_NULL_HANDLE;
	runtime->on_present(texture->m_pQueue, 0, {}, signal);
}
static void vr_submit_d3d11(vr::EVREye eye, ID3D11Texture2D *texture, const vr::VRTextureBounds_t *bounds)
{
	if (s_vr_runtimes.find(vr::TextureType_DirectX) == s_vr_runtimes.end())
	{
		com_ptr<ID3D11Device> device;
		texture->GetDevice(&device);

		s_vr_runtimes.emplace(vr::TextureType_DirectX, std::make_unique<reshade::d3d11::runtime_d3d11>(device.get(), nullptr));
	}

	const auto runtime = std::static_pointer_cast<reshade::d3d11::runtime_d3d11>(s_vr_runtimes.at(vr::TextureType_DirectX));

	D3D11_TEXTURE2D_DESC source_desc = {};
	texture->GetDesc(&source_desc);

	if (runtime->frame_width() != source_desc.Width ||
		runtime->frame_height() != source_desc.Height)
	{
		runtime->on_reset();

		DXGI_SWAP_CHAIN_DESC swap_desc = {};
		swap_desc.BufferCount = 1;
		swap_desc.BufferDesc.Width = static_cast<UINT>(source_desc.Width);
		swap_desc.BufferDesc.Height = source_desc.Height;
		swap_desc.BufferDesc.Format = source_desc.Format;
		runtime->on_init(swap_desc);
	}

	runtime->on_present();
}
static void vr_submit_d3d12(vr::EVREye eye, const vr::D3D12TextureData_t *texture, const vr::VRTextureBounds_t *bounds)
{
	if (s_vr_runtimes.find(vr::TextureType_DirectX12) == s_vr_runtimes.end())
	{
		com_ptr<ID3D12Device> device;
		texture->m_pResource->GetDevice(IID_PPV_ARGS(&device));

		s_vr_runtimes.emplace(vr::TextureType_DirectX12, std::make_unique<reshade::d3d12::runtime_d3d12>(device.get(), texture->m_pCommandQueue, nullptr));
	}

	const auto runtime = std::static_pointer_cast<reshade::d3d12::runtime_d3d12>(s_vr_runtimes.at(vr::TextureType_DirectX12));

	const D3D12_RESOURCE_DESC source_desc = texture->m_pResource->GetDesc();

	if (runtime->frame_width() != source_desc.Width ||
		runtime->frame_height() != source_desc.Height)
	{
		runtime->on_reset();

		DXGI_SWAP_CHAIN_DESC swap_desc = {};
		swap_desc.BufferCount = 1;
		swap_desc.BufferDesc.Width = static_cast<UINT>(source_desc.Width);
		swap_desc.BufferDesc.Height = source_desc.Height;
		swap_desc.BufferDesc.Format = source_desc.Format;
		runtime->on_init(swap_desc);
	}

	runtime->on_present();
}

static vr::EVRCompositorError IVRCompositor_Submit(vr::IVRCompositor *pCompositor, vr::EVREye eEye, const vr::Texture_t *pTexture, const vr::VRTextureBounds_t *pBounds, vr::EVRSubmitFlags nSubmitFlags)
{
	assert(pTexture != nullptr);

	switch (pTexture->eType)
	{
	case vr::TextureType_OpenGL:
		vr_submit_gl(eEye, static_cast<GLuint>(reinterpret_cast<uintptr_t>(pTexture->handle)), (nSubmitFlags & vr::Submit_GlRenderBuffer) != 0, pBounds);
		break;
	case vr::TextureType_Vulkan:
		vr_submit_vk(eEye, static_cast<const vr::VRVulkanTextureData_t *>(pTexture->handle), pBounds);
		break;
	case vr::TextureType_DirectX:
		vr_submit_d3d11(eEye, static_cast<ID3D11Texture2D *>(pTexture->handle), pBounds);
		break;
	case vr::TextureType_DirectX12:
		vr_submit_d3d12(eEye, static_cast<const vr::D3D12TextureData_t *>(pTexture->handle), pBounds);
		break;
	}

	static const auto trampoline = reshade::hooks::call(IVRCompositor_Submit);
	return trampoline(pCompositor, eEye, pTexture, pBounds, nSubmitFlags);
}

HOOK_EXPORT uint32_t VR_CALLTYPE VR_InitInternal2(vr::EVRInitError *peError, vr::EVRApplicationType eApplicationType, const char *pStartupInfo)
{
	LOG(INFO) << "Redirecting VR_InitInternal2" << '(' << "peError = " << peError << ", eApplicationType = " << eApplicationType << ", pStartupInfo = " << pStartupInfo << ')' << " ...";

	return reshade::hooks::call(VR_InitInternal2)(peError, eApplicationType, pStartupInfo);
}
HOOK_EXPORT void     VR_CALLTYPE VR_ShutdownInternal()
{
	LOG(INFO) << "Redirecting VR_ShutdownInternal" << '(' << ')' << " ...";

	if (const auto it = s_vr_runtimes.find(vr::TextureType_OpenGL); it != s_vr_runtimes.end())
		static_cast<reshade::opengl::runtime_gl *>(it->second.get())->on_reset();
	if (const auto it = s_vr_runtimes.find(vr::TextureType_Vulkan); it != s_vr_runtimes.end())
		static_cast<reshade::vulkan::runtime_vk *>(it->second.get())->on_reset();
	if (const auto it = s_vr_runtimes.find(vr::TextureType_DirectX); it != s_vr_runtimes.end())
		static_cast<reshade::d3d11::runtime_d3d11 *>(it->second.get())->on_reset();
	if (const auto it = s_vr_runtimes.find(vr::TextureType_DirectX12); it != s_vr_runtimes.end())
		static_cast<reshade::d3d12::runtime_d3d12 *>(it->second.get())->on_reset();

	s_vr_runtimes.clear();

	return reshade::hooks::call(VR_ShutdownInternal)();
}

HOOK_EXPORT void *   VR_CALLTYPE VR_GetGenericInterface(const char *pchInterfaceVersion, vr::EVRInitError *peError)
{
	LOG(INFO) << "Redirecting VR_GetGenericInterface" << '(' << "pchInterfaceVersion = " << pchInterfaceVersion << ", peError = " << peError << ')' << " ...";

	void *const interface_instance = reshade::hooks::call(VR_GetGenericInterface)(pchInterfaceVersion, peError);

	if (strcmp(pchInterfaceVersion, "IVRCompositor_026") == 0)
		reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 5, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit));

	return interface_instance;
}
