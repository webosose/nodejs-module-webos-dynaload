// Copyright (c) 2010-2018 LG Electronics, Inc.
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
// SPDX-License-Identifier: Apache-2.0

#ifndef UTF8_EXTERNAL_STRING_H
#define UTF8_EXTERNAL_STRING_H

#include <v8.h>

#include <vector>
#include <boost/smart_ptr.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/file_mapping.hpp>

v8::Local<v8::String> createV8StringFromFile(const char* pathToFile);

class MappedRegionExternalString : public v8::String::ExternalOneByteStringResource {
public:
	static v8::Local<v8::String> create(const char* pathToFile);
	MappedRegionExternalString(const char* pathToFile);
    virtual const char* data() const;
    virtual size_t length() const;
private:
	boost::interprocess::file_mapping fMappedFile;
	boost::interprocess::mapped_region fRegion;
};

#endif
