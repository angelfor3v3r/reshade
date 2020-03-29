/*
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#if RESHADE_OPENVR

#include "dll_log.hpp"
#include "hook_manager.hpp"
#include "d3d11/d3d11_device.hpp"
#include "d3d11/d3d11_device_context.hpp"
#include "d3d11/runtime_d3d11.hpp"
#include "d3d12/d3d12_device.hpp"
#include "d3d12/runtime_d3d12.hpp"
#include "opengl/runtime_gl.hpp"
#include "vulkan/runtime_vk.hpp"
#include <openvr.h>

std::unordered_map<vr::ETextureType, std::shared_ptr<reshade::runtime>> s_vr_runtimes;

static void vr_submit_gl(vr::EVREye eye, GLuint object, bool rbo, const vr::VRTextureBounds_t *bounds)
{
#if 0
	std::shared_ptr<reshade::opengl::runtime_gl> runtime;
	if (const auto it = s_vr_runtimes.find(vr::TextureType_OpenGL); it == s_vr_runtimes.end())
	{
		runtime = std::make_shared<reshade::opengl::runtime_gl>();

		s_vr_runtimes.emplace(vr::TextureType_OpenGL, runtime);
	}
	else
	{
		runtime = std::static_pointer_cast<reshade::opengl::runtime_gl>(it->second);
	}

	unsigned int width, height;
	if (rbo)
	{
		GLint prev_object = 0;
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &prev_object);
		glBindRenderbuffer(GL_RENDERBUFFER, object);
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, reinterpret_cast<GLint *>(&width));
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, reinterpret_cast<GLint *>(&height));
		glBindRenderbuffer(GL_RENDERBUFFER, prev_object);
	}
	else
	{
		GLint prev_object = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_object);
		glBindTexture(GL_TEXTURE_2D, object);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, reinterpret_cast<GLint *>(&width));
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, reinterpret_cast<GLint *>(&height));
		glBindTexture(GL_TEXTURE_2D, prev_object);
	}

	if (width != runtime->frame_width() || height != runtime->frame_height())
	{
		runtime->on_reset();

		// TODO: Pass in object
		if (!runtime->on_init(nullptr, width, height))
		{
			LOG(ERROR) << "Failed to recreate OpenGL runtime environment on runtime " << runtime.get() << '.';
			return;
		}
	}

	runtime->on_present();
#endif
}
static void vr_submit_vk(vr::EVREye eye, const vr::VRVulkanTextureData_t *texture, const vr::VRTextureBounds_t *bounds)
{
#if 0
	std::shared_ptr<reshade::vulkan::runtime_vk> runtime;
	if (const auto it = s_vr_runtimes.find(vr::TextureType_Vulkan); it == s_vr_runtimes.end())
	{
		extern VkLayerDispatchTable &dispatch_table_from_device(VkDevice device);
		extern VkLayerInstanceDispatchTable &dispatch_table_from_instance(VkInstance instance);

		runtime = std::make_shared<reshade::vulkan::runtime_vk>(
			texture->m_pDevice, texture->m_pPhysicalDevice, texture->m_nQueueFamilyIndex, dispatch_table_from_instance(texture->m_pInstance), dispatch_table_from_device(texture->m_pDevice));

		runtime->_buffer_detection = nullptr; // TODO

		s_vr_runtimes.emplace(vr::TextureType_Vulkan, runtime);
	}
	else
	{
		runtime = std::static_pointer_cast<reshade::vulkan::runtime_vk>(it->second);
	}

	static uint64_t s_last_source_tex = 0;
	if (!runtime->is_initialized() || texture->m_nImage != s_last_source_tex)
	{
		runtime->on_reset();

		VkSwapchainCreateInfoKHR swap_desc { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
		swap_desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		swap_desc.imageExtent = { texture->m_nWidth, texture->m_nHeight };
		swap_desc.imageFormat = static_cast<VkFormat>(texture->m_nFormat);

		if (!runtime->on_init(VK_NULL_HANDLE, swap_desc, nullptr, reinterpret_cast<VkImage>(texture->m_nImage)))
		{
			LOG(ERROR) << "Failed to recreate Vulkan runtime environment on runtime " << runtime.get() << '.';
			return;
		}

		s_last_source_tex = texture->m_nImage;
	}

	VkSemaphore signal = VK_NULL_HANDLE;
	runtime->on_present(texture->m_pQueue, 0, {}, signal);
#endif
}
static void vr_submit_d3d11(vr::EVREye eye, ID3D11Texture2D *texture, const vr::VRTextureBounds_t *bounds)
{
	std::shared_ptr<reshade::d3d11::runtime_d3d11> runtime;
	if (const auto it = s_vr_runtimes.find(vr::TextureType_DirectX); it == s_vr_runtimes.end())
	{
		com_ptr<ID3D11Device> device;
		com_ptr< D3D11Device> device_proxy;
		texture->GetDevice(&device);
		if (UINT data_size = sizeof(device_proxy);
			FAILED(device->GetPrivateData(__uuidof(D3D11Device), &data_size, reinterpret_cast<void *>(&device_proxy))))
			return;

		runtime = std::make_shared<reshade::d3d11::runtime_d3d11>(device.get(), nullptr);

		runtime->_buffer_detection = &device_proxy->_immediate_context->_buffer_detection;

		s_vr_runtimes.emplace(vr::TextureType_DirectX, runtime);
	}
	else
	{
		runtime = std::static_pointer_cast<reshade::d3d11::runtime_d3d11>(it->second);
	}

	static ID3D11Texture2D *s_last_source_tex = nullptr;
	if (!runtime->is_initialized() || texture != s_last_source_tex)
	{
		runtime->on_reset();

		DXGI_SWAP_CHAIN_DESC swap_desc = {};
		swap_desc.BufferCount = 1;
		D3D11_TEXTURE2D_DESC source_desc = {};
		texture->GetDesc(&source_desc);
		swap_desc.BufferDesc.Width = static_cast<UINT>(source_desc.Width);
		swap_desc.BufferDesc.Height = source_desc.Height;
		swap_desc.BufferDesc.Format = source_desc.Format;
		swap_desc.SampleDesc = source_desc.SampleDesc;

		if (!runtime->on_init(swap_desc, texture))
		{
			LOG(ERROR) << "Failed to recreate Direct3D 11 runtime environment on runtime " << runtime.get() << '.';
			return;
		}

		s_last_source_tex = texture;
	}

	runtime->on_present();
}
static void vr_submit_d3d12(vr::EVREye eye, const vr::D3D12TextureData_t *texture, const vr::VRTextureBounds_t *bounds)
{
	std::shared_ptr<reshade::d3d12::runtime_d3d12> runtime;
	if (const auto it = s_vr_runtimes.find(vr::TextureType_DirectX12); it == s_vr_runtimes.end())
	{
		com_ptr<ID3D12Device> device;
		com_ptr< D3D12Device> device_proxy;
		texture->m_pResource->GetDevice(IID_PPV_ARGS(&device));
		if (UINT data_size = sizeof(device_proxy);
			FAILED(device->GetPrivateData(__uuidof(D3D12Device), &data_size, reinterpret_cast<void *>(&device_proxy))))
			return;

		runtime = std::make_shared<reshade::d3d12::runtime_d3d12>(device.get(), texture->m_pCommandQueue, nullptr);

		runtime->_buffer_detection = &device_proxy->_buffer_detection;

		s_vr_runtimes.emplace(vr::TextureType_DirectX12, runtime);
	}
	else
	{
		runtime = std::static_pointer_cast<reshade::d3d12::runtime_d3d12>(it->second);
	}

	static ID3D12Resource *s_last_source_tex = nullptr;
	if (!runtime->is_initialized() || texture->m_pResource != s_last_source_tex)
	{
		runtime->on_reset();

		DXGI_SWAP_CHAIN_DESC swap_desc = {};
		swap_desc.BufferCount = 1;
		const D3D12_RESOURCE_DESC source_desc = texture->m_pResource->GetDesc();
		swap_desc.BufferDesc.Width = static_cast<UINT>(source_desc.Width);
		swap_desc.BufferDesc.Height = source_desc.Height;
		swap_desc.BufferDesc.Format = source_desc.Format;
		swap_desc.SampleDesc = source_desc.SampleDesc;

		if (!runtime->on_init(swap_desc, texture->m_pResource))
		{
			LOG(ERROR) << "Failed to recreate Direct3D 12 runtime environment on runtime " << runtime.get() << '.';
			return;
		}

		s_last_source_tex = texture->m_pResource;
	}

	runtime->on_present();
}

static vr::EVRCompositorError IVRCompositor_Submit_007(vr::IVRCompositor *pCompositor, vr::EVREye eEye, unsigned int eTextureType, void *pTexture, const vr::VRTextureBounds_t *pBounds)
{
	switch (eTextureType)
	{
	case 0: // API_DirectX
		vr_submit_d3d11(eEye, static_cast<ID3D11Texture2D *>(pTexture), pBounds);
		break;
	case 1: // API_OpenGL
		vr_submit_gl(eEye, static_cast<GLuint>(reinterpret_cast<uintptr_t>(pTexture)), false, pBounds);
		break;
	}

	static const auto trampoline = reshade::hooks::call(IVRCompositor_Submit_007);
	return trampoline(pCompositor, eEye, eTextureType, pTexture, pBounds);
}
static vr::EVRCompositorError IVRCompositor_Submit_008(vr::IVRCompositor *pCompositor, vr::EVREye eEye, unsigned int eTextureType, void *pTexture, const vr::VRTextureBounds_t *pBounds, vr::EVRSubmitFlags nSubmitFlags)
{
	switch (eTextureType)
	{
	case 0: // API_DirectX
		vr_submit_d3d11(eEye, static_cast<ID3D11Texture2D *>(pTexture), pBounds);
		break;
	case 1: // API_OpenGL
		vr_submit_gl(eEye, static_cast<GLuint>(reinterpret_cast<uintptr_t>(pTexture)), false, pBounds);
		break;
	}

	static const auto trampoline = reshade::hooks::call(IVRCompositor_Submit_008);
	return trampoline(pCompositor, eEye, eTextureType, pTexture, pBounds, nSubmitFlags);
}
static vr::EVRCompositorError IVRCompositor_Submit_009(vr::IVRCompositor *pCompositor, vr::EVREye eEye, const vr::Texture_t *pTexture, const vr::VRTextureBounds_t *pBounds, vr::EVRSubmitFlags nSubmitFlags)
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

	static const auto trampoline = reshade::hooks::call(IVRCompositor_Submit_009);
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

	if (unsigned int compositor_version = 0;
		std::sscanf(pchInterfaceVersion, "IVRCompositor_%u", &compositor_version))
	{
		if (compositor_version >= 12)
			reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 5, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit_009));
		else if (compositor_version >= 9)
			reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 4, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit_009));
		else if (compositor_version == 8)
			reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 6, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit_008));
		else if (compositor_version == 7)
			reshade::hooks::install("IVRCompositor::Submit", vtable_from_instance(static_cast<vr::IVRCompositor *>(interface_instance)), 6, reinterpret_cast<reshade::hook::address>(IVRCompositor_Submit_007));
	}

	return interface_instance;
}

#endif
