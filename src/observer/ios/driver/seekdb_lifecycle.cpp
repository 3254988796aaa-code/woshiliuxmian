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
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
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
  char *db_dir;
  char *sock_path;
  char *socket_alias_dir;
};

/** Own one engine thread and the arguments retained for its entire lifetime. */
struct Engine {
  std::mutex mutex;
  pthread_t thread{};
  bool started = false;
  size_t handles = 0;
  std::string directory;
  std::vector<std::string> parameters;
  std::vector<const char *> initial;
  std::atomic<int> result{0};
};
Engine engine;
thread_local std::string last_open_error;
const char *const startup_parameters[] = {
    "mysql_port_mode", "disabled", "sql_net_thread_count", "2", "cpu_count", "2", nullptr};

int open_failed(int code, const char *message)
{
  last_open_error = message;
  return code;
}

/** Resolve existing parents so fresh and existing paths use the same singleton identity. */
std::string canonical_directory(const char *directory)
{
  std::string parent = directory;
  std::vector<std::string> suffix;
  char resolved[PATH_MAX];
  while (realpath(parent.c_str(), resolved) == nullptr) {
    size_t slash = parent.find_last_of('/');
    if (slash == std::string::npos || parent == "/") {
      return {};
    }
    suffix.push_back(parent.substr(slash + 1));
    parent = slash == 0 ? "/" : parent.substr(0, slash);
  }
  std::string result = resolved;
  for (auto part = suffix.rbegin(); part != suffix.rend(); ++part) {
    if (part->empty() || *part == ".") {
      continue;
    }
    if (*part == "..") {
      size_t slash = result.find_last_of('/');
      result = slash == 0 ? "/" : result.substr(0, slash);
    } else {
      result += (result == "/" ? "" : "/") + *part;
    }
  }
  return result;
}

/** Run the in-process engine; the caller joins before releasing its arguments. */
void *run_engine(void *)
{
  engine.result.store(seekdb_in_process_run(engine.directory.c_str(), startup_parameters, engine.initial.data()));
  return nullptr;
}

void prepare_initial_parameters(const char **parameters)
{
  const char *defaults[][2] = {
      {"memory_budget", "1G"}, {"vector_memory_limit", "128M"},
      {"log_disk_size", "2G"}, {"datafile_maxsize", "20G"}};
  for (const auto &parameter : defaults) {
    const char *value = parameter[1];
    for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
      if (std::strcmp(parameters[i], parameter[0]) == 0) {
        value = parameters[i + 1];
      }
    }
    engine.parameters.emplace_back(parameter[0]);
    engine.parameters.emplace_back(value);
  }
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
    bool reserved = false;
    for (const char *key : {"port", "mysql_port_mode", "cpu_count", "sql_net_thread_count",
                            "memory_budget", "vector_memory_limit", "log_disk_size", "datafile_maxsize"}) {
      reserved = reserved || std::strcmp(parameters[i], key) == 0;
    }
    if (!reserved) {
      engine.parameters.emplace_back(parameters[i]);
      engine.parameters.emplace_back(parameters[i + 1]);
    }
  }
  for (const auto &parameter : engine.parameters) {
    engine.initial.push_back(parameter.c_str());
  }
  engine.initial.push_back(nullptr);
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
    std::free(handle->db_dir);
    std::free(handle);
  }
}

/** Shorten long socket names inside the App home; simulator may use host /tmp. */
bool prepare_socket(SeekdbHandleImpl *handle)
{
  std::string socket = engine.directory + "/run/sql.sock";
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
    if (symlink((engine.directory + "/run").c_str(), link.c_str()) != 0) {
      return false;
    }
    socket = link + "/sql.sock";
  }
  handle->sock_path = strdup(socket.c_str());
  return handle->sock_path != nullptr;
}

