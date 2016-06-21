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

#include <vector>

#include <gmock/gmock.h>

#include <mesos/resources.hpp>
#include <mesos/scheduler.hpp>

#include <mesos/master/detector.hpp>

#include <process/future.hpp>
#include <process/gtest.hpp>
#include <process/owned.hpp>

#include <stout/jsonify.hpp>

#include "master/master.hpp"

#include "slave/slave.hpp"

#include "slave/containerizer/containerizer.hpp"
#include "slave/containerizer/fetcher.hpp"

#include "slave/containerizer/mesos/isolators/gpu/nvml.hpp"

#include "tests/mesos.hpp"

using mesos::internal::master::Master;

using mesos::internal::slave::Containerizer;
using mesos::internal::slave::Fetcher;
using mesos::internal::slave::MesosContainerizer;
using mesos::internal::slave::MesosContainerizerProcess;
using mesos::internal::slave::Slave;

using mesos::master::detector::MasterDetector;

using process::Future;
using process::Owned;

using std::vector;

using testing::_;
using testing::Eq;
using testing::Return;

namespace mesos {
namespace internal {
namespace tests {

class NvidiaGpuTest : public MesosTest {};


// This test verifies that we are able to enable the Nvidia GPU
// isolator and launch tasks with restricted access to GPUs. We
// first launch a task with access to 0 GPUs and verify that a
// call to `nvidia-smi` fails. We then launch a task with 1 GPU
// and verify that a call to `nvidia-smi` both succeeds and
// reports exactly 1 GPU available.
TEST_F(NvidiaGpuTest, ROOT_CGROUPS_NVIDIA_GPU_VerifyDeviceAccess)
{
  Try<Owned<cluster::Master>> master = StartMaster();
  ASSERT_SOME(master);

  // Turn on Nvidia GPU isolation.
  // Assume at least one GPU is available for isolation.
  slave::Flags flags = CreateSlaveFlags();
  flags.isolation = "cgroups/devices,gpu/nvidia";
  flags.nvidia_gpu_devices = vector<unsigned int>({0u});
  flags.resources = "gpus:1";

  Owned<MasterDetector> detector = master.get()->createDetector();

  Try<Owned<cluster::Slave>> slave = StartSlave(detector.get(), flags);
  ASSERT_SOME(slave);

  MockScheduler sched;

  FrameworkInfo frameworkInfo = DEFAULT_FRAMEWORK_INFO;
  frameworkInfo.add_capabilities()->set_type(
      FrameworkInfo::Capability::GPU_RESOURCES);

  MesosSchedulerDriver driver(
      &sched, frameworkInfo, master.get()->pid, DEFAULT_CREDENTIAL);

  Future<Nothing> schedRegistered;
  EXPECT_CALL(sched, registered(_, _, _))
    .WillOnce(FutureSatisfy(&schedRegistered));

  Future<vector<Offer>> offers1, offers2;
  EXPECT_CALL(sched, resourceOffers(_, _))
    .WillOnce(FutureArg<1>(&offers1))
    .WillOnce(FutureArg<1>(&offers2))
    .WillRepeatedly(Return());      // Ignore subsequent offers.

  driver.start();

  AWAIT_READY(schedRegistered);

  // Launch a task requesting no GPUs and
  // verify that running `nvidia-smi` fails.
  AWAIT_READY(offers1);
  EXPECT_EQ(1u, offers1->size());

  TaskInfo task1 = createTask(
      offers1.get()[0].slave_id(),
      Resources::parse("cpus:0.1;mem:128;").get(),
      "nvidia-smi");

  Future<TaskStatus> statusRunning1, statusFailed1;
  EXPECT_CALL(sched, statusUpdate(_, _))
    .WillOnce(FutureArg<1>(&statusRunning1))
    .WillOnce(FutureArg<1>(&statusFailed1));

  driver.launchTasks(offers1.get()[0].id(), {task1});

  AWAIT_READY(statusRunning1);
  ASSERT_EQ(TASK_RUNNING, statusRunning1->state());

  AWAIT_READY(statusFailed1);
  ASSERT_EQ(TASK_FAILED, statusFailed1->state());

  // Launch a task requesting 1 GPU and verify
  // that `nvidia-smi` lists exactly one GPU.
  AWAIT_READY(offers2);
  EXPECT_EQ(1u, offers2->size());

  TaskInfo task2 = createTask(
      offers1.get()[0].slave_id(),
      Resources::parse("cpus:0.1;mem:128;gpus:1").get(),
      "NUM_GPUS=`nvidia-smi --list-gpus | wc -l`;\n"
      "if [ \"$NUM_GPUS\" != \"1\" ]; then\n"
      "  exit 1;\n"
      "fi");

  Future<TaskStatus> statusRunning2, statusFinished2;
  EXPECT_CALL(sched, statusUpdate(_, _))
    .WillOnce(FutureArg<1>(&statusRunning2))
    .WillOnce(FutureArg<1>(&statusFinished2));

  driver.launchTasks(offers2.get()[0].id(), {task2});

  AWAIT_READY(statusRunning2);
  ASSERT_EQ(TASK_RUNNING, statusRunning2->state());

  AWAIT_READY(statusFinished2);
  ASSERT_EQ(TASK_FINISHED, statusFinished2->state());

  driver.stop();
  driver.join();
}


// This test verifies correct failure semantics when
// a task requests a fractional number of GPUs.
TEST_F(NvidiaGpuTest, ROOT_CGROUPS_NVIDIA_GPU_FractionalResources)
{
  Try<Owned<cluster::Master>> master = StartMaster();
  ASSERT_SOME(master);

  // Turn on Nvidia GPU isolation.
  // Assume at least one GPU is available for isolation.
  slave::Flags flags = CreateSlaveFlags();
  flags.isolation = "cgroups/devices,gpu/nvidia";
  flags.nvidia_gpu_devices = vector<unsigned int>({0u});
  flags.resources = "gpus:1";

  Owned<MasterDetector> detector = master.get()->createDetector();

  Try<Owned<cluster::Slave>> slave = StartSlave(detector.get(), flags);
  ASSERT_SOME(slave);

  MockScheduler sched;

  FrameworkInfo frameworkInfo = DEFAULT_FRAMEWORK_INFO;
  frameworkInfo.add_capabilities()->set_type(
      FrameworkInfo::Capability::GPU_RESOURCES);

  MesosSchedulerDriver driver(
      &sched, frameworkInfo, master.get()->pid, DEFAULT_CREDENTIAL);

  Future<Nothing> schedRegistered;
  EXPECT_CALL(sched, registered(_, _, _))
    .WillOnce(FutureSatisfy(&schedRegistered));

  Future<vector<Offer>> offers;
  EXPECT_CALL(sched, resourceOffers(_, _))
    .WillOnce(FutureArg<1>(&offers))
    .WillRepeatedly(Return());      // Ignore subsequent offers.

  driver.start();

  AWAIT_READY(schedRegistered);

  // Launch a task requesting a fractional number
  // of GPUs and verify that it fails as expected.
  AWAIT_READY(offers);
  EXPECT_EQ(1u, offers->size());

  TaskInfo task = createTask(
      offers.get()[0].slave_id(),
      Resources::parse("cpus:0.1;mem:128;gpus:0.1").get(),
      "true");

  Future<TaskStatus> status;
  EXPECT_CALL(sched, statusUpdate(_, _))
    .WillOnce(FutureArg<1>(&status));

  driver.launchTasks(offers.get()[0].id(), {task});

  AWAIT_READY(status);

  EXPECT_EQ(TASK_ERROR, status->state());
  EXPECT_EQ(TaskStatus::REASON_TASK_INVALID, status->reason());
  EXPECT_TRUE(strings::contains(
      status->message(),
      "The 'gpus' resource must be an unsigned integer"));

  driver.stop();
  driver.join();
}


// Ensures that GPUs can be auto-discovered.
TEST_F(NvidiaGpuTest, ROOT_CGROUPS_NVIDIA_GPU_Discovery)
{
  ASSERT_TRUE(nvml::isAvailable());
  ASSERT_SOME(nvml::initialize());

  Try<unsigned int> gpus = nvml::deviceGetCount();
  ASSERT_SOME(gpus);

  slave::Flags flags = CreateSlaveFlags();
  flags.resources = "cpus:1"; // To override the default with gpus:0.
  flags.isolation = "gpu/nvidia";

  Try<Resources> resources = Containerizer::resources(flags);

  ASSERT_SOME(resources);
  ASSERT_SOME(resources->gpus());
  ASSERT_EQ(gpus.get(), resources->gpus().get());
}


// Ensures that the --resources and --nvidia_gpu_devices
// flags are correctly validated.
TEST_F(NvidiaGpuTest, ROOT_CGROUPS_NVIDIA_GPU_FlagValidation)
{
  ASSERT_TRUE(nvml::isAvailable());
  ASSERT_SOME(nvml::initialize());

  Try<unsigned int> gpus = nvml::deviceGetCount();
  ASSERT_SOME(gpus);

  // Not setting the `gpu/nvidia` isolation flag
  // should not trigger-autodiscovery!
  slave::Flags flags = CreateSlaveFlags();

  Try<Resources> resources = Containerizer::resources(flags);

  ASSERT_SOME(resources);
  ASSERT_NONE(resources->gpus());

  // Setting `--nvidia_gpu_devices` without the `gpu/nvidia`
  // isolation flag should trigger an error.
  flags = CreateSlaveFlags();
  flags.nvidia_gpu_devices = vector<unsigned int>({0u});
  flags.resources = "gpus:1";

  resources = Containerizer::resources(flags);

  ASSERT_ERROR(resources);

  // Setting GPUs without the `gpu/nvidia` isolation
  // flag should just pass them through without an error.
  flags = CreateSlaveFlags();
  flags.resources = "gpus:100";

  resources = Containerizer::resources(flags);

  ASSERT_SOME(resources);
  ASSERT_SOME(resources->gpus());
  ASSERT_EQ(100u, resources->gpus().get());

  // Setting the `gpu/nvidia` isolation
  // flag should trigger autodiscovery.
  flags = CreateSlaveFlags();
  flags.resources = "cpus:1"; // To override the default with gpus:0.
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_SOME(resources);
  ASSERT_SOME(resources->gpus());
  ASSERT_EQ(gpus.get(), resources->gpus().get());

  // Setting the GPUs to 0 should not trigger auto-discovery!
  flags = CreateSlaveFlags();
  flags.resources = "gpus:0";
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_SOME(resources);
  ASSERT_NONE(resources->gpus());

  // --nvidia_gpu_devices and --resources agree on the number of GPUs.
  flags = CreateSlaveFlags();
  flags.nvidia_gpu_devices = vector<unsigned int>({0u});
  flags.resources = "gpus:1";
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_SOME(resources);
  ASSERT_SOME(resources->gpus());
  ASSERT_EQ(1u, resources->gpus().get());

  // Both --resources and --nvidia_gpu_devices must be specified!
  flags = CreateSlaveFlags();
  flags.nvidia_gpu_devices = vector<unsigned int>({0u});
  flags.resources = "cpus:1"; // To override the default with gpus:0.
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_ERROR(resources);

  flags = CreateSlaveFlags();
  flags.resources = "gpus:" + stringify(gpus.get());
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_ERROR(resources);

  // --nvidia_gpu_devices and --resources do not match!
  flags = CreateSlaveFlags();
  flags.nvidia_gpu_devices = vector<unsigned int>({0u});
  flags.resources = "gpus:2";
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_ERROR(resources);

  flags = CreateSlaveFlags();
  flags.nvidia_gpu_devices = vector<unsigned int>({0u});
  flags.resources = "gpus:0";
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_ERROR(resources);

  // More than available on the machine!
  flags = CreateSlaveFlags();
  flags.nvidia_gpu_devices = vector<unsigned int>();
  flags.resources = "gpus:" + stringify(2 * gpus.get());
  flags.isolation = "gpu/nvidia";

  for (size_t i = 0; i < 2 * gpus.get(); ++i) {
    flags.nvidia_gpu_devices->push_back(i);
  }

  resources = Containerizer::resources(flags);

  ASSERT_ERROR(resources);

  // Set `nvidia_gpu_devices` to contain duplicates.
  flags = CreateSlaveFlags();
  flags.nvidia_gpu_devices = vector<unsigned int>({0u, 0u});
  flags.resources = "cpus:1;gpus:1";
  flags.isolation = "gpu/nvidia";

  resources = Containerizer::resources(flags);

  ASSERT_ERROR(resources);
}

} // namespace tests {
} // namespace internal {
} // namespace mesos {
