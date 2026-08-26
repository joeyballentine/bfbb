#include "xMath2.h"

// Defining a static member of an explicit specialization needs template<>;
// CodeWarrior takes it either way.
template <>
const basic_rect<F32> basic_rect<F32>::m_Null = { 0.0f, 0.0f, 0.0f, 0.0f };
template <>
const basic_rect<F32> basic_rect<F32>::m_Unit = { 0.0f, 0.0f, 1.0f, 1.0f };