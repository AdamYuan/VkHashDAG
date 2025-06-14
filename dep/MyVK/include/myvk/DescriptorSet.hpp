#ifndef MYVK_DESCRIPTOR_SET_HPP
#define MYVK_DESCRIPTOR_SET_HPP

#include "BufferBase.hpp"
#include "DescriptorPool.hpp"
#include "DescriptorSetLayout.hpp"
#include "DeviceObjectBase.hpp"
#include "ImageView.hpp"
#include "Sampler.hpp"

#include <memory>

namespace myvk {

class DescriptorSet;

struct DescriptorSetWrite {
	std::vector<VkDescriptorBufferInfo> buffer_infos;
	std::vector<VkDescriptorImageInfo> image_infos;
	VkWriteDescriptorSet write;

	static DescriptorSetWrite WriteBuffers(const Ptr<DescriptorSet> &descriptor_set,
	                                       std::vector<VkDescriptorBufferInfo> buffer_infos, VkDescriptorType type,
	                                       uint32_t binding, uint32_t array_element = 0);
	static DescriptorSetWrite WriteImages(const Ptr<DescriptorSet> &descriptor_set,
	                                      std::vector<VkDescriptorImageInfo> image_infos, VkDescriptorType type,
	                                      uint32_t binding, uint32_t array_element = 0);
	static DescriptorSetWrite WriteUniformBuffer(const Ptr<DescriptorSet> &descriptor_set,
	                                             const Ptr<BufferBase> &buffer, uint32_t binding,
	                                             uint32_t array_element = 0, VkDeviceSize offset = 0,
	                                             VkDeviceSize range = VK_WHOLE_SIZE);
	static DescriptorSetWrite WriteCombinedImageSampler(const Ptr<DescriptorSet> &descriptor_set,
	                                                    const Ptr<Sampler> &sampler, const Ptr<ImageView> &image_view,
	                                                    uint32_t binding, uint32_t array_element = 0);
	static DescriptorSetWrite WriteStorageBuffer(const Ptr<DescriptorSet> &descriptor_set,
	                                             const Ptr<BufferBase> &buffer, uint32_t binding,
	                                             uint32_t array_element = 0, VkDeviceSize offset = 0,
	                                             VkDeviceSize range = VK_WHOLE_SIZE);
	static DescriptorSetWrite WriteStorageImage(const Ptr<DescriptorSet> &descriptor_set,
	                                            const Ptr<ImageView> &image_view, uint32_t binding,
	                                            uint32_t array_element = 0);
	static DescriptorSetWrite WriteSampledImage(const Ptr<DescriptorSet> &descriptor_set,
	                                            const Ptr<ImageView> &image_view, uint32_t binding,
	                                            uint32_t array_element = 0);
	static DescriptorSetWrite WriteInputAttachment(const Ptr<DescriptorSet> &descriptor_set,
	                                               const Ptr<ImageView> &image_view, uint32_t binding,
	                                               uint32_t array_element = 0);

	VkWriteDescriptorSet GetVkWriteDescriptorSet() const;
	static std::vector<VkWriteDescriptorSet> GetVkWriteDescriptorSets(const std::vector<DescriptorSetWrite> &writes);
};

class DescriptorSet : public DeviceObjectBase {
private:
	Ptr<DescriptorPool> m_descriptor_pool_ptr;
	Ptr<DescriptorSetLayout> m_descriptor_set_layout_ptr;

	VkDescriptorSet m_descriptor_set{VK_NULL_HANDLE};

public:
	static Ptr<DescriptorSet> Create(const Ptr<DescriptorPool> &descriptor_pool,
	                                 const Ptr<DescriptorSetLayout> &descriptor_set_layout);

	static std::vector<Ptr<DescriptorSet>>
	CreateMultiple(const Ptr<DescriptorPool> &descriptor_pool,
	               const std::vector<Ptr<DescriptorSetLayout>> &descriptor_set_layouts);

	void UpdateUniformBuffer(const Ptr<BufferBase> &buffer, uint32_t binding, uint32_t array_element = 0,
	                         VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE) const;

	void UpdateCombinedImageSampler(const Ptr<Sampler> &sampler, const Ptr<ImageView> &image_view, uint32_t binding,
	                                uint32_t array_element = 0) const;

	void UpdateStorageBuffer(const Ptr<BufferBase> &buffer, uint32_t binding, uint32_t array_element = 0,
	                         VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE) const;

	void UpdateStorageImage(const Ptr<ImageView> &image_view, uint32_t binding, uint32_t array_element = 0) const;
	void UpdateInputAttachment(const Ptr<ImageView> &image_view, uint32_t binding, uint32_t array_element = 0) const;

	static void Update(const Ptr<Device> &device, const DescriptorSetWrite &write);
	static void Update(const Ptr<Device> &device, const std::vector<DescriptorSetWrite> &writes);

	VkDescriptorSet GetHandle() const { return m_descriptor_set; }

	const Ptr<Device> &GetDevicePtr() const override { return m_descriptor_pool_ptr->GetDevicePtr(); }

	const Ptr<DescriptorPool> &GetDescriptorPoolPtr() const { return m_descriptor_pool_ptr; }

	const Ptr<DescriptorSetLayout> &GetDescriptorSetLayoutPtr() const { return m_descriptor_set_layout_ptr; }

	~DescriptorSet() override;
};
} // namespace myvk

#endif
