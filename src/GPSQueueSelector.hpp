//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_GPSQUEUESELECTOR_HPP
#define VKHASHDAG_GPSQUEUESELECTOR_HPP

#include <myvk/Queue.hpp>
#include <myvk/Surface.hpp>

// Generic + Present + Sparse Queue
class GPSQueueSelector {
private:
	myvk::Ptr<myvk::Surface> m_surface;
	myvk::Ptr<myvk::Queue> *m_p_generic_queue, *m_p_sparse_queue;
	myvk::Ptr<myvk::PresentQueue> *m_p_present_queue;

public:
	GPSQueueSelector(myvk::Ptr<myvk::Queue> *p_generic_queue, myvk::Ptr<myvk::Queue> *p_sparse_queue,
	                 const myvk::Ptr<myvk::Surface> &surface, myvk::Ptr<myvk::PresentQueue> *p_present_queue)
	    : m_surface{surface}, m_p_generic_queue{p_generic_queue}, m_p_sparse_queue{p_sparse_queue},
	      m_p_present_queue{p_present_queue} {}

	std::vector<myvk::QueueSelection> operator()(const myvk::Ptr<const myvk::PhysicalDevice> &physical_device) const;
};

#endif // VKHASHDAG_GPSQUEUESELECTOR_HPP
