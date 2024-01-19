//
// Created by adamyuan on 1/16/24.
//

#pragma once
#ifndef VKHASHDAG_BUCKETMUTEX_HPP
#define VKHASHDAG_BUCKETMUTEX_HPP

#include <shared_mutex>

namespace hashdag {

using EditMutex = std::mutex;

}

#endif // VKHASHDAG_BUCKETMUTEX_HPP
