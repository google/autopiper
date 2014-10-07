/*
 * Copyright 2014 Google Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _AUTOPIPER_UTIL_H_
#define _AUTOPIPER_UTIL_H_

#include <memory>
#include <vector>
#include <string>
#include <stdio.h>
#include <stdarg.h>

inline std::string strprintf(const char* fmt, ...) {
    char buf[4096];
    va_list va;
    va_start(va, fmt);
    vsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);
    return std::string(buf);
}

template<typename T>
inline T* PrependOwnedToVector(
        std::vector<std::unique_ptr<T>>& v,
        T* new_value) {
    v.push_back(std::move(std::unique_ptr<T>(new_value)));
    for (unsigned i = v.size() - 1; i != 0; --i) {
        std::swap(v[i-1], v[i]);
    }
    return new_value;
}

template<typename T>
inline T* AppendOwnedToVector(
        std::vector<std::unique_ptr<T>>& v,
        T* new_value) {
    v.push_back(std::move(std::unique_ptr<T>(new_value)));
    return new_value;
}

template<typename T>
inline T* InsertOwnedBefore(
        std::vector<std::unique_ptr<T>>& v,
        T* before_this,
        T* new_value) {
    v.push_back(std::move(std::unique_ptr<T>(new_value)));
    for (unsigned i = v.size() - 1; i != 0; --i) {
        std::swap(v[i-1], v[i]);
        if (v[i].get() == before_this) {
            break;
        }
    }
    return new_value;
}

#endif
