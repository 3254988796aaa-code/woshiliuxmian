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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Engine states for the single-run-per-process in-process host. */
enum seekdb_in_process_state {
  SEEKDB_IN_PROCESS_IDLE = 0,
  SEEKDB_IN_PROCESS_STARTING = 1,
  SEEKDB_IN_PROCESS_RUNNING = 2,
  SEEKDB_IN_PROCESS_FAILED = 3
};

int seekdb_in_process_start(const char *absolute_directory, const char *const *startup_parameters,
                            const char *const *initial_parameters);

const char *seekdb_in_process_get_socket_path(void);

const char *seekdb_in_process_get_start_error(void);

/** Return the current lifecycle state without blocking. */
enum seekdb_in_process_state seekdb_in_process_get_state(void);

/** Return the source revision compiled into the runtime. */
const char *seekdb_in_process_get_build_id(void);

#ifdef __cplusplus
}
#endif
