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

#include "seekdb_in_process.h"
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <unistd.h>
#include <curl/curl.h>
#include "lib/file/file_directory_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/thread/protected_stack_allocator.h"
#include "lib/worker.h"
#include "observer/ob_server.h"
#include "observer/ob_server_options.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::observer;

namespace {
__attribute__((used)) const char artifact_metadata[] =
    "SEEKDB_IN_PROCESS_ARTIFACT_BUILD_ID=" SEEKDB_IN_PROCESS_BUILD_ID
    ";SEEKDB_IN_PROCESS_ARTIFACT_HOOK_MODE=" SEEKDB_IN_PROCESS_HOOK_MODE;
std::atomic<seekdb_in_process_state> runtime_state{SEEKDB_IN_PROCESS_IDLE};
std::atomic<bool> stop_requested{false};
std::atomic<unsigned int> cleanup_status{SEEKDB_IN_PROCESS_CLEANUP_NONE};
std::atomic<int> cleanup_error{OB_SUCCESS};

/** Preserve the first cleanup failure for diagnostics. */
void record_cleanup_error(int error)
{
  int expected = OB_SUCCESS;
  if (OB_SUCCESS != error) {
    cleanup_error.compare_exchange_strong(expected, error);
  }
}

int add_parameters(ObServerOptions &options, const char *const *parameters)
{
  int ret = OB_SUCCESS;
  for (size_t i = 0; OB_SUCC(ret) && parameters != nullptr && parameters[i] != nullptr; i += 2) {
    if (parameters[i + 1] == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ret = options.parameters_.push_back(std::make_pair(ObString(parameters[i]), ObString(parameters[i + 1])));
    }
  }
  return ret;
}

/** Create engine-owned directories and pass the caller's server parameters. */
int prepare_runtime(const char *directory, ObServerOptions &options,
                    const char *const *startup_parameters, const char *const *initial_parameters)
{
  int ret = options.base_dir_.assign(directory);
  if (OB_SUCC(ret)) {
    ret = FileDirectoryUtils::create_full_path(directory);
  }
  if (OB_SUCC(ret) && chdir(directory) != 0) {
    ret = OB_IO_ERROR;
  }
  for (const char *path : {"run", "etc", "log"}) {
    if (OB_SUCC(ret)) {
      ret = FileDirectoryUtils::create_full_path(path);
    }
  }
  options.in_process_ = true;
  options.nodaemon_ = true;
  if (OB_SUCC(ret)) {
    ret = add_parameters(options, startup_parameters);
  }
  // Seed caller configuration only for an uninitialized data directory.
  bool first_init = true;
  DIR *sstable = opendir("store/sstable");
  if (sstable != nullptr) {
    while (dirent *entry = readdir(sstable)) {
      if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0) {
        first_init = false;
        break;
      }
    }
    closedir(sstable);
  }
  if (OB_SUCC(ret) && first_init) {
    ret = add_parameters(options, initial_parameters);
  }
  return ret;
}

/** Stop and destroy an in-process server while preserving its primary error. */
int cleanup_server(ObServer &server, int primary_ret, bool server_initialized)
{
  server.set_stop();
  const int cleanup_ret = server.wait();
  server.destroy();
  const bool cleanup_succeeded = OB_SUCCESS == cleanup_ret
      || (!server_initialized && OB_NOT_INIT == cleanup_ret);
  if (cleanup_succeeded) {
    cleanup_status.fetch_or(SEEKDB_IN_PROCESS_CLEANUP_SERVER);
  } else {
    record_cleanup_error(cleanup_ret);
  }
  return primary_ret == OB_SUCCESS ? cleanup_ret : primary_ret;
}

/** Start and stop the singleton without taking ownership of process signals. */
int run_runtime(ObServerOptions &options)
{
  int ret = OB_SUCCESS;
  lib::ObStackHeaderGuard stack_header_guard;
  lib::Worker worker;
  lib::Worker::set_worker_to_thread_local(&worker);
  OB_LOGGER.set_log_level(DEFAULT_LOG_LEVEL);
  OB_LOGGER.set_file_name("log/seekdb.log", true, false);
  ObPLogWriterCfg log_config;
  ObServer &server = ObServer::get_instance();
  bool server_initialized = false;
  if (OB_FAIL(server.init(options, log_config))) {
  } else {
    server_initialized = true;
    if (OB_FAIL(server.start())) {
    }
    if (OB_SUCC(ret)) {
      runtime_state.store(SEEKDB_IN_PROCESS_RUNNING);
      while (!stop_requested.load()) {
        usleep(100000);
      }
      runtime_state.store(SEEKDB_IN_PROCESS_STOPPING);
      server.prepare_stop();
    }
  }
  ret = cleanup_server(server, ret, server_initialized);
  lib::Worker::set_worker_to_thread_local(nullptr);
  return ret;
}
} // namespace

int seekdb_in_process_run(const char *absolute_directory, const char *const *startup_parameters,
                          const char *const *initial_parameters)
{
  if (absolute_directory == nullptr || absolute_directory[0] != '/') {
    return OB_INVALID_ARGUMENT;
  }
  seekdb_in_process_state expected = SEEKDB_IN_PROCESS_IDLE;
  if (!runtime_state.compare_exchange_strong(expected, SEEKDB_IN_PROCESS_STARTING)) {
    return OB_INIT_TWICE;
  }
  int ret = OB_SUCCESS;
  bool curl_initialized = false;
  cleanup_status.store(SEEKDB_IN_PROCESS_CLEANUP_NONE);
  cleanup_error.store(OB_SUCCESS);
  const int previous_directory = open(".", O_RDONLY);
  if (previous_directory < 0) {
    ret = OB_IO_ERROR;
  } else {
    ObServerOptions options;
    if (OB_FAIL(prepare_runtime(absolute_directory, options, startup_parameters, initial_parameters))) {
    } else if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      curl_initialized = true;
      ret = run_runtime(options);
    }
    if (curl_initialized) {
      curl_global_cleanup();
      cleanup_status.fetch_or(SEEKDB_IN_PROCESS_CLEANUP_CURL);
    }
    if (fchdir(previous_directory) != 0) {
      record_cleanup_error(OB_IO_ERROR);
      if (OB_SUCC(ret)) {
        ret = OB_IO_ERROR;
      }
    } else {
      cleanup_status.fetch_or(SEEKDB_IN_PROCESS_CLEANUP_WORKING_DIRECTORY);
    }
    if (close(previous_directory) != 0) {
      record_cleanup_error(OB_IO_ERROR);
      if (OB_SUCC(ret)) {
        ret = OB_IO_ERROR;
      }
    }
  }
  runtime_state.store(OB_SUCC(ret) ? SEEKDB_IN_PROCESS_STOPPED : SEEKDB_IN_PROCESS_FAILED);
  return ret;
}

void seekdb_in_process_request_stop(void)
{
  stop_requested.store(true);
}

seekdb_in_process_state seekdb_in_process_get_state(void)
{
  return runtime_state.load();
}

unsigned int seekdb_in_process_get_cleanup_status(void)
{
  return cleanup_status.load();
}

int seekdb_in_process_get_cleanup_error(void)
{
  return cleanup_error.load();
}

const char *seekdb_in_process_get_build_id(void)
{
  return SEEKDB_IN_PROCESS_BUILD_ID;
}

const char *seekdb_in_process_get_hook_mode(void)
{
  return SEEKDB_IN_PROCESS_HOOK_MODE;
}
