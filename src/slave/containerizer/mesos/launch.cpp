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

#include <errno.h>
#include <string.h>

#include <iostream>

#include <stout/foreach.hpp>
#include <stout/os.hpp>
#include <stout/protobuf.hpp>
#include <stout/unreachable.hpp>

#ifdef __linux__
#include "linux/fs.hpp"
#endif

#include "mesos/mesos.hpp"

#include "slave/containerizer/mesos/launch.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

namespace mesos {
namespace internal {
namespace slave {

const string MesosContainerizerLaunch::NAME = "launch";


MesosContainerizerLaunch::Flags::Flags()
{
  add(&command,
      "command",
      "The command to execute.");

  add(&sandbox,
      "sandbox",
      "The sandbox for the executor. If rootfs is specified this must\n"
      "be relative to the new root.");

  add(&working_directory,
      "working_directory",
      "The working directory for the executor. It will be ignored if\n"
      "container root filesystem is not specified.");

#ifndef __WINDOWS__
  add(&rootfs,
      "rootfs",
      "Absolute path to the container root filesystem.\n"
      "The command and sandbox flags are interpreted relative\n"
      "to rootfs\n"
      "Different platforms may implement 'chroot' differently.");

  add(&user,
      "user",
      "The user to change to.");
#endif // __WINDOWS__

  add(&pipe_read,
      "pipe_read",
      "The read end of the control pipe.");

  add(&pipe_write,
      "pipe_write",
      "The write end of the control pipe.");

  add(&commands,
      "commands",
      "The additional preparation commands to execute before\n"
      "executing the command.");
}


int MesosContainerizerLaunch::execute()
{
  // Check command line flags.
  if (flags.command.isNone()) {
    cerr << "Flag --command is not specified" << endl;
    return 1;
  }

  if (flags.sandbox.isNone()) {
    cerr << "Flag --sandbox is not specified" << endl;
    return 1;
  }

  if (flags.pipe_read.isNone()) {
    cerr << "Flag --pipe_read is not specified" << endl;
    return 1;
  }

  if (flags.pipe_write.isNone()) {
    cerr << "Flag --pipe_write is not specified" << endl;
    return 1;
  }

  // Parse the command.
  Try<CommandInfo> command =
    ::protobuf::parse<CommandInfo>(flags.command.get());

  if (command.isError()) {
    cerr << "Failed to parse the command: " << command.error() << endl;
    return 1;
  }

  // Validate the command.
  if (command.get().shell()) {
    if (!command.get().has_value()) {
      cerr << "Shell command is not specified" << endl;
      return 1;
    }
  } else {
    if (!command.get().has_value()) {
      cerr << "Executable path is not specified" << endl;
      return 1;
    }
  }

  int pipe[2] = { flags.pipe_read.get(), flags.pipe_write.get() };

// NOTE: On windows we need to pass `HANDLE`s between processes, as
// file descriptors are not unique across processes. Here we convert
// back from from the `HANDLE`s we receive to fds that can be used in
// os-agnostic code.
#ifdef __WINDOWS__
  pipe[0] = os::handle_to_fd(pipe[0], _O_RDONLY | _O_TEXT);
  pipe[1] = os::handle_to_fd(pipe[1], _O_TEXT);
#endif // __WINDOWS__

  Try<Nothing> close = os::close(pipe[1]);
  if (close.isError()) {
    cerr << "Failed to close pipe[1]: " << close.error() << endl;
    return 1;
  }

  // Do a blocking read on the pipe until the parent signals us to continue.
  char dummy;
  ssize_t length;
  while ((length = os::read(
              pipe[0],
              &dummy,
              sizeof(dummy))) == -1 &&
          errno == EINTR);

  if (length != sizeof(dummy)) {
     // There's a reasonable probability this will occur during agent
     // restarts across a large/busy cluster.
     cerr << "Failed to synchronize with agent (it's probably exited)" << endl;
     return 1;
  }

  close = os::close(pipe[0]);
  if (close.isError()) {
    cerr << "Failed to close pipe[0]: " << close.error() << endl;
    return 1;
  }

  // Run additional preparation commands. These are run as the same
  // user and with the environment as the agent.
  if (flags.commands.isSome()) {
    // TODO(jieyu): Use JSON::Array if we have generic parse support.
    JSON::Object object = flags.commands.get();
    if (object.values.count("commands") == 0) {
      cerr << "Invalid JSON format for flag --commands" << endl;
      return 1;
    }

    if (!object.values["commands"].is<JSON::Array>()) {
      cerr << "Invalid JSON format for flag --commands" << endl;
      return 1;
    }

    JSON::Array array = object.values["commands"].as<JSON::Array>();
    foreach (const JSON::Value& value, array.values) {
      if (!value.is<JSON::Object>()) {
        cerr << "Invalid JSON format for flag --commands" << endl;
        return 1;
      }

      Try<CommandInfo> parse = ::protobuf::parse<CommandInfo>(value);
      if (parse.isError()) {
        cerr << "Failed to parse a preparation command: "
             << parse.error() << endl;
        return 1;
      }

      // TODO(jieyu): Currently, we only accept shell commands for the
      // preparation commands.
      if (!parse.get().shell()) {
        cerr << "Preparation commands need to be shell commands" << endl;
        return 1;
      }

      if (!parse.get().has_value()) {
        cerr << "The 'value' of a preparation command is not specified" << endl;
        return 1;
      }

      // Block until the command completes.
      int status = os::system(parse.get().value());
      if (!WIFEXITED(status) || (WEXITSTATUS(status) != 0)) {
        cerr << "Failed to execute a preparation shell command" << endl;
        return 1;
      }
    }
  }

#ifndef __WINDOWS__
  // NOTE: If 'flags.user' is set, we will get the uid, gid, and the
  // supplementary group ids associated with the specified user before
  // changing the filesystem root. This is because after changing the
  // filesystem root, the current process might no longer have access
  // to /etc/passwd and /etc/group on the host.
  Option<uid_t> uid;
  Option<gid_t> gid;
  vector<gid_t> gids;

  // TODO(gilbert): For the case container user exists, support
  // framework/task/default user -> container user mapping once
  // user namespace and container capabilities is available for
  // mesos container.

  if (flags.user.isSome()) {
    Result<uid_t> _uid = os::getuid(flags.user.get());
    if (!_uid.isSome()) {
      cerr << "Failed to get the uid of user '" << flags.user.get() << "': "
           << (_uid.isError() ? _uid.error() : "not found") << endl;
      return 1;
    }

    // No need to change user/groups if the specified user is the same
    // as that of the current process.
    if (_uid.get() != os::getuid().get()) {
      Result<gid_t> _gid = os::getgid(flags.user.get());
      if (!_gid.isSome()) {
        cerr << "Failed to get the gid of user '" << flags.user.get() << "': "
             << (_gid.isError() ? _gid.error() : "not found") << endl;
        return 1;
      }

      Try<vector<gid_t>> _gids = os::getgrouplist(flags.user.get());
      if (_gids.isError()) {
        cerr << "Failed to get the supplementary gids of user '"
             << flags.user.get() << "': "
             << (_gids.isError() ? _gids.error() : "not found") << endl;
        return 1;
      }

      uid = _uid.get();
      gid = _gid.get();
      gids = _gids.get();
    }
  }
#endif // __WINDOWS__

#ifdef __WINDOWS__
  // Not supported on Windows.
  const Option<std::string> rootfs = None();
#else
  const Option<std::string> rootfs = flags.rootfs;
#endif // __WINDOWS__

  // Change root to a new root, if provided.
  if (rootfs.isSome()) {
    cout << "Changing root to " << rootfs.get() << endl;

    // Verify that rootfs is an absolute path.
    Result<string> realpath = os::realpath(rootfs.get());
    if (realpath.isError()) {
      cerr << "Failed to determine if rootfs is an absolute path: "
           << realpath.error() << endl;
      return 1;
    } else if (realpath.isNone()) {
      cerr << "Rootfs path does not exist" << endl;
      return 1;
    } else if (realpath.get() != rootfs.get()) {
      cerr << "Rootfs path is not an absolute path" << endl;
      return 1;
    }

#ifdef __linux__
    Try<Nothing> chroot = fs::chroot::enter(rootfs.get());
#elif defined(__WINDOWS__)
    Try<Nothing> chroot = Error("`chroot` not supported on Windows");
#else // For any other platform we'll just use POSIX chroot.
    Try<Nothing> chroot = os::chroot(rootfs.get());
#endif // __linux__
    if (chroot.isError()) {
      cerr << "Failed to enter chroot '" << rootfs.get()
           << "': " << chroot.error();
      return 1;
    }
  }

  // Change user if provided. Note that we do that after executing the
  // preparation commands so that those commands will be run with the
  // same privilege as the mesos-agent.
#ifndef __WINDOWS__
  if (uid.isSome()) {
    Try<Nothing> setgid = os::setgid(gid.get());
    if (setgid.isError()) {
      cerr << "Failed to set gid to " << gid.get()
           << ": " << setgid.error() << endl;
      return 1;
    }

    Try<Nothing> setgroups = os::setgroups(gids, uid);
    if (setgroups.isError()) {
      cerr << "Failed to set supplementary gids: "
           << setgroups.error() << endl;
      return 1;
    }

    Try<Nothing> setuid = os::setuid(uid.get());
    if (setuid.isError()) {
      cerr << "Failed to set uid to " << uid.get()
           << ": " << setuid.error() << endl;
      return 1;
    }
  }
#endif // __WINDOWS__

  // Determine the current working directory for the executor.
  string cwd;
  if (rootfs.isSome() && flags.working_directory.isSome()) {
    cwd = flags.working_directory.get();
  } else {
    cwd = flags.sandbox.get();
  }

  Try<Nothing> chdir = os::chdir(cwd);
  if (chdir.isError()) {
    cerr << "Failed to chdir into current working directory '"
         << cwd << "': " << chdir.error() << endl;
    return 1;
  }

  // Relay the environment variables.
  // TODO(jieyu): Consider using a clean environment.

  if (command.get().shell()) {
    // Execute the command using shell.
    os::execlp(os::Shell::name, os::Shell::arg0,
               os::Shell::arg1, command.get().value().c_str(), (char*) nullptr);
  } else {
    // Use execvp to launch the command.
    char** argv = new char*[command.get().arguments().size() + 1];
    for (int i = 0; i < command.get().arguments().size(); i++) {
      argv[i] = strdup(command.get().arguments(i).c_str());
    }
    argv[command.get().arguments().size()] = nullptr;

    execvp(command.get().value().c_str(), argv);
  }

  // If we get here, the execle call failed.
  cerr << "Failed to execute command: " << os::strerror(errno) << endl;
  UNREACHABLE();
}

} // namespace slave {
} // namespace internal {
} // namespace mesos {
