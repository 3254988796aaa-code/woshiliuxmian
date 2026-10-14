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
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <TargetConditionals.h>
#include <jemalloc/jemalloc.h>

/** Complete allocator bootstrap on the loading thread before engine workers can initialize it. */
__attribute__((constructor)) static void initialize_framework_allocator()
{
  // Refuse a partially initialized allocator before a short-lived loader thread can retain bootstrap TSD.
  void *allocation = je_malloc(1);
  if (allocation == nullptr) {
    std::abort();
  }
  je_free(allocation);
}

namespace {
struct SeekdbHandleImpl {
  char *sock_path;
  char *socket_alias_dir;
};

std::mutex handles_mutex;
size_t handles = 0;
thread_local std::string last_open_error;
const char *const startup_parameters[] = {
    "mysql_port_mode", "disabled", "sql_net_thread_count", "2", "cpu_count", "2", nullptr};

int open_failed(int code, const char *message)
{
  last_open_error = message;
  return code;
}

std::vector<std::string> initial_parameters(const char **parameters)
{
  const char *defaults[][2] = {
      {"memory_budget", "1G"}, {"vector_memory_limit", "128M"},
      {"log_disk_size", "2G"}, {"datafile_maxsize", "20G"}};
  std::vector<std::string> result;
  for (const auto &parameter : defaults) {
    const char *value = parameter[1];
    for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
      if (std::strcmp(parameters[i], parameter[0]) == 0) {
        value = parameters[i + 1];
      }
    }
    result.emplace_back(parameter[0]);
    result.emplace_back(value);
  }
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
    bool reserved = false;
    for (const char *key : {"port", "mysql_port_mode", "cpu_count", "sql_net_thread_count",
                            "memory_budget", "vector_memory_limit", "log_disk_size", "datafile_maxsize"}) {
      reserved = reserved || std::strcmp(parameters[i], key) == 0;
    }
    if (!reserved) {
      result.emplace_back(parameters[i]);
      result.emplace_back(parameters[i + 1]);
    }
  }
  return result;
}

/** Validate desktop key/value syntax and the iOS socket-only restriction. */
const char *invalid_parameters(const char **parameters)
{
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
    if (parameters[i][0] == '\0' || parameters[i + 1] == nullptr) {
      return "parameters must be key/value pairs with non-empty keys";
    }
    if (std::strcmp(parameters[i], "port") == 0) {
      char *end = nullptr;
      errno = 0;
      long port = std::strtol(parameters[i + 1], &end, 10);
      if (errno != 0 || parameters[i + 1][0] == '\0' || *end != '\0' || port != 0) {
        return "TCP is not available on iOS; port must be 0";
      }
    }
    if (std::strcmp(parameters[i], "mysql_port_mode") == 0
        && std::strcmp(parameters[i + 1], "disabled") != 0) {
      return "TCP is not available on iOS; mysql_port_mode must be disabled";
    }
  }
  return nullptr;
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

/** Shorten long socket names inside the App home; simulator may use host /tmp. */
bool prepare_socket(SeekdbHandleImpl *handle, const char *engine_socket)
{
  if (engine_socket == nullptr) {
    return false;
  }
  std::string socket = engine_socket;
  if (socket.size() >= sizeof(sockaddr_un::sun_path)) {
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] != '/') {
      return false;
    }
    std::string writable_home = home;
    // /var and /private/var name the same container; the short spelling leaves room for its writable tmp directory.
    if (writable_home.compare(0, 13, "/private/var/") == 0) {
      writable_home.erase(0, 8);
    }
    std::string pattern = writable_home + "/tmp/.XXXXXX";
#if TARGET_OS_SIMULATOR
    if (pattern.size() + sizeof("/r/sql.sock") > sizeof(sockaddr_un::sun_path)) {
      pattern = "/tmp/seekdb-XXXXXX";
    }
#endif
    if (pattern.size() + sizeof("/r/sql.sock") > sizeof(sockaddr_un::sun_path)) {
      return false;
    }
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (mkdtemp(buffer.data()) == nullptr) {
      return false;
    }
    handle->socket_alias_dir = strdup(buffer.data());
    if (handle->socket_alias_dir == nullptr) {
      rmdir(buffer.data());
      return false;
    }
    std::string link = std::string(buffer.data()) + "/r";
    if (symlink(socket.substr(0, socket.rfind('/')).c_str(), link.c_str()) != 0) {
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
    return open_failed(SEEKDB_INTERNAL_ERROR,
                       "the engine already stopped in this process; restart the App to open a database again");
  }
  auto *handle = static_cast<SeekdbHandleImpl *>(std::calloc(1, sizeof(SeekdbHandleImpl)));
  if (handle == nullptr) {
    return open_failed(SEEKDB_INTERNAL_ERROR, "out of memory");
  }
  std::vector<std::string> initial = initial_parameters(parameters);
  std::vector<const char *> initial_pointers;
  for (const auto &parameter : initial) {
    initial_pointers.push_back(parameter.c_str());
  }
  initial_pointers.push_back(nullptr);
  if (seekdb_in_process_start(directory, startup_parameters, initial_pointers.data()) != 0) {
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
