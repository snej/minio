//
//  mini/base.hh
//
// Copyright © 2025 Jens Alfke. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include <string>
#include <string_view>

#if __has_include("betterassert.hh")
#include "betterassert.hh"
#else
#include <cassert>
#endif


#ifndef __has_attribute
#  define __has_attribute(A) 0
#endif

#ifndef __has_feature
#  define __has_feature(F) 0
#endif

#ifndef __has_builtin
#  define __has_builtin(B) 0
#endif


#if __has_feature(nullability)
#  define ASSUME_NONNULL_BEGIN  _Pragma("clang assume_nonnull begin")
#  define ASSUME_NONNULL_END    _Pragma("clang assume_nonnull end")
#else
#  define ASSUME_NONNULL_BEGIN
#  define ASSUME_NONNULL_END
#  define _Nullable
#  ifndef _Nonnull
#    define _Nonnull
#  endif
#endif


#if (defined(__GNUC__) || __has_attribute(__pure__)) && !defined(CPPCHECK)
    #define pure                      __attribute__((__pure__))
#else
    #define pure
#endif


namespace snej::minio {
    using std::string;
    using std::string_view;
}
