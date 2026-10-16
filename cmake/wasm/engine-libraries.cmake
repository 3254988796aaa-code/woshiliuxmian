# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
# Every library the complete engine links, in link order. The dependency
# modules included after this file append their imported libraries, and
# database links the list as it stands.
set(engine_libraries
  engine_options
  observer sql storage share
  logservice rootserver pl
  query data_plane parser
  oblib_common oblib_lib rpc
  compression restore memory
  crypto startup_sources)
