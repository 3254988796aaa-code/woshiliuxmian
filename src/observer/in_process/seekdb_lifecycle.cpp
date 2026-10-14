// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "seekdb.h"
#include "seekdb_in_process.h"
#include "seekdb_platform.h"
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {
struct SeekdbHandleImpl {
  char *sock_path;
  char *socket_alias_dir;
};

std::mutex handles_mutex;
size_t handles = 0;
thread_local std::string last_open_error;

int open_failed(int code, const char *message)
{
  last_open_error = message;
  return code;
}

/** Validate desktop key/value syntax and the iOS socket-only restriction. */
const char *invalid_parameters(const char **parameters)
{
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
    if (parameters[i][0] == '\0' || parameters[i + 1] == nullptr) {
      return "parameters must be key/value pairs with non-empty keys";
    }
  }
  return seekdb_platform::invalid_parameters(parameters);
}

/** Release a sandbox socket alias and the opaque handle's owned strings. */
void destroy_handle(SeekdbHandleImpl *handle)
{
  if (handle != nullptr) {
    if (handle->socket_alias_dir != nullptr) {
      std::string link = std::string(handle->socket_alias_dir) + "/r";
      unlink(link.c_str());
      rmdir(handle->socket_alias_dir);
    }
    std::free(handle->socket_alias_dir);
    std::free(handle->sock_path);
    std::free(handle);
  }
}

bool prepare_socket(SeekdbHandleImpl *handle, const char *engine_socket)
{
  if (engine_socket == nullptr) {
    return false;
  }
  std::string socket = engine_socket;
  if (socket.size() >= sizeof(sockaddr_un::sun_path)) {
    std::string link;
    for (const std::string &pattern : seekdb_platform::socket_alias_patterns()) {
      if (pattern.size() + sizeof("/r/sql.sock") > sizeof(sockaddr_un::sun_path)) {
        continue;
      }
      std::vector<char> buffer(pattern.begin(), pattern.end());
      buffer.push_back('\0');
      if (mkdtemp(buffer.data()) != nullptr) {
        handle->socket_alias_dir = strdup(buffer.data());
        if (handle->socket_alias_dir == nullptr) {
          rmdir(buffer.data());
          return false;
        }
        link = std::string(buffer.data()) + "/r";
        break;
      }
    }
    if (link.empty() || symlink(socket.substr(0, socket.rfind('/')).c_str(), link.c_str()) != 0) {
      return false;
    }
    socket = link + "/sql.sock";
  }
  handle->sock_path = strdup(socket.c_str());
  return handle->sock_path != nullptr;
}
} // namespace

extern "C" {
/** Open the singleton on a background thread, blocking until its socket accepts SQL clients. */
int seekdb_open(const char *directory, const char **parameters, SeekdbHandle *out_handle)
{
  last_open_error.clear();
  if (out_handle == nullptr) {
    return open_failed(SEEKDB_INVALID_ARGUMENT, "out_handle must not be NULL");
  }
  *out_handle = nullptr;
  if (directory == nullptr || directory[0] != '/') {
    return open_failed(SEEKDB_INVALID_ARGUMENT, "db_dir must be an absolute path");
  }
  if (const char *message = invalid_parameters(parameters)) {
    return open_failed(SEEKDB_INVALID_ARGUMENT, message);
  }
  std::lock_guard<std::mutex> guard(handles_mutex);
  auto state = seekdb_in_process_get_state();
  if (state == SEEKDB_IN_PROCESS_STOPPED || state == SEEKDB_IN_PROCESS_FAILED) {
    return open_failed(SEEKDB_INTERNAL_ERROR, seekdb_platform::stopped_message());
  }
  auto *handle = static_cast<SeekdbHandleImpl *>(std::calloc(1, sizeof(SeekdbHandleImpl)));
  if (handle == nullptr) {
    return open_failed(SEEKDB_INTERNAL_ERROR, "out of memory");
  }
  std::vector<std::string> initial = seekdb_platform::initial_parameters(parameters);
  std::vector<const char *> initial_pointers;
  for (const auto &parameter : initial) {
    initial_pointers.push_back(parameter.c_str());
  }
  initial_pointers.push_back(nullptr);
  if (seekdb_in_process_start(directory, seekdb_platform::startup_parameters(), initial_pointers.data()) != 0) {
    destroy_handle(handle);
    return open_failed(SEEKDB_INTERNAL_ERROR, seekdb_in_process_last_error());
  }
  if (!prepare_socket(handle, seekdb_in_process_socket_path())) {
    seekdb_in_process_stop();
    destroy_handle(handle);
    return open_failed(SEEKDB_INTERNAL_ERROR, "cannot prepare the Unix socket path");
  }
  ++handles;
  *out_handle = handle;
  return SEEKDB_SUCCESS;
}

int seekdb_connection_options(SeekdbHandle opaque, SeekdbConnectionOptions *out_options)
{
  if (opaque == nullptr || out_options == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  const auto *handle = static_cast<const SeekdbHandleImpl *>(opaque);
  *out_options = {};
  if (handle->sock_path == nullptr) {
    return SEEKDB_INTERNAL_ERROR;
  }
  out_options->transport = SEEKDB_CONNECTION_TRANSPORT_UNIX_SOCKET;
  out_options->unix_socket = handle->sock_path;
  out_options->user = "root";
  return SEEKDB_SUCCESS;
}

const char *seekdb_last_open_error(void)
{
  return last_open_error.c_str();
}

/** Release this handle; the last handle stops and joins the one-shot engine. */
int seekdb_close(SeekdbHandle opaque)
{
  if (opaque == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> guard(handles_mutex);
  if (handles == 0) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  --handles;
  int result = seekdb_in_process_stop() == 0 ? SEEKDB_SUCCESS : SEEKDB_INTERNAL_ERROR;
  destroy_handle(static_cast<SeekdbHandleImpl *>(opaque));
  return result;
}
}
