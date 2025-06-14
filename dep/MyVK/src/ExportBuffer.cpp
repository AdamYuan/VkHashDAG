#include <myvk/ExportBuffer.hpp>

#include <myvk/ExternalMemoryUtil.hpp>
#include <myvk/Queue.hpp>
#include <set>

#ifdef _WIN64
#include <VersionHelpers.h>
#include <aclapi.h>
#include <dxgi1_2.h>
#endif /* _WIN64 */

namespace myvk {

ExportBuffer::~ExportBuffer() {
	if (m_buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(m_device_ptr->GetHandle(), m_buffer, nullptr);
		m_buffer = VK_NULL_HANDLE;
	}
	if (m_device_memory != VK_NULL_HANDLE) {
		vkFreeMemory(m_device_ptr->GetHandle(), m_device_memory, nullptr);
		m_device_memory = VK_NULL_HANDLE;
	}
}

void ExportBuffer::DestroyHandle(const Ptr<Device> &device, Handle handle) {
	if (handle.buffer != VK_NULL_HANDLE)
		vkDestroyBuffer(device->GetHandle(), handle.buffer, nullptr);
	if (handle.device_memory != VK_NULL_HANDLE)
		vkFreeMemory(device->GetHandle(), handle.device_memory, nullptr);
}

#ifdef _WIN64
namespace {
class WindowsSecurityAttributes {
protected:
	SECURITY_ATTRIBUTES m_winSecurityAttributes;
	PSECURITY_DESCRIPTOR m_winPSecurityDescriptor;

public:
	WindowsSecurityAttributes();
	~WindowsSecurityAttributes();
	SECURITY_ATTRIBUTES *operator&();
};

WindowsSecurityAttributes::WindowsSecurityAttributes() {
	m_winPSecurityDescriptor = (PSECURITY_DESCRIPTOR)calloc(1, SECURITY_DESCRIPTOR_MIN_LENGTH + 2 * sizeof(void **));

	PSID *ppSID = (PSID *)((PBYTE)m_winPSecurityDescriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
	PACL *ppACL = (PACL *)((PBYTE)ppSID + sizeof(PSID *));

	InitializeSecurityDescriptor(m_winPSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);

	SID_IDENTIFIER_AUTHORITY sidIdentifierAuthority = SECURITY_WORLD_SID_AUTHORITY;
	AllocateAndInitializeSid(&sidIdentifierAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, ppSID);

	EXPLICIT_ACCESS explicitAccess;
	ZeroMemory(&explicitAccess, sizeof(EXPLICIT_ACCESS));
	explicitAccess.grfAccessPermissions = STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL;
	explicitAccess.grfAccessMode = SET_ACCESS;
	explicitAccess.grfInheritance = INHERIT_ONLY;
	explicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
	explicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
	explicitAccess.Trustee.ptstrName = (LPTSTR)*ppSID;

	SetEntriesInAcl(1, &explicitAccess, NULL, ppACL);

	SetSecurityDescriptorDacl(m_winPSecurityDescriptor, TRUE, *ppACL, FALSE);

	m_winSecurityAttributes.nLength = sizeof(m_winSecurityAttributes);
	m_winSecurityAttributes.lpSecurityDescriptor = m_winPSecurityDescriptor;
	m_winSecurityAttributes.bInheritHandle = TRUE;
}

WindowsSecurityAttributes::~WindowsSecurityAttributes() {
	PSID *ppSID = (PSID *)((PBYTE)m_winPSecurityDescriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
	PACL *ppACL = (PACL *)((PBYTE)ppSID + sizeof(PSID *));

	if (*ppSID) {
		FreeSid(*ppSID);
	}
	if (*ppACL) {
		LocalFree(*ppACL);
	}
	free(m_winPSecurityDescriptor);
}

SECURITY_ATTRIBUTES *WindowsSecurityAttributes::operator&() { return &m_winSecurityAttributes; }
} // namespace
#endif

ExportBuffer::Handle ExportBuffer::CreateHandle(const Ptr<Device> &device, VkDeviceSize size, VkBufferUsageFlags usage,
                                                VkMemoryPropertyFlags memory_properties,
                                                const std::vector<Ptr<Queue>> &access_queues) {
	Handle ret{};

	// Create Buffer
	VkExternalMemoryHandleTypeFlagBits ext_handle_type = GetExternalMemoryHandleType();
	VkExternalMemoryBufferCreateInfo external_create_info = {
	    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
	    .handleTypes = (VkExternalMemoryHandleTypeFlags)ext_handle_type,
	};
	VkBufferCreateInfo buffer_create_info{
	    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	    .pNext = &external_create_info,
	    .size = size,
	    .usage = usage,
	};
	std::set<uint32_t> queue_family_set;
	for (auto &i : access_queues)
		queue_family_set.insert(i->GetFamilyIndex());
	std::vector<uint32_t> queue_families{queue_family_set.begin(), queue_family_set.end()};
	if (queue_families.size() <= 1) {
		buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	} else {
		buffer_create_info.sharingMode = VK_SHARING_MODE_CONCURRENT;
		buffer_create_info.queueFamilyIndexCount = queue_families.size();
		buffer_create_info.pQueueFamilyIndices = queue_families.data();
	}

	if (vkCreateBuffer(device->GetHandle(), &buffer_create_info, nullptr, &ret.buffer) != VK_SUCCESS) {
		DestroyHandle(device, ret);
		return {};
	}

	VkMemoryRequirements mem_requirements;
	vkGetBufferMemoryRequirements(device->GetHandle(), ret.buffer, &mem_requirements);

	// Create Allocation
#ifdef _WIN64
	WindowsSecurityAttributes win_security_attributes;

	VkExportMemoryWin32HandleInfoKHR export_win32_handle_info = {
	    .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
	export_win32_handle_info.pAttributes = &win_security_attributes;
	export_win32_handle_info.dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
	export_win32_handle_info.name = (LPCWSTR) nullptr;
#endif

	VkExportMemoryAllocateInfoKHR export_alloc_info = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO_KHR};

#ifdef _WIN64
	export_alloc_info.pNext =
	    ext_handle_type & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR ? &export_win32_handle_info : nullptr;
	export_alloc_info.handleTypes = ext_handle_type;
#else
	export_alloc_info.pNext = nullptr;
	export_alloc_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

	VkMemoryAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	alloc_info.pNext = &export_alloc_info;
	alloc_info.allocationSize = mem_requirements.size;
	auto opt_memory_type_index =
	    device->GetPhysicalDevicePtr()->FindMemoryType(mem_requirements.memoryTypeBits, memory_properties);
	if (!opt_memory_type_index) {
		DestroyHandle(device, ret);
		return {};
	}
	alloc_info.memoryTypeIndex = *opt_memory_type_index;

	if (vkAllocateMemory(device->GetHandle(), &alloc_info, nullptr, &ret.device_memory) != VK_SUCCESS) {
		DestroyHandle(device, ret);
		return {};
	}

	// Bind Buffer
	vkBindBufferMemory(device->GetHandle(), ret.buffer, ret.device_memory, 0);

	// Get Memory Handle
#ifdef _WIN64
	HANDLE handle = 0;
	VkMemoryGetWin32HandleInfoKHR mem_get_win32_handle_info = {};
	mem_get_win32_handle_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
	mem_get_win32_handle_info.pNext = nullptr;
	mem_get_win32_handle_info.memory = ret.device_memory;
	mem_get_win32_handle_info.handleType = ext_handle_type;
	if (vkGetMemoryWin32HandleKHR(device->GetHandle(), &mem_get_win32_handle_info, &handle) != VK_SUCCESS) {
		throw std::runtime_error("Failed to retrieve handle for buffer!");
	}
	ret.mem_handle = (void *)handle;
#else
	int fd = -1;
	VkMemoryGetFdInfoKHR mem_get_fd_info = {
	    .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
	    .memory = ret.device_memory,
	    .handleType = ext_handle_type,
	};
	if (vkGetMemoryFdKHR(device->GetHandle(), &mem_get_fd_info, &fd) != VK_SUCCESS) {
		DestroyHandle(device, ret);
		return {};
	}
	ret.mem_handle = (void *)(uintptr_t)fd;
#endif

	return ret;
}

Ptr<ExportBuffer> ExportBuffer::Create(const Ptr<Device> &device, VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkMemoryPropertyFlags memory_properties,
                                       const std::vector<Ptr<Queue>> &access_queues) {
	auto handle = CreateHandle(device, size, usage, memory_properties, access_queues);
	if (!handle.IsValid())
		return nullptr;

	auto ret = std::make_shared<ExportBuffer>();
	ret->m_device_ptr = device;
	ret->m_size = size;
	ret->m_usage = usage;

	ret->m_buffer = handle.buffer;
	ret->m_device_memory = handle.device_memory;
	ret->m_mem_handle = handle.mem_handle;

	return ret;
}

} // namespace myvk