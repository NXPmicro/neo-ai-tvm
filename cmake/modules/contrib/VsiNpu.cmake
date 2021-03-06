# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

if((USE_VSI_NPU STREQUAL "ON") OR (USE_VSI_NPU STREQUAL "JSON"))
	file(GLOB VSI_NPU_RELAY_CONTRIB_SRC src/relay/backend/contrib/vsi_npu/codegen_json.cc)
	list(APPEND COMPILER_SRCS ${VSI_NPU_RELAY_CONTRIB_SRC})
	message(STATUS "Build with VSI NPU support ...")

	if(USE_VSI_NPU_RUNTIME)
		list(APPEND TVM_RUNTIME_LINKER_LIBS tim-vx)
		add_definitions(-DUSE_VSI_NPU_RUNTIME=1)
	endif(USE_VSI_NPU_RUNTIME)
	file(GLOB VSI_NPU_CONTRIB_SRC src/runtime/contrib/vsi_npu/vsi_npu_json.cc)
	list(APPEND RUNTIME_SRCS ${VSI_NPU_CONTRIB_SRC})
	message(STATUS "Build with VSI NPU json runtime: " ${EXTERN_LIBRARY_DNNL})
endif((USE_VSI_NPU STREQUAL "ON") OR (USE_VSI_NPU STREQUAL "JSON"))
