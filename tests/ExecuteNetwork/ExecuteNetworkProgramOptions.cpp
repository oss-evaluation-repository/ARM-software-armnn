//
// Copyright © 2020 Arm Ltd and Contributors. All rights reserved.
// SPDX-License-Identifier: MIT
//

#include "ExecuteNetworkProgramOptions.hpp"
#include "NetworkExecutionUtils/NetworkExecutionUtils.hpp"
#include "InferenceTest.hpp"

#include <armnn/BackendRegistry.hpp>
#include <armnn/Exceptions.hpp>
#include <armnn/utility/Assert.hpp>
#include <armnn/utility/StringUtils.hpp>
#include <armnn/Logging.hpp>

#include <fmt/format.h>

bool CheckOption(const cxxopts::ParseResult& result,
                 const char* option)
{
    // Check that the given option is valid.
    if (option == nullptr)
    {
        return false;
    }

    // Check whether 'option' is provided.
    return ((result.count(option)) ? true : false);
}

void CheckOptionDependency(const cxxopts::ParseResult& result,
                           const char* option,
                           const char* required)
{
    // Check that the given options are valid.
    if (option == nullptr || required == nullptr)
    {
        throw cxxopts::OptionParseException("Invalid option to check dependency for");
    }

    // Check that if 'option' is provided, 'required' is also provided.
    if (CheckOption(result, option) && !result[option].has_default())
    {
        if (CheckOption(result, required) == 0 || result[required].has_default())
        {
            throw cxxopts::OptionParseException(
                    std::string("Option '") + option + "' requires option '" + required + "'.");
        }
    }
}

void CheckOptionDependencies(const cxxopts::ParseResult& result)
{
    CheckOptionDependency(result, "model-path", "model-format");
    CheckOptionDependency(result, "input-tensor-shape", "model-path");
    CheckOptionDependency(result, "tuning-level", "tuning-path");
}

void RemoveDuplicateDevices(std::vector<armnn::BackendId>& computeDevices)
{
    // Mark the duplicate devices as 'Undefined'.
    for (auto i = computeDevices.begin(); i != computeDevices.end(); ++i)
    {
        for (auto j = std::next(i); j != computeDevices.end(); ++j)
        {
            if (*j == *i)
            {
                *j = armnn::Compute::Undefined;
            }
        }
    }

    // Remove 'Undefined' devices.
    computeDevices.erase(std::remove(computeDevices.begin(), computeDevices.end(), armnn::Compute::Undefined),
                         computeDevices.end());
}

/// Takes a vector of backend strings and returns a vector of backendIDs. Removes duplicate entries.
std::vector<armnn::BackendId> GetBackendIDs(const std::vector<std::string>& backendStrings)
{
    std::vector<armnn::BackendId> backendIDs;
    for (const auto& b : backendStrings)
    {
        backendIDs.push_back(armnn::BackendId(b));
    }

    RemoveDuplicateDevices(backendIDs);

    return backendIDs;
}

/// Provides a segfault safe way to get cxxopts option values by checking if the option was defined.
/// If the option wasn't defined it returns an empty object.
template<typename optionType>
optionType GetOptionValue(std::string&& optionName, const cxxopts::ParseResult& result)
{
    optionType out;
    if(result.count(optionName))
    {
        out = result[optionName].as<optionType>();
    }
    return out;
}

void LogAndThrowFatal(std::string errorMessage)
{
    throw armnn::InvalidArgumentException (errorMessage);
}

void CheckRequiredOptions(const cxxopts::ParseResult& result)
{

    // For each option in option-group "a) Required
    std::vector<std::string> requiredOptions{"compute",
                                             "model-format",
                                             "model-path",
                                             "input-name",
                                             "output-name"};

    bool requiredMissing = false;
    for(auto const&  str : requiredOptions)
    {
        if(!(result.count(str) > 0))
        {
            ARMNN_LOG(error) << fmt::format("The program option '{}' is mandatory but wasn't provided.", str);
            requiredMissing = true;
        }
    }
    if(requiredMissing)
    {
        throw armnn::InvalidArgumentException ("Some required arguments are missing");
    }
}

void ProgramOptions::ValidateExecuteNetworkParams()
{
    m_ExNetParams.ValidateParams();
}

