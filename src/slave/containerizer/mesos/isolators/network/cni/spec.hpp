// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef __ISOLATOR_CNI_SPEC_HPP__
#define __ISOLATOR_CNI_SPEC_HPP__

#include <stout/try.hpp>

// ONLY USEFUL AFTER RUNNING PROTOC.
#include "slave/containerizer/mesos/isolators/network/cni/spec.pb.h"

namespace mesos {
namespace internal {
namespace slave {
namespace cni {
namespace spec {

Try<NetworkConfig> parseNetworkConfig(const std::string& s);


Try<NetworkInfo> parseNetworkInfo(const std::string& s);

} // namespace spec {
} // namespace cni {
} // namespace slave {
} // namespace internal {
} // namespace mesos {

#endif // __ISOLATOR_CNI_SPEC_HPP__
