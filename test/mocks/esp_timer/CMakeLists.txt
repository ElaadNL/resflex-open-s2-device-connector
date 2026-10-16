# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0

# Lightweight mock component for esp_timer used by unit tests
idf_component_register(
    SRCS "Mockesp_timer.c"
    INCLUDE_DIRS "."
)