void ProgramOptions::ValidateRuntimeOptions()
{
    if (m_RuntimeOptions.m_ProfilingOptions.m_TimelineEnabled &&
        !m_RuntimeOptions.m_ProfilingOptions.m_EnableProfiling)
    {
        LogAndThrowFatal("Timeline profiling requires external profiling to be turned on");
    }
}


ProgramOptions::ProgramOptions() : m_CxxOptions{"ExecuteNetwork",
                                                "Executes a neural network model using the provided input "
                                                "tensor. Prints the resulting output tensor."}
{
    try
    {
        // cxxopts doesn't provide a mechanism to ensure required options are given. There is a
        // separate function CheckRequiredOptions() for that.
        m_CxxOptions.add_options("a) Required")
                ("c,compute",
                 "Which device to run layers on by default. Possible choices: "
                 + armnn::BackendRegistryInstance().GetBackendIdsAsString()
                 + " NOTE: Compute devices need to be passed as a comma separated list without whitespaces "
                   "e.g. CpuRef,CpuAcc",
                 cxxopts::value<std::vector<std::string>>())

                ("f,model-format",
                 "armnn-binary, caffe-binary, caffe-text, onnx-binary, onnx-text, tflite-binary, tensorflow-binary or "
                 "tensorflow-text.",
                 cxxopts::value<std::string>())

                ("m,model-path",
                 "Path to model file, e.g. .armnn, .caffemodel, .prototxt, .tflite, .onnx",
                 cxxopts::value<std::string>(m_ExNetParams.m_ModelPath))

                ("i,input-name",
                 "Identifier of the input tensors in the network separated by comma.",
                 cxxopts::value<std::string>())

                ("o,output-name",
                 "Identifier of the output tensors in the network separated by comma.",
                 cxxopts::value<std::string>());

        m_CxxOptions.add_options("b) General")
                ("b,dynamic-backends-path",
                 "Path where to load any available dynamic backend from. "
                 "If left empty (the default), dynamic backends will not be used.",
                 cxxopts::value<std::string>(m_RuntimeOptions.m_DynamicBackendsPath))

                ("d,input-tensor-data",
                 "Path to files containing the input data as a flat array separated by whitespace. "
                 "Several paths can be passed by separating them with a comma. If not specified, the network will be "
                 "run with dummy data (useful for profiling).",
                 cxxopts::value<std::string>()->default_value(""))

                ("h,help", "Display usage information")

                ("infer-output-shape",
                 "Infers output tensor shape from input tensor shape and validate where applicable (where supported by "
                 "parser)",
                 cxxopts::value<bool>(m_ExNetParams.m_InferOutputShape)->default_value("false")->implicit_value("true"))

                ("iterations",
                 "Number of iterations to run the network for, default is set to 1",
                 cxxopts::value<size_t>(m_ExNetParams.m_Iterations)->default_value("1"))

                ("l,dequantize-output",
                 "If this option is enabled, all quantized outputs will be dequantized to float. "
                 "If unset, default to not get dequantized. "
                 "Accepted values (true or false)",
                 cxxopts::value<bool>(m_ExNetParams.m_DequantizeOutput)->default_value("false")->implicit_value("true"))

                ("p,print-intermediate-layers",
                 "If this option is enabled, the output of every graph layer will be printed.",
                 cxxopts::value<bool>(m_ExNetParams.m_PrintIntermediate)->default_value("false")
                 ->implicit_value("true"))

                ("parse-unsupported",
                 "Add unsupported operators as stand-in layers (where supported by parser)",
                 cxxopts::value<bool>(m_ExNetParams.m_ParseUnsupported)->default_value("false")->implicit_value("true"))

                ("q,quantize-input",
                 "If this option is enabled, all float inputs will be quantized to qasymm8. "
                 "If unset, default to not quantized. Accepted values (true or false)",
                 cxxopts::value<bool>(m_ExNetParams.m_QuantizeInput)->default_value("false")->implicit_value("true"))

                ("r,threshold-time",
                 "Threshold time is the maximum allowed time for inference measured in milliseconds. If the actual "
                 "inference time is greater than the threshold time, the test will fail. By default, no threshold "
                 "time is used.",
                 cxxopts::value<double>(m_ExNetParams.m_ThresholdTime)->default_value("0.0"))

                ("s,input-tensor-shape",
                 "The shape of the input tensors in the network as a flat array of integers separated by comma."
                 "Several shapes can be passed by separating them with a colon (:).",
                 cxxopts::value<std::string>())

                ("v,visualize-optimized-model",
                 "Enables built optimized model visualizer. If unset, defaults to off.",
                 cxxopts::value<bool>(m_ExNetParams.m_EnableLayerDetails)->default_value("false")
                 ->implicit_value("true"))

                ("w,write-outputs-to-file",
                 "Comma-separated list of output file paths keyed with the binding-id of the output slot. "
                 "If left empty (the default), the output tensors will not be written to a file.",
                 cxxopts::value<std::string>())

                ("x,subgraph-number",
                 "Id of the subgraph to be executed. Defaults to 0.",
                 cxxopts::value<size_t>(m_ExNetParams.m_SubgraphId)->default_value("0"))

                ("y,input-type",
                 "The type of the input tensors in the network separated by comma. "
                 "If unset, defaults to \"float\" for all defined inputs. "
                 "Accepted values (float, int or qasymm8).",
                 cxxopts::value<std::string>())

                ("z,output-type",
                 "The type of the output tensors in the network separated by comma. "
                 "If unset, defaults to \"float\" for all defined outputs. "
                 "Accepted values (float, int or qasymm8).",
                 cxxopts::value<std::string>());

        m_CxxOptions.add_options("c) Optimization")
                ("bf16-turbo-mode",
                 "If this option is enabled, FP32 layers, "
                 "weights and biases will be converted to BFloat16 where the backend supports it",
                 cxxopts::value<bool>(m_ExNetParams.m_EnableBf16TurboMode)
                 ->default_value("false")->implicit_value("true"))

                ("enable-fast-math",
                 "Enables fast_math options in backends that support it. Using the fast_math flag can lead to "
                 "performance improvements but may result in reduced or different precision.",
                 cxxopts::value<bool>(m_ExNetParams.m_EnableFastMath)->default_value("false")->implicit_value("true"))

                ("fp16-turbo-mode",
                 "If this option is enabled, FP32 layers, "
                 "weights and biases will be converted to FP16 where the backend supports it",
                 cxxopts::value<bool>(m_ExNetParams.m_EnableFp16TurboMode)
                 ->default_value("false")->implicit_value("true"))

                ("tuning-level",
                 "Sets the tuning level which enables a tuning run which will update/create a tuning file. "
                 "Available options are: 1 (Rapid), 2 (Normal), 3 (Exhaustive). "
                 "Requires tuning-path to be set, default is set to 0 (No tuning run)",
                 cxxopts::value<int>(m_ExNetParams.m_TuningLevel)->default_value("0"))

                ("tuning-path",
                 "Path to tuning file. Enables use of CL tuning",
                 cxxopts::value<std::string>(m_ExNetParams.m_TuningPath));

        m_CxxOptions.add_options("d) Profiling")
                ("a,enable-external-profiling",
                 "If enabled external profiling will be switched on",
                 cxxopts::value<bool>(m_RuntimeOptions.m_ProfilingOptions.m_EnableProfiling)
                         ->default_value("false")->implicit_value("true"))

                ("e,event-based-profiling",
                 "Enables built in profiler. If unset, defaults to off.",
                 cxxopts::value<bool>(m_ExNetParams.m_EnableProfiling)->default_value("false")->implicit_value("true"))

                ("g,file-only-external-profiling",
                 "If enabled then the 'file-only' test mode of external profiling will be enabled",
                 cxxopts::value<bool>(m_RuntimeOptions.m_ProfilingOptions.m_FileOnly)
                 ->default_value("false")->implicit_value("true"))

                ("file-format",
                 "If profiling is enabled specifies the output file format",
                 cxxopts::value<std::string>(m_RuntimeOptions.m_ProfilingOptions.m_FileFormat)->default_value("binary"))

                ("j,outgoing-capture-file",
                 "If specified the outgoing external profiling packets will be captured in this binary file",
                 cxxopts::value<std::string>(m_RuntimeOptions.m_ProfilingOptions.m_OutgoingCaptureFile))

                ("k,incoming-capture-file",
                 "If specified the incoming external profiling packets will be captured in this binary file",
                 cxxopts::value<std::string>(m_RuntimeOptions.m_ProfilingOptions.m_IncomingCaptureFile))

                ("timeline-profiling",
                 "If enabled timeline profiling will be switched on, requires external profiling",
                 cxxopts::value<bool>(m_RuntimeOptions.m_ProfilingOptions.m_TimelineEnabled)
                 ->default_value("false")->implicit_value("true"))

                ("u,counter-capture-period",
                 "If profiling is enabled in 'file-only' mode this is the capture period that will be used in the test",
                 cxxopts::value<uint32_t>(m_RuntimeOptions.m_ProfilingOptions.m_CapturePeriod)->default_value("150"));
    }
    catch (const std::exception& e)
    {
        ARMNN_ASSERT_MSG(false, "Caught unexpected exception");
        ARMNN_LOG(fatal) << "Fatal internal error: " << e.what();
        exit(EXIT_FAILURE);
    }
}

