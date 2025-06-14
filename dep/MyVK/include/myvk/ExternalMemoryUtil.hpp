//
// Created by adamyuan on 4/1/25.
//

#pragma once
#ifndef MYVK_EXTERNALMEMORYUTIL_HPP
#define MYVK_EXTERNALMEMORYUTIL_HPP

#include "volk.h"

namespace myvk {

constexpr VkExternalMemoryHandleTypeFlagBits GetExternalMemoryHandleType() {
#ifdef _WIN64
	return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
	return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
}

constexpr const char *GetExternalMemoryExtensionName() {
#ifdef _WIN64
	return VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
#else
	return VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
#endif
}

} // namespace myvk

#endif
