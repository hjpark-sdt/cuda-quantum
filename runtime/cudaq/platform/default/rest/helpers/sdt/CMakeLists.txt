# ============================================================================ #
# Copyright (c) 2022 - 2025 NVIDIA Corporation & Affiliates.                   #
# All rights reserved.                                                         #
#                                                                              #
# This source code and the accompanying materials are made available under     #
# the terms of the Apache License 2.0 which accompanies this distribution.     #
# ============================================================================ #
target_sources(cudaq-rest-qpu PRIVATE SdtServerHelper.cpp)
add_target_config(sdt)
add_target_mapping_arch(sdt berkeley-25q.txt)
add_target_mapping_arch(sdt telegraph-8q.txt)

add_library(cudaq-serverhelper-sdt SHARED SdtServerHelper.cpp )
target_link_libraries(cudaq-serverhelper-sdt
  PUBLIC 
    cudaq-common 
    fmt::fmt-header-only 
)
install(TARGETS cudaq-serverhelper-sdt DESTINATION lib)

