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

name = "Nutmeg";
exports.name = "Nutmeg";
console.log("another script, __filename = " + __filename);
console.log("another script, __dirname = " + __dirname);

var sys=require('sys');
sys.puts("require works from module");

setTimeout(function() {
	console.log("Later man");
}, 2000);