/** Confirm that the running engine's Unix socket answers with a MySQL protocol greeting. */
bool socket_ready(const SeekdbHandleImpl *handle)
{
  int client = socket(AF_UNIX, SOCK_STREAM, 0);
  if (client < 0) {
    return false;
  }
  timeval timeout{1, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, handle->sock_path, sizeof(address.sun_path) - 1);
  unsigned char greeting[5] = {};
  size_t received = 0;
  bool ready = connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
  while (ready && received < sizeof(greeting)) {
    ssize_t count = recv(client, greeting + received, sizeof(greeting) - received, 0);
    ready = count > 0;
    if (ready) {
      received += static_cast<size_t>(count);
    }
  }
  int error = errno;
  ready = ready && greeting[3] == 0 && greeting[4] == 10;
  close(client);
  static unsigned int failed_attempts = 0;
  if (!ready && ++failed_attempts % 50 == 1) {
    std::fprintf(stderr, "SeekDB socket readiness: %zu bytes, %s\n", received, std::strerror(error));
    FILE *log = std::fopen((engine.directory + "/log/ios-driver.log").c_str(), "a");
    if (log != nullptr) {
      std::fprintf(log, "socket readiness attempt %u: %zu bytes, %s\n", failed_attempts, received, std::strerror(error));
      std::fclose(log);
    }
  }
  return ready;
}

/** Stop and join the consumed engine thread before returning to the caller. */
int stop_engine()
{
  seekdb_in_process_request_stop();
  if (pthread_join(engine.thread, nullptr) != 0) {
    return SEEKDB_INTERNAL_ERROR;
  }
  return engine.result.load() == 0 ? SEEKDB_SUCCESS : SEEKDB_INTERNAL_ERROR;
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
  std::lock_guard<std::mutex> guard(engine.mutex);
  std::string canonical = canonical_directory(directory);
  if (canonical.empty()) {
    return open_failed(SEEKDB_INVALID_ARGUMENT, "cannot resolve db_dir");
  }
  if (engine.started && engine.handles == 0) {
    return open_failed(SEEKDB_INTERNAL_ERROR,
                       "the engine already stopped in this process; restart the App to open a database again");
  }
  if (engine.started && engine.directory != canonical) {
    return open_failed(SEEKDB_INTERNAL_ERROR, "another db_dir is already open in this process");
  }
  if (engine.started && seekdb_in_process_get_state() != SEEKDB_IN_PROCESS_RUNNING) {
    return open_failed(SEEKDB_INTERNAL_ERROR, "the engine is no longer running");
  }
  auto *handle = static_cast<SeekdbHandleImpl *>(std::calloc(1, sizeof(SeekdbHandleImpl)));
  if (handle == nullptr) {
    return open_failed(SEEKDB_INTERNAL_ERROR, "out of memory");
  }
  if (!engine.started) {
    engine.directory = canonical;
  }
  handle->db_dir = strdup(canonical.c_str());
  if (handle->db_dir == nullptr || !prepare_socket(handle)) {
    destroy_handle(handle);
    return open_failed(SEEKDB_INTERNAL_ERROR, "cannot prepare the Unix socket path");
  }
  if (!engine.started) {
    prepare_initial_parameters(parameters);
    pthread_attr_t attributes;
    int thread_error = pthread_attr_init(&attributes);
    if (thread_error == 0) {
      thread_error = pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
      if (thread_error == 0) {
        thread_error = pthread_create(&engine.thread, &attributes, run_engine, nullptr);
      }
      pthread_attr_destroy(&attributes);
    }
    if (thread_error != 0) {
      engine.parameters.clear();
      engine.initial.clear();
      destroy_handle(handle);
      return open_failed(SEEKDB_INTERNAL_ERROR, "cannot start the engine thread");
    }
    engine.started = true;
    bool ready = false;
    bool stopped = false;
    for (unsigned int attempt = 0; attempt < 1500; ++attempt) {
      auto state = seekdb_in_process_get_state();
      if (state == SEEKDB_IN_PROCESS_FAILED || state == SEEKDB_IN_PROCESS_STOPPED) {
        stopped = true;
        break;
      }
      if (state == SEEKDB_IN_PROCESS_RUNNING && socket_ready(handle)) {
        ready = true;
        break;
      }
      usleep(200000);
    }
    if (!ready) {
      stop_engine();
      destroy_handle(handle);
      return open_failed(SEEKDB_INTERNAL_ERROR, stopped
                         ? "the engine stopped during startup; see the log directory under db_dir"
                         : "the engine did not accept SQL connections within 300 seconds; see the log directory under db_dir");
    }
  }
  ++engine.handles;
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
  std::lock_guard<std::mutex> guard(engine.mutex);
  if (engine.handles == 0) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  --engine.handles;
  int result = engine.handles == 0 ? stop_engine() : SEEKDB_SUCCESS;
  destroy_handle(static_cast<SeekdbHandleImpl *>(opaque));
  return result;
}
}
