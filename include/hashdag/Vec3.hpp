//
// Created by adamyuan on 1/15/24.
//

#pragma once
#ifndef VKHASHDAG_VEC3_HPP
#define VKHASHDAG_VEC3_HPP

namespace hashdag {

template <typename Type> struct Vec3 {
	Type x, y, z;
	inline bool Any(auto &&compare, const Vec3 &r) const {
		return compare(x, r.x) || compare(y, r.y) || compare(z, r.z);
	}
	inline bool All(auto &&compare, const Vec3 &r) const {
		return compare(x, r.x) && compare(y, r.y) && compare(z, r.z);
	}
};

} // namespace hashdag

#endif // VKHASHDAG_VEC3_HPP
