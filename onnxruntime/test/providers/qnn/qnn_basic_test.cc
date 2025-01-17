// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <string>

#include "core/session/onnxruntime_cxx_api.h"
#include "core/session/onnxruntime_session_options_config_keys.h"
#include "core/session/inference_session.h"

#include "test/providers/qnn/qnn_test_utils.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

using namespace ONNX_NAMESPACE;
using namespace onnxruntime::logging;

#define ORT_MODEL_FOLDER ORT_TSTR("testdata/")

// in test_main.cc
extern std::unique_ptr<Ort::Env> ort_env;

namespace onnxruntime {
namespace test {

// test uses ONNX model so can't be run in a minimal build.
// TODO: When we need QNN in a minimal build we should add an ORT format version of the model
#if !defined(ORT_MINIMAL_BUILD)

// Tests that the QNN EP is registered when added via the public C++ API.
// Loads a simple ONNX model that adds floats.
TEST(QnnEP, TestAddEpUsingPublicApi) {
  {
    // C++ API test
    Ort::SessionOptions so;
    onnxruntime::ProviderOptions options;

#if defined(_WIN32)
    options["backend_path"] = "QnnCpu.dll";
#else
    options["backend_path"] = "libQnnCpu.so";
#endif

    so.AppendExecutionProvider("QNN", options);

    const ORTCHAR_T* ort_model_path = ORT_MODEL_FOLDER "constant_floats.onnx";
    Ort::Session session(*ort_env, ort_model_path, so);

    // Access the underlying InferenceSession.
    const OrtSession* ort_session = session;
    const InferenceSession* s = reinterpret_cast<const InferenceSession*>(ort_session);

    bool have_qnn_ep = false;

    for (const auto& provider : s->GetRegisteredProviderTypes()) {
      if (provider == kQnnExecutionProvider) {
        have_qnn_ep = true;
        break;
      }
    }

    ASSERT_TRUE(have_qnn_ep) << "QNN EP was not found in registered providers for session.";
  }
}

// Helper function that runs an ONNX model with a NHWC Resize operator to test that
// type/shape inference succeeds during layout transformation.
// Refer to onnxruntime/core/graph/contrib_ops/nhwc_inference_context.h.
//
// The models passed to this function are subgraphs extracted from a larger model that exhibited
// shape inferencing issues on QNN. Thus, the models are expected to have a specific input/output
// types and shapes.
static void RunNHWCResizeModel(const ORTCHAR_T* ort_model_path, bool use_htp) {
  Ort::SessionOptions so;

  // Ensure all type/shape inference warnings result in errors!
  so.AddConfigEntry(kOrtSessionOptionsConfigStrictShapeTypeInference, "1");
  so.SetGraphOptimizationLevel(ORT_ENABLE_ALL);

  onnxruntime::ProviderOptions options;

#if defined(_WIN32)
  options["backend_path"] = use_htp ? "QnnHtp.dll" : "QnnCpu.dll";
#else
  options["backend_path"] = use_htp ? "libQnnHtp.so" : "libQnnCpu.so";
#endif

  so.AppendExecutionProvider("QNN", options);

  Ort::Session session(*ort_env, ort_model_path, so);

  // Input can be all zeros since we're testing for correct shape inference.
  std::array<float, 1 * 3 * 4 * 5> input0_data = {};
  std::array<float, 1 * 3 * 4 * 5> input1_data = {};
  std::array<float, 1 * 3 * 4 * 5> input2_data = {};

  auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
  std::vector<Ort::Value> ort_inputs;
  std::vector<const char*> ort_input_names;

  // Add input0
  std::array<int64_t, 4> inputs_shape{1, 3, 4, 5};
  ort_inputs.emplace_back(Ort::Value::CreateTensor<float>(
      memory_info, input0_data.data(), input0_data.size(), inputs_shape.data(), inputs_shape.size()));
  ort_input_names.push_back("input0");

  // Add input1
  ort_inputs.emplace_back(Ort::Value::CreateTensor<float>(
      memory_info, input1_data.data(), input1_data.size(), inputs_shape.data(), inputs_shape.size()));
  ort_input_names.push_back("input1");

  // Add input2
  ort_inputs.emplace_back(Ort::Value::CreateTensor<float>(
      memory_info, input2_data.data(), input2_data.size(), inputs_shape.data(), inputs_shape.size()));
  ort_input_names.push_back("input2");

  // Run session and get outputs
  std::array<const char*, 2> output_names{"output0", "output1"};
  std::vector<Ort::Value> ort_outputs = session.Run(Ort::RunOptions{nullptr}, ort_input_names.data(), ort_inputs.data(),
                                                    ort_inputs.size(), output_names.data(), output_names.size());

  // Check output shape.
  Ort::Value& ort_output = ort_outputs[1];
  auto typeshape = ort_output.GetTensorTypeAndShapeInfo();
  std::vector<int64_t> output_shape = typeshape.GetShape();

  ASSERT_THAT(output_shape, ::testing::ElementsAre(1, 6, 7, 10));
}

// Test shape inference of NHWC Resize operator (opset 11) that uses
// the scales input. Use the QNN CPU backend.
TEST(QnnCPUBackendTests, TestNHWCResizeShapeInference_scales_opset11) {
  RunNHWCResizeModel(ORT_MODEL_FOLDER "nhwc_resize_scales_opset11.onnx", false);
}

// Test shape inference of NHWC Resize operator (opset 18) that uses
// the scales input. Use the QNN CPU backend.
TEST(QnnCPUBackendTests, TestNHWCResizeShapeInference_scales_opset18) {
  RunNHWCResizeModel(ORT_MODEL_FOLDER "nhwc_resize_scales_opset18.onnx", false);
}

// Test shape inference of NHWC Resize operator (opset 11) that uses
// the sizes input. Use the QNN CPU backend.
TEST(QnnCPUBackendTests, TestNHWCResizeShapeInference_sizes_opset11) {
  RunNHWCResizeModel(ORT_MODEL_FOLDER "nhwc_resize_sizes_opset11.onnx", false);
}

// Test shape inference of NHWC Resize operator (opset 18) that uses
// the sizes input. Use the QNN CPU backend.
TEST(QnnCPUBackendTests, TestNHWCResizeShapeInference_sizes_opset18) {
  RunNHWCResizeModel(ORT_MODEL_FOLDER "nhwc_resize_sizes_opset18.onnx", false);
}

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__linux__)

// Test shape inference of QDQ NHWC Resize operator (opset 18) that uses
// the sizes input. Use the QNN HTP backend.
TEST_F(QnnHTPBackendTests, TestNHWCResizeShapeInference_qdq_sizes_opset18) {
  RunNHWCResizeModel(ORT_MODEL_FOLDER "nhwc_resize_sizes_opset18.quant.onnx", true);
}
#endif  // defined(__aarch64__) || defined(_M_ARM64) || defined(__linux__)

#endif  // !defined(ORT_MINIMAL_BUILD)

}  // namespace test
}  // namespace onnxruntime
