/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file src/relay/backend/contrib/VsiNpu/codegen_json.cc
 * \brief Implementation of VsiNpu codegen APIs.
 */

#include <tvm/relay/attrs/nn.h>
#include <tvm/relay/expr_functor.h>
#include <tvm/relay/transform.h>
#include <tvm/relay/type.h>
#include <tvm/runtime/module.h>
#include <tvm/runtime/registry.h>

#include <fstream>
#include <numeric>
#include <sstream>

#include "../../utils.h"

#include "../../../../runtime/contrib/json/json_node.h"
#include "../codegen_json/codegen_json.h"

namespace tvm {
namespace relay {
namespace contrib {

using namespace backend;

class VsiNpuJSONSerializer : public backend::contrib::JSONSerializer {
  using JSONGraphNode = tvm::runtime::json::JSONGraphNode;
  using JSONGraphNodeEntry = tvm::runtime::json::JSONGraphNodeEntry;

 public:
  /*!
   * \brief A series of operators that form a composite
   * dense layer. Supports both nn.dense and qnn.dense.
   */
  struct CompositeDenseNode {
    const CallNode* dense = nullptr;
    const CallNode* bias = nullptr;
    const CallNode* requantize = nullptr;
  };

  VsiNpuJSONSerializer(const std::string& symbol, const Expr& expr) : JSONSerializer(symbol, expr) {}

  std::vector<JSONGraphNodeEntry> VisitExpr_(const CallNode* cn) override {

#if 0
    Expr expr = GetRef<Expr>(cn);
    std::string name;
    const CallNode* call = cn;
    if (const auto* op_node = cn->op.as<OpNode>()) {
      name = op_node->name;
    } else if (const auto* fn = cn->op.as<FunctionNode>()) {
      auto comp = fn->GetAttr<String>(attr::kComposite);
      CHECK(comp.defined()) << "VsiNpu JSON runtime only supports composite functions.";
      name = comp.value();

      if (name == "vsi_npu.dense") {
        call = GetRootCall(fn->body.as<CallNode>(), 1, {"nn.dense", "nn.bias_add"});
        CHECK(call->op.as<OpNode>()) << "Not op node";
      } else {
        LOG(FATAL) << "Unrecognized VsiNpu pattern: " << name;
      }
    } else {
      LOG(FATAL) << "VsiNpu JSON runtime does not support calls to " << cn->op->GetTypeKey();
    }

    std::vector<JSONGraphNodeEntry> inputs;
    for (const auto& arg : cn->args) {
      auto res = VisitExpr(arg);
      inputs.insert(inputs.end(), res.begin(), res.end());
    }
    auto node = std::make_shared<JSONGraphNode>(name,     /* name_ */
                                                "kernel", /* op_type_ */
                                                inputs, 1 /* num_outputs_ */);
    SetCallNodeAttribute(node, call);
    return AddNode(node, GetRef<Expr>(cn));
#else
    if (cn->op.as<OpNode>()) {
      return JSONSerializer::VisitExpr_(cn);
    }
    if (!cn->op.as<FunctionNode>()) {
      LOG(FATAL) << "VSI NPU JSON runtime does not support calls to "
                 << cn->op->GetTypeKey();
    }
    auto fn = cn->op.as<FunctionNode>();
    auto comp = fn->GetAttr<String>(attr::kComposite);
    CHECK(comp.defined()) << "VSI NPU JSON runtime only supports composite functions.";
    const std::string name = comp.value();
    std::shared_ptr<JSONGraphNode> json_node;
    if (name == "vsi_npu.dense") {
      json_node = CreateCompositeDenseJSONNode(cn);
    } else {
      LOG(FATAL) << "Unrecognized VSI NPU pattern: " << name;
    }
    return AddNode(json_node, GetRef<Expr>(cn));

#endif
  }
 private:
    std::shared_ptr<JSONGraphNode> CreateCompositeDenseJSONNode(const CallNode* cn) {
    CompositeDenseNode nodes = UnpackCompositeDense(cn);
    std::string name = "nn.dense";

    // Inputs must be added in the same order they appear in the relay graph.
    std::vector<JSONGraphNodeEntry> inputs;
    inputs.push_back(VisitExpr(cn->args[0])[0]);
    inputs.push_back(VisitExpr(nodes.dense->args[1])[0]);
    if (nodes.requantize) {
      name = "qnn.dense";
      inputs.push_back(VisitExpr(nodes.dense->args[2])[0]);  // input zero-point
      inputs.push_back(VisitExpr(nodes.dense->args[3])[0]);  // weight zero-point
      inputs.push_back(VisitExpr(nodes.dense->args[4])[0]);  // input scale
      inputs.push_back(VisitExpr(nodes.dense->args[5])[0]);  // weight scale
    }
    if (nodes.bias) {
      inputs.push_back(VisitExpr(nodes.bias->args[1])[0]);
    }
    if (nodes.requantize) {
      inputs.push_back(VisitExpr(nodes.requantize->args[3])[0]);  // output scale
      inputs.push_back(VisitExpr(nodes.requantize->args[4])[0]);  // output zero-point
    }

    auto json_node = std::make_shared<JSONGraphNode>(name, "kernel", inputs, 1);
    SetCallNodeAttribute(json_node, nodes.dense);
    return json_node;
  }
  /*!
   * \brief Extract dense nodes from a composite function.
   *
   * \param cn The call node of the composite function.
   * \return Extracted composite convolution nodes.
   */
  static CompositeDenseNode UnpackCompositeDense(const CallNode* cn) {
    CompositeDenseNode nodes{};
    const auto* fn = cn->op.as<FunctionNode>();
    CHECK(fn);

    // Traverse composite dense function from child to parent
    const auto* current_call = fn->body.as<CallNode>();
    if (backend::IsOp(current_call, "qnn.requantize")) {
      nodes.requantize = current_call;
      current_call = current_call->args[0].as<CallNode>();
    }
    if (backend::IsOp(current_call, "nn.bias_add")) {
      nodes.bias = current_call;
      current_call = current_call->args[0].as<CallNode>();
    }
    // Enforce a dense node exists at this point during traversal
    if (nodes.requantize) {
      CHECK(backend::IsOp(current_call, "qnn.dense"));
    } else {
      CHECK(backend::IsOp(current_call, "nn.dense"));
    }
    nodes.dense = current_call;
    return nodes;
  }

};

/*!
 * \brief The external compiler/codegen tool. It takes a Relay expression/module and
 * compile it into a runtime module.
 */
runtime::Module VsiNpuCompiler(const ObjectRef& ref) {
  CHECK(ref->IsInstance<FunctionNode>());
  auto func = Downcast<Function>(ref);
  auto func_name = GetExtSymbol(func);
  VsiNpuJSONSerializer serializer(func_name, func);
  serializer.serialize();
  std::string graph_json = serializer.GetJSON();
  auto params = serializer.GetParams();

  const auto* pf = runtime::Registry::Get("runtime.VsiNpuJSONRuntimeCreate");
  CHECK(pf != nullptr) << "Cannot find JSON runtime module to create";
  auto mod = (*pf)(func_name, graph_json, params);
  return mod;
}

TVM_REGISTER_GLOBAL("relay.ext.vsi_npu").set_body_typed(VsiNpuCompiler);
}  // namespace contrib
}  // namespace relay
}  // namespace tvm