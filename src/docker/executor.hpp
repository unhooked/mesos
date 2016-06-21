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

#ifndef __DOCKER_EXECUTOR_HPP__
#define __DOCKER_EXECUTOR_HPP__

#include <stdio.h>

#include <map>
#include <string>

#include <process/process.hpp>

#include <stout/option.hpp>

#include "logging/flags.hpp"

namespace mesos {
namespace internal {
namespace docker {

struct Flags : public mesos::internal::logging::Flags
{
  Flags() {
    add(&container,
        "container",
        "The name of the docker container to run.\n");

    add(&docker,
        "docker",
        "The path to the docker executable.\n");

    add(&docker_socket,
        "docker_socket",
        "The UNIX socket path to be used by docker CLI for accessing docker\n"
        "daemon.\n");

    add(&sandbox_directory,
        "sandbox_directory",
        "The path to the container sandbox holding stdout and stderr files\n"
        "into which docker container logs will be redirected.");

    add(&mapped_directory,
        "mapped_directory",
        "The sandbox directory path that is mapped in the docker container.\n");

    // TODO(alexr): Remove this after the deprecation cycle (started in 1.0).
    add(&stop_timeout,
        "stop_timeout",
        "The duration for docker to wait after stopping a running container\n"
        "before it kills that container. This flag is deprecated; use task's\n"
        "kill policy instead.");

    add(&launcher_dir,
        "launcher_dir",
        "Directory path of Mesos binaries. Mesos would find health-check,\n"
        "fetcher, containerizer and executor binary files under this\n"
        "directory.");

    add(&task_environment,
        "task_environment",
        "A JSON map of environment variables and values that should\n"
        "be passed into the task launched by this executor.");
  }

  Option<std::string> container;
  Option<std::string> docker;
  Option<std::string> docker_socket;
  Option<std::string> sandbox_directory;
  Option<std::string> mapped_directory;
  Option<std::string> launcher_dir;
  Option<std::string> task_environment;

  // TODO(alexr): Remove this after the deprecation cycle (started in 1.0).
  Option<Duration> stop_timeout;
};

} // namespace docker {
} // namespace internal {
} // namespace mesos {

#endif // __DOCKER_EXECUTOR_HPP__
