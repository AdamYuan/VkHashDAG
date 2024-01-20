//
// Created by adamyuan on 1/19/24.
//

#pragma once
#ifndef VKHASHDAG_COLORSVO_COLORBLOCK_HPP
#define VKHASHDAG_COLORSVO_COLORBLOCK_HPP

namespace colorsvo {

template <typename T>
concept ColorBlock = requires(const T c) {
	{ c.(NodeCoord<Word>{}, NodePointer<Word>{}) } -> std::convertible_to<EditType>;
	{ ce.EditVoxel(NodeCoord<Word>{}, bool{}) } -> std::convertible_to<bool>;
};

}

#endif // VKHASHDAG_COLORSVO_COLORBLOCK_HPP
