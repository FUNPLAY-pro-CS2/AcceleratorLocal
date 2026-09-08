/**
 * ======================================================
 * Accelerator local
 * Written by Slynx (˙·٠● S l y n x ●٠·˙) 2026, Phoenix (˙·٠●Феникс●٠·˙) 2023-2025, Asher Baker (asherkin) 2011.
 * ======================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 * 
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from 
 * the use of this software.
 */

#ifndef _INCLUDE_ACCELERATOR_LOCAL_UTILS_H_
#define _INCLUDE_ACCELERATOR_LOCAL_UTILS_H_

#include <type_traits>

// Mem-initializer for a hook held through a plain pointer: the hook's type is
// already spelled out on the member's declaration, so it is taken from there
// instead of being repeated -- or deduced, which MSVC fails at for KHook's
// manual-index and some member-function-pointer constructors.
//   CFoo::CFoo() : KHOOK_NEW(m_hThink, 52u, this, &CFoo::Pre, &CFoo::Post) {}
#define KHOOK_NEW(member, ...) member(new std::remove_pointer_t<decltype(member)>(__VA_ARGS__))

#endif //_INCLUDE_ACCELERATOR_LOCAL_UTILS_H_
