#include "myvk/DescriptorSet.hpp"

namespace myvk {

DescriptorSetWrite DescriptorSetWrite::WriteBuffers(const Ptr<DescriptorSet> &descriptor_set,
                                                    std::vector<VkDescriptorBufferInfo> buffer_infos,
                                                    VkDescriptorType type, uint32_t binding, uint32_t array_element) {
	DescriptorSetWrite ret;
	ret.buffer_infos = std::move(buffer_infos);
	ret.write = VkWriteDescriptorSet{
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptor_set ? descriptor_set->GetHandle() : VK_NULL_HANDLE,
	    .dstBinding = binding,
	    .dstArrayElement = array_element,
	    .descriptorCount = (uint32_t)ret.buffer_infos.size(),
	    .descriptorType = type,
	    // .pBufferInfo = ret.buffer_infos.data(), // Ignored, will process in GetVkWriteDescriptorSet
	};
	return ret;
}
DescriptorSetWrite DescriptorSetWrite::WriteImages(const Ptr<DescriptorSet> &descriptor_set,
                                                   std::vector<VkDescriptorImageInfo> image_infos,
                                                   VkDescriptorType type, uint32_t binding, uint32_t array_element) {
	DescriptorSetWrite ret;
	ret.image_infos = std::move(image_infos);
	ret.write = VkWriteDescriptorSet{
	    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	    .dstSet = descriptor_set ? descriptor_set->GetHandle() : VK_NULL_HANDLE,
	    .dstBinding = binding,
	    .dstArrayElement = array_element,
	    .descriptorCount = (uint32_t)ret.image_infos.size(),
	    .descriptorType = type,
	    // .pImageInfo = ret.image_infos.data(), // Ignored, will process in GetVkWriteDescriptorSet
	};
	return ret;
}
DescriptorSetWrite DescriptorSetWrite::WriteUniformBuffer(const Ptr<DescriptorSet> &descriptor_set,
                                                          const Ptr<BufferBase> &buffer, uint32_t binding,
                                                          uint32_t array_element, VkDeviceSize offset,
                                                          VkDeviceSize range) {
	return WriteBuffers(descriptor_set,
	                    {VkDescriptorBufferInfo{
	                        .buffer = buffer->GetHandle(),
	                        .offset = offset,
	                        .range = range,
	                    }},
	                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, binding, array_element);
}
DescriptorSetWrite DescriptorSetWrite::WriteCombinedImageSampler(const Ptr<DescriptorSet> &descriptor_set,
                                                                 const Ptr<Sampler> &sampler,
                                                                 const Ptr<ImageView> &image_view, uint32_t binding,
                                                                 uint32_t array_element) {
	return WriteImages(descriptor_set,
	                   {VkDescriptorImageInfo{
	                       .sampler = sampler->GetHandle(),
	                       .imageView = image_view->GetHandle(),
	                       .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   }},
	                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, array_element);
}
DescriptorSetWrite DescriptorSetWrite::WriteStorageBuffer(const Ptr<DescriptorSet> &descriptor_set,
                                                          const Ptr<BufferBase> &buffer, uint32_t binding,
                                                          uint32_t array_element, VkDeviceSize offset,
                                                          VkDeviceSize range) {
	return WriteBuffers(descriptor_set,
	                    {VkDescriptorBufferInfo{
	                        .buffer = buffer->GetHandle(),
	                        .offset = offset,
	                        .range = range,
	                    }},
	                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, binding, array_element);
}
DescriptorSetWrite DescriptorSetWrite::WriteStorageImage(const Ptr<DescriptorSet> &descriptor_set,
                                                         const Ptr<ImageView> &image_view, uint32_t binding,
                                                         uint32_t array_element) {
	return WriteImages(descriptor_set,
	                   {VkDescriptorImageInfo{
	                       .imageView = image_view->GetHandle(),
	                       .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
	                   }},
	                   VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, binding, array_element);
}
DescriptorSetWrite DescriptorSetWrite::WriteSampledImage(const Ptr<DescriptorSet> &descriptor_set,
                                                         const Ptr<ImageView> &image_view, uint32_t binding,
                                                         uint32_t array_element) {
	return WriteImages(descriptor_set,
	                   {VkDescriptorImageInfo{
	                       .imageView = image_view->GetHandle(),
	                       .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   }},
	                   VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, binding, array_element);
}
DescriptorSetWrite DescriptorSetWrite::WriteInputAttachment(const Ptr<DescriptorSet> &descriptor_set,
                                                            const Ptr<ImageView> &image_view, uint32_t binding,
                                                            uint32_t array_element) {
	return WriteImages(descriptor_set,
	                   {VkDescriptorImageInfo{
	                       .imageView = image_view->GetHandle(),
	                       .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   }},
	                   VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, binding, array_element);
}

VkWriteDescriptorSet DescriptorSetWrite::GetVkWriteDescriptorSet() const {
	VkWriteDescriptorSet ret = this->write;
	ret.pBufferInfo = buffer_infos.data();
	ret.pImageInfo = image_infos.data();
	return ret;
}

std::vector<VkWriteDescriptorSet>
DescriptorSetWrite::GetVkWriteDescriptorSets(const std::vector<DescriptorSetWrite> &writes) {
	std::vector<VkWriteDescriptorSet> ret(writes.size());
	for (std::size_t i = 0; i < writes.size(); ++i)
		ret[i] = writes[i].GetVkWriteDescriptorSet();
	return ret;
}

Ptr<DescriptorSet> DescriptorSet::Create(const Ptr<DescriptorPool> &descriptor_pool,
                                         const Ptr<DescriptorSetLayout> &descriptor_set_layout) {
	auto ret = std::make_shared<DescriptorSet>();
	ret->m_descriptor_pool_ptr = descriptor_pool;
	ret->m_descriptor_set_layout_ptr = descriptor_set_layout;

	VkDescriptorSetAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = descriptor_pool->GetHandle();
	alloc_info.descriptorSetCount = 1;
	VkDescriptorSetLayout layout = descriptor_set_layout->GetHandle();
	alloc_info.pSetLayouts = &layout;

	if (vkAllocateDescriptorSets(descriptor_pool->GetDevicePtr()->GetHandle(), &alloc_info, &ret->m_descriptor_set) !=
	    VK_SUCCESS)
		return nullptr;
	return ret;
}

std::vector<Ptr<DescriptorSet>>
DescriptorSet::CreateMultiple(const Ptr<DescriptorPool> &descriptor_pool,
                              const std::vector<Ptr<DescriptorSetLayout>> &descriptor_set_layouts) {
	uint32_t count = descriptor_set_layouts.size();

	VkDescriptorSetAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	alloc_info.descriptorPool = descriptor_pool->GetHandle();
	alloc_info.descriptorSetCount = count;
	std::vector<VkDescriptorSetLayout> layouts(count);
	for (uint32_t i = 0; i < count; ++i)
		layouts[i] = descriptor_set_layouts[i]->GetHandle();
	alloc_info.pSetLayouts = layouts.data();

	std::vector<VkDescriptorSet> handles(count);
	if (vkAllocateDescriptorSets(descriptor_pool->GetDevicePtr()->GetHandle(), &alloc_info, handles.data()) !=
	    VK_SUCCESS)
		return {};

	std::vector<Ptr<DescriptorSet>> ret(count);
	for (uint32_t i = 0; i < count; ++i) {
		auto ptr = std::make_shared<DescriptorSet>();
		ptr->m_descriptor_pool_ptr = descriptor_pool;
		ptr->m_descriptor_set_layout_ptr = descriptor_set_layouts[i];
		ptr->m_descriptor_set = handles[i];

		ret[i] = ptr;
	}
	return ret;
}

void DescriptorSet::UpdateUniformBuffer(const Ptr<BufferBase> &buffer, uint32_t binding, uint32_t array_element,
                                        VkDeviceSize offset, VkDeviceSize range) const {
	VkDescriptorBufferInfo info = {};
	info.buffer = buffer->GetHandle();
	info.range = range;
	info.offset = offset;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_descriptor_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_element;
	write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write.descriptorCount = 1;
	write.pBufferInfo = &info;

	vkUpdateDescriptorSets(GetDevicePtr()->GetHandle(), 1, &write, 0, nullptr);
}

void DescriptorSet::UpdateStorageBuffer(const Ptr<BufferBase> &buffer, uint32_t binding, uint32_t array_element,
                                        VkDeviceSize offset, VkDeviceSize range) const {
	VkDescriptorBufferInfo info = {};
	info.buffer = buffer->GetHandle();
	info.range = range;
	info.offset = offset;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_descriptor_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_element;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	write.descriptorCount = 1;
	write.pBufferInfo = &info;

	vkUpdateDescriptorSets(GetDevicePtr()->GetHandle(), 1, &write, 0, nullptr);
}

void DescriptorSet::UpdateCombinedImageSampler(const Ptr<Sampler> &sampler, const Ptr<ImageView> &image_view,
                                               uint32_t binding, uint32_t array_element) const {
	VkDescriptorImageInfo info = {};
	info.imageView = image_view->GetHandle();
	info.sampler = sampler->GetHandle();
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_descriptor_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_element;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.descriptorCount = 1;
	write.pImageInfo = &info;

	vkUpdateDescriptorSets(GetDevicePtr()->GetHandle(), 1, &write, 0, nullptr);
}

void DescriptorSet::UpdateStorageImage(const Ptr<ImageView> &image_view, uint32_t binding,
                                       uint32_t array_element) const {
	VkDescriptorImageInfo info = {};
	info.imageView = image_view->GetHandle();
	info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_descriptor_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_element;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	write.descriptorCount = 1;
	write.pImageInfo = &info;

	vkUpdateDescriptorSets(GetDevicePtr()->GetHandle(), 1, &write, 0, nullptr);
}

void DescriptorSet::UpdateInputAttachment(const Ptr<ImageView> &image_view, uint32_t binding,
                                          uint32_t array_element) const {
	VkDescriptorImageInfo info = {};
	info.imageView = image_view->GetHandle();
	info.sampler = VK_NULL_HANDLE;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	write.dstSet = m_descriptor_set;
	write.dstBinding = binding;
	write.dstArrayElement = array_element;
	write.descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
	write.descriptorCount = 1;
	write.pImageInfo = &info;

	vkUpdateDescriptorSets(GetDevicePtr()->GetHandle(), 1, &write, 0, nullptr);
}

void DescriptorSet::Update(const Ptr<Device> &device, const DescriptorSetWrite &write) {
	auto vk_write = write.GetVkWriteDescriptorSet();
	vkUpdateDescriptorSets(device->GetHandle(), 1, &vk_write, 0, nullptr);
}

void DescriptorSet::Update(const Ptr<Device> &device, const std::vector<DescriptorSetWrite> &writes) {
	auto vk_writes = DescriptorSetWrite::GetVkWriteDescriptorSets(writes);
	vkUpdateDescriptorSets(device->GetHandle(), vk_writes.size(), vk_writes.data(), 0, nullptr);
}

DescriptorSet::~DescriptorSet() {
	if (m_descriptor_set)
		vkFreeDescriptorSets(m_descriptor_pool_ptr->GetDevicePtr()->GetHandle(), m_descriptor_pool_ptr->GetHandle(), 1,
		                     &m_descriptor_set);
}
} // namespace myvk
