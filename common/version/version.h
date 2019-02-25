#pragma once
/*
 * DRace, a dynamic data race detector
 *
 * Copyright (c) Siemens AG, 2018
 *
 * Authors:
 *   Felix Moessbauer <felix.moessbauer@siemens.com>
 *
 * This work is licensed under the terms of the MIT license.  See
 * the LICENSE file in the top-level directory.
 */

/**
* This template is processed by cmake and adds the current
* git tag and commit hash to the binary.
*/

const char * DRACE_BUILD_VERSION = "1.0.x (git)";
const char * DRACE_BUILD_HASH = "c6f3464d88db47f01f4975719cfa3196979736e4";
