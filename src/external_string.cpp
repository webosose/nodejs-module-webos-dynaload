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

#include "external_string.h"

#include <boost/filesystem.hpp>

#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;
using namespace v8;

namespace bf = boost::filesystem;
namespace bi = boost::interprocess;

// Simple function to check a buffer for non-ASCII characters. If this check
// turns out to be significant in the time it takes to load a file it could
// have its loops unrolled or rewritten in assembly.
static bool characterBufferIsASCII(const char* startPtr, const char* limitPtr)
{
	bool isAscii = true;
	while(startPtr < limitPtr) {
		if (!isascii(*startPtr)) {
			isAscii = false;
			break;
		}
		startPtr++;
	}
	return isAscii;
}

// createV8StringFromFile uses memory mapping to examine the contents of a file. If
// the contents are strictly ASCII it creates a v8 ASCII external string resource
// using the mapped region. Otherwise it allows v8 to convert the string into a regular,
// heap-based string.
v8::Local<v8::String> createV8StringFromFile(const char* inPathToFile)
{
	bf::path pathToFile(inPathToFile);
	// file_size throws an exception if the file doesn't exist. While this
	// is handled by the higher levels, it might be better to catch the boost
	// exception and throw a runtime exception with a less boost-flavored message.
	boost::uintmax_t fileSize = bf::file_size(pathToFile);
	if (fileSize == 0) {
		return String::NewFromUtf8(v8::Isolate::GetCurrent(), "");
	}
	bi::file_mapping mappedFile(inPathToFile, bi::read_only);
	bi::mapped_region region(mappedFile, bi::read_only);
	const char *startPtr = static_cast<const char*>(region.get_address());
	const char *limitPtr = startPtr + fileSize;
	bool isASCII = characterBufferIsASCII(startPtr, limitPtr);
	if (isASCII) {
		// While we'd like to pass the already mapped region to the
		// MappedRegionExternalString class, boost's mapping classes cannot be
		// copied.
		return MappedRegionExternalString::create(inPathToFile);
	}
	// Let v8 do its normal string conversion. This should be a rare case, as there's
	// no good reason for a JavaScript source file to have anything but ASCII.
    return String::NewFromUtf8(v8::Isolate::GetCurrent(), startPtr);
}

// Wrapper function to create a v8 external string.
v8::Local<v8::String> MappedRegionExternalString::create(const char* pathToFile)
{
	Isolate* isolate = Isolate::GetCurrent();
	MappedRegionExternalString* extString = new MappedRegionExternalString(pathToFile);
	return String::NewExternal(isolate, extString);
}

// The boost constructors do all the hard work of mapping the region and unmapping it when this object is destroyed.
MappedRegionExternalString::MappedRegionExternalString(const char* pathToFile) : fMappedFile(pathToFile, bi::read_only), fRegion(fMappedFile, bi::read_only)
{
}

const char* MappedRegionExternalString::data() const
{
	return static_cast<const char*>(fRegion.get_address());
}

size_t MappedRegionExternalString::length() const
{
	return fRegion.get_size();
}


