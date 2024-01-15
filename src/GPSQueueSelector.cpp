//
// Created by adamyuan on 1/15/24.
//

#include "GPSQueueSelector.hpp"
#include <optional>

std::vector<myvk::QueueSelection>
GPSQueueSelector::operator()(const myvk::Ptr<const myvk::PhysicalDevice> &physical_device) const {
	const auto &families = physical_device->GetQueueFamilyProperties();
	if (families.empty())
		return {};

	std::optional<uint32_t> generic_queue_family, sparse_queue_family, present_queue_family;

	// generic queue
	for (uint32_t i = 0; i < families.size(); ++i) {
		VkQueueFlags flags = families[i].queueFlags;
		if ((flags & VK_QUEUE_GRAPHICS_BIT) && (flags & VK_QUEUE_TRANSFER_BIT) && (flags & VK_QUEUE_COMPUTE_BIT)) {
			generic_queue_family = i;
			if (physical_device->GetQueueSurfaceSupport(i, m_surface)) {
				present_queue_family = i;
				break;
			}
		}
	}

	// find sparse queue
	for (uint32_t i = 0; i < families.size(); ++i) {
		VkQueueFlags flags = families[i].queueFlags;
		if (flags & VK_QUEUE_SPARSE_BINDING_BIT) {
			sparse_queue_family = i;
			if (generic_queue_family && i != *generic_queue_family)
				break;
		}
	}

	// present queue fallback
	if (!present_queue_family.has_value())
		for (uint32_t i = 0; i < families.size(); ++i) {
			if (physical_device->GetQueueSurfaceSupport(i, m_surface)) {
				present_queue_family = i;
				break;
			}
		}

	if (generic_queue_family && sparse_queue_family && present_queue_family)
		return {{m_surface, m_p_present_queue, *present_queue_family, 0},
		        {m_p_generic_queue, *generic_queue_family, 0},
		        {m_p_sparse_queue, *sparse_queue_family, 1}};
	return {};
}