ProgramOptions::ProgramOptions(int ac, const char* av[]): ProgramOptions()
{
    ParseOptions(ac, av);
}

void ProgramOptions::ParseOptions(int ac, const char* av[])
{
    // Parses the command-line.
    m_CxxResult = m_CxxOptions.parse(ac, av);

    if (m_CxxResult.count("help") || ac <= 1)
    {
        std::cout << m_CxxOptions.help() << std::endl;
        exit(EXIT_SUCCESS);
    }

    CheckRequiredOptions(m_CxxResult);
    CheckOptionDependencies(m_CxxResult);

    // Some options can't be assigned directly because they need some post-processing:
    auto computeDevices = GetOptionValue<std::vector<std::string>>("compute", m_CxxResult);
    m_ExNetParams.m_ComputeDevices = GetBackendIDs(computeDevices);
    m_ExNetParams.m_ModelFormat =
            armnn::stringUtils::StringTrimCopy(GetOptionValue<std::string>("model-format", m_CxxResult));
    m_ExNetParams.m_InputNames =
            ParseStringList(GetOptionValue<std::string>("input-name", m_CxxResult), ",");
    m_ExNetParams.m_InputTensorDataFilePaths =
            ParseStringList(GetOptionValue<std::string>("input-tensor-data", m_CxxResult), ",");
    m_ExNetParams.m_OutputNames =
            ParseStringList(GetOptionValue<std::string>("output-name", m_CxxResult), ",");
    m_ExNetParams.m_InputTypes =
            ParseStringList(GetOptionValue<std::string>("input-type", m_CxxResult), ",");
    m_ExNetParams.m_OutputTypes =
            ParseStringList(GetOptionValue<std::string>("output-type", m_CxxResult), ",");
    m_ExNetParams.m_OutputTensorFiles =
            ParseStringList(GetOptionValue<std::string>("write-outputs-to-file", m_CxxResult), ",");
    m_ExNetParams.m_GenerateTensorData =
            m_ExNetParams.m_InputTensorDataFilePaths.empty();
    m_ExNetParams.m_DynamicBackendsPath = m_RuntimeOptions.m_DynamicBackendsPath;

    // Parse input tensor shape from the string we got from the command-line.
    std::vector<std::string> inputTensorShapesVector =
            ParseStringList(GetOptionValue<std::string>("input-tensor-shape", m_CxxResult), ":");

    if (!inputTensorShapesVector.empty())
    {
        m_ExNetParams.m_InputTensorShapes.reserve(inputTensorShapesVector.size());

        for(const std::string& shape : inputTensorShapesVector)
        {
            std::stringstream ss(shape);
            std::vector<unsigned int> dims = ParseArray(ss);

            m_ExNetParams.m_InputTensorShapes.push_back(
                    std::make_unique<armnn::TensorShape>(static_cast<unsigned int>(dims.size()), dims.data()));
        }
    }

    // We have to validate ExecuteNetworkParams first so that the tuning path and level is validated
    ValidateExecuteNetworkParams();

    // Parse CL tuning parameters to runtime options
    if (!m_ExNetParams.m_TuningPath.empty())
    {
        m_RuntimeOptions.m_BackendOptions.emplace_back(
            armnn::BackendOptions
            {
                "GpuAcc",
                {
                    {"TuningLevel", m_ExNetParams.m_TuningLevel},
                    {"TuningFile", m_ExNetParams.m_TuningPath.c_str()},
                    {"KernelProfilingEnabled", m_ExNetParams.m_EnableProfiling}
                }
            }
        );
    }

    ValidateRuntimeOptions();
}

