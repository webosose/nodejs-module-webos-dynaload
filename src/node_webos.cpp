// Copyright (c) 2010-2020 LG Electronics, Inc.
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

#include <boost/filesystem.hpp>
#include <node.h>
#include <v8.h>

#include "external_string.h"

namespace bf = boost::filesystem;

using namespace v8;
using namespace std;

const char* kFileNameGlobal="__filename";
const char* kDirNameGlobal="__dirname";

static void SetFileAndDirectoryGlobals(Local<Object> global, const char* path)
{
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	bf::path pathToFile(bf::system_complete(bf::path(path)));
	bf::path pathToParentDir(pathToFile.parent_path());
	Local<String> fileName = v8::String::NewFromUtf8(isolate, pathToFile.string().c_str()).ToLocalChecked();
	global->Set(isolate->GetCurrentContext(),
		v8::String::NewFromUtf8(isolate, kFileNameGlobal, v8::NewStringType::kInternalized).ToLocalChecked(),
		fileName);
	Local<String> dirName = v8::String::NewFromUtf8(isolate, pathToParentDir.string().c_str()).ToLocalChecked();
	global->Set(isolate->GetCurrentContext(),
		v8::String::NewFromUtf8(isolate, kDirNameGlobal, v8::NewStringType::kInternalized).ToLocalChecked(),
		dirName);
}

static void ClearFileAndDirectoryGlobals(Local<Object> global)
{
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	global->Set(isolate->GetCurrentContext(),
		v8::String::NewFromUtf8(isolate, kFileNameGlobal, v8::NewStringType::kInternalized).ToLocalChecked(),
		v8::Undefined(isolate));
	global->Set(isolate->GetCurrentContext(),
		v8::String::NewFromUtf8(isolate, kDirNameGlobal, v8::NewStringType::kInternalized).ToLocalChecked(),
		v8::Undefined(isolate));
}

// Load, compile and execute a JavaScript file in the current context. Used by
// the webOS unit test framework and service launcher, as well as part of the implementation
// of the webOS custom require function below.
Local<Value> IncludeScript(char const * pathToScriptSource, bool& exceptionOccurred)
{
	exceptionOccurred = true;
	v8::Isolate* isolate = v8::Isolate::GetCurrent();
	if(!pathToScriptSource || !*pathToScriptSource ) {
        return isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8(isolate, "webOS 'include' requires a non-empty filename argument.").ToLocalChecked()));
	}
	EscapableHandleScope scope(isolate);
	Local<Value> returnValue = Undefined(isolate);
	Local<String> scriptSource = createV8StringFromFile(pathToScriptSource);
	Local<Context> currentContext = isolate->GetCurrentContext();
	ScriptOrigin *scriptOrigin = new ScriptOrigin(isolate, String::NewFromUtf8(isolate, pathToScriptSource).ToLocalChecked());
	Local<Script> compiledScript(Script::Compile(currentContext, scriptSource, scriptOrigin).ToLocalChecked());
	if(compiledScript.IsEmpty()) {
		return returnValue;
	}
	//Local<Context> currentContext = isolate->GetCurrentContext();
	Local<Object> global = currentContext->Global();
	SetFileAndDirectoryGlobals(global, pathToScriptSource);
	returnValue = compiledScript->Run(currentContext).ToLocalChecked();
	ClearFileAndDirectoryGlobals(global);
	if(returnValue.IsEmpty()) {
		return returnValue;
	}
	exceptionOccurred = false;
	return scope.Escape(Local<Value>::New(isolate, returnValue));
}

// Wrapper function that checks and converts parameters on the way in and converts
// exceptions.
void IncludeScriptWrapper( const v8::FunctionCallbackInfo<v8::Value> & arguments )
{
    v8::Isolate* isolate = arguments.GetIsolate();
    if (arguments.Length() != 1) {
        arguments.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8(isolate, "Invalid number of parameters, 1 expected.").ToLocalChecked())));
        return;
    }
    try {
		Isolate* isolate = v8::Isolate::GetCurrent();
		v8::String::Utf8Value fileName(isolate, arguments[0]);
		bool exceptionOccurred;
		arguments.GetReturnValue().Set(IncludeScript(*fileName, exceptionOccurred));
    } catch( std::exception const & ex ) {
        arguments.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8(isolate, ex.what()).ToLocalChecked())));
    } catch( ... ) {
        arguments.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8(isolate, "Native function threw an unknown exception.").ToLocalChecked())));
    }
}

static void CopyProperty(const Local<Object>& src, const Local<Object>& dst, const char* propertyName)
{
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    Local<String> pName(v8::String::NewFromUtf8(isolate, propertyName, v8::NewStringType::kInternalized).ToLocalChecked());
    Local<Value> v;
    if(src->Get(isolate->GetCurrentContext(), pName).ToLocal(&v)) {
	dst->Set(isolate->GetCurrentContext(), pName, v);
    }
}

