/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "seekdb_platform.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
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

namespace seekdb_platform {
const char *const *startup_parameters()
{
  static const char *const parameters[] = {
      "mysql_port_mode", "disabled", "sql_net_thread_count", "2", "cpu_count", "2", nullptr};
  return parameters;
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

const char *invalid_parameters(const char **parameters)
{
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
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

/** Shorten long socket names inside the App home; simulator may use host /tmp. */
std::vector<std::string> socket_alias_patterns()
{
  std::vector<std::string> patterns;
  const char *home = std::getenv("HOME");
  if (home != nullptr && home[0] == '/') {
    std::string writable_home = home;
    // /var and /private/var name the same container; the short spelling leaves room for its writable tmp directory.
    if (writable_home.compare(0, 13, "/private/var/") == 0) {
      writable_home.erase(0, 8);
    }
    patterns.push_back(writable_home + "/tmp/.XXXXXX");
  }
#if TARGET_OS_SIMULATOR
  patterns.push_back("/tmp/seekdb-XXXXXX");
#endif
  return patterns;
}

const char *stopped_message()
{
  return "the engine already stopped in this process; restart the App to open a database again";
}
}