// Function that creates a new JavaScript context and loads, compiles and executes a list of source
// files in that context. Compatible with the CommonJS module specification.
// This implementation is imperfect, though, as it can't import all the apparently global symbols
// required from node. In particular, the node require() function is in fact a local variable, and not
// possible to access from this function. At this point this function is only an interesting experiment.
static Local<Value> Require(const Local<Value>& nativeRequire, const Local<Value>& loader, const Local<Array> & filePaths)
{
	v8::Isolate* isolate = v8::Isolate::GetCurrent();

	// fetch the current content and global object
	Local<Context> currentContext = isolate->GetCurrentContext();
	Local<Object> currentGlobal = currentContext->Global();
	
	// create a new context with an empty global template. This would be the place we'd
	// extend the global template with the function from node if that were possible.
	Local<ObjectTemplate> globalTemplate = ObjectTemplate::New(isolate);
	Persistent<Context> utilityContext(isolate, Context::New(isolate, NULL, globalTemplate));

	// If security tokens don't match between contexts then neither context can access each
	// other's properties. This is the mechanism that keeps JS in pages in a browser from sniffing
	// other pages data. It's not being used for any purpose in webOS's use of node.
	Local<Context> localUtilityContext = Local<Context>::New(isolate, utilityContext);
	localUtilityContext->SetSecurityToken(currentContext->GetSecurityToken());
	Context::Scope utilityScope(localUtilityContext);

	// Set up an exports object for use by modules.
	Local<ObjectTemplate> exportsTemplate = ObjectTemplate::New(isolate);
	Local<Object> exportsInstance = exportsTemplate->NewInstance(currentContext).ToLocalChecked();;
	Local<Object> global = localUtilityContext->Global();
	global->Set(currentContext,
            v8::String::NewFromUtf8(
                isolate, "exports",    v8::NewStringType::kInternalized).ToLocalChecked(),
            exportsInstance);
	global->Set(currentContext,
            v8::String::NewFromUtf8(
                isolate, "global",     v8::NewStringType::kInternalized).ToLocalChecked(),
            global);
	global->Set(currentContext,
            v8::String::NewFromUtf8(
                isolate, "globals",    v8::NewStringType::kInternalized).ToLocalChecked(),
            currentGlobal);
	global->Set(currentContext,
            v8::String::NewFromUtf8(
                isolate, "root",       v8::NewStringType::kInternalized).ToLocalChecked(),
            currentGlobal);
	global->Set(currentContext,
            v8::String::NewFromUtf8(
                isolate, "MojoLoader", v8::NewStringType::kInternalized).ToLocalChecked(),
            loader);
	global->Set(currentContext,
            v8::String::NewFromUtf8(
                isolate, "require",    v8::NewStringType::kInternalized).ToLocalChecked(),
            nativeRequire);
	
	// copy a number of useful properties from the loading node context.
	CopyProperty(currentGlobal, global, "console");
	CopyProperty(currentGlobal, global, "setTimeout");
	CopyProperty(currentGlobal, global, "clearTimeout");
	CopyProperty(currentGlobal, global, "setInterval");
	CopyProperty(currentGlobal, global, "clearInterval");
	
	// load the list of files, stopping if any produce an error
	uint32_t length = filePaths->Length();
	for(uint32_t i = 0; i < length; ++i) {
		Local<Value> fileNameObject(filePaths->Get(currentContext, i).ToLocalChecked());
		if (!fileNameObject->IsString()) {
		    return isolate->ThrowException(v8::Exception::Error(
			    v8::String::NewFromUtf8(isolate, "All elements of file paths array must be strings.").ToLocalChecked()));
		}
		Isolate* isolate = v8::Isolate::GetCurrent();
		v8::String::Utf8Value fileName(isolate, fileNameObject);
		bool exceptionOccurred;
		SetFileAndDirectoryGlobals(global, *fileName);
		IncludeScript(*fileName, exceptionOccurred);
		if (exceptionOccurred) {
			break;
		}
	}
	ClearFileAndDirectoryGlobals(global);
	return global;
}

static void RequireWrapper(const v8::FunctionCallbackInfo<v8::Value>& arguments)
{
    Isolate* isolate = Isolate::GetCurrent();
    if (arguments.Length() != 3) {
        arguments.GetReturnValue().Set(isolate->ThrowException(Exception::Error(
                                  String::NewFromUtf8(isolate, "Invalid number of parameters, 3 expected.").ToLocalChecked())));
        return;
    }
	if (!arguments[0]->IsFunction()) {
        arguments.GetReturnValue().Set(isolate->ThrowException(Exception::Error(
                              String::NewFromUtf8(isolate, "Argument 2 must be an function.").ToLocalChecked())));
        return;
	}
	if (!arguments[2]->IsArray()) {
        arguments.GetReturnValue().Set(isolate->ThrowException(Exception::Error(
                              String::NewFromUtf8(isolate, "Argument 3 must be an array.").ToLocalChecked())));
        return;
	}
	Local<Array> fileList = Local<Array>::Cast(arguments[2]);
    arguments.GetReturnValue().Set(Require(arguments[0], arguments[1], fileList));
}


void init(Local<Object> target)
{
    Isolate* isolate = Isolate::GetCurrent();
	Local<Context> currentContext = isolate->GetCurrentContext();
    HandleScope scope(isolate);
    Local<FunctionTemplate> includeTemplate = FunctionTemplate::New(isolate, IncludeScriptWrapper);
    target->Set(currentContext,
        v8::String::NewFromUtf8(isolate, "include", v8::NewStringType::kInternalized).ToLocalChecked(),
        includeTemplate->GetFunction(currentContext).ToLocalChecked());
    Local<FunctionTemplate> requireTemplate = FunctionTemplate::New(isolate, RequireWrapper);
    target->Set(currentContext,
        v8::String::NewFromUtf8(isolate, "require", v8::NewStringType::kInternalized).ToLocalChecked(),
        requireTemplate->GetFunction(currentContext).ToLocalChecked());
}

NODE_MODULE(webos, init)
