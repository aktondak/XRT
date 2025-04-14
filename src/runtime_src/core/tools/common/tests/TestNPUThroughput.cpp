// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2024-2025 Advanced Micro Devices, Inc. All rights reserved.

// ------ I N C L U D E   F I L E S -------------------------------------------
// Local - Include Files
#include "TestNPUThroughput.h"
#include "TestValidateUtilities.h"
#include "tools/common/XBUtilities.h"
#include "xrt/xrt_bo.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_hw_context.h"
#include "xrt/xrt_kernel.h"
#include "xrt/experimental/xrt_kernel.h"
namespace XBU = XBUtilities;

#include <filesystem>

static constexpr size_t buffer_size = 20;
static constexpr int run_buffer = 9;
static constexpr int itr_count_throughput = 2502;

// ----- C L A S S   M E T H O D S -------------------------------------------
TestNPUThroughput::TestNPUThroughput()
  : TestRunner("throughput", "Run end-to-end throughput test")
{}

boost::property_tree::ptree
TestNPUThroughput::run(std::shared_ptr<xrt_core::device> dev)
{
  boost::property_tree::ptree ptree = get_test_header();
  ptree.erase("xclbin");

  // Check Whether Use ELF or DPU Sequence
  auto elf = XBValidateUtils::get_elf();
  std::string xclbin_path;
  
  if (!elf) {
    xclbin_path = XBValidateUtils::get_xclbin_path(dev, xrt_core::query::xclbin_name::type::validate, ptree);
    if (XBU::getVerbose())
      XBValidateUtils::logger(ptree, "Details", "Using DPU Sequence");
  } else {
    xclbin_path = XBValidateUtils::get_xclbin_path(dev, xrt_core::query::xclbin_name::type::validate_elf, ptree);
    if (XBU::getVerbose())
      XBValidateUtils::logger(ptree, "Details", "Using ELF");
  }

  if (!std::filesystem::exists(xclbin_path)){
    XBValidateUtils::logger(ptree, "Details", "The test is not supported on this device.");
    return ptree;
  }

  xrt::xclbin xclbin;
  try {
    xclbin = xrt::xclbin(xclbin_path);
  }
  catch (const std::runtime_error& ex) {
    XBValidateUtils::logger(ptree, "Error", ex.what());
    ptree.put("status", XBValidateUtils::test_token_failed);
    return ptree;
  }

  // Determine The DPU Kernel Name
  auto kernelName = XBValidateUtils::get_kernel_name(xclbin, ptree);

  auto working_dev = xrt::device(dev);
  working_dev.register_xclbin(xclbin);

  xrt::hw_context hwctx;
  xrt::kernel kernel;

  if (!elf) { // DPU
    try {
      hwctx = xrt::hw_context(working_dev, xclbin.get_uuid());
      kernel = xrt::kernel(hwctx, kernelName);
    } 
    catch (const std::exception& )
    {
      XBValidateUtils::logger (ptree, "Error", "Not enough columns available. Please make sure no other workload is running on the device.");
      ptree.put("status", XBValidateUtils::test_token_failed);
      return ptree;
    }
  }
  else { // ELF
    const auto elf_name = xrt_core::device_query<xrt_core::query::elf_name>(dev, xrt_core::query::elf_name::type::nop);
    auto elf_path = XBValidateUtils::findPlatformFile(elf_name, ptree);

    if (!std::filesystem::exists(elf_path))
      return ptree;
    
    try {
      hwctx = xrt::hw_context(working_dev, xclbin.get_uuid());
      kernel = get_kernel(hwctx, kernelName, elf_path);
    } 
    catch (const std::exception& )
    {
      XBValidateUtils::logger (ptree, "Error", "Not enough columns available. Please make sure no other workload is running on the device.");
      ptree.put("status", XBValidateUtils::test_token_failed);ptree.put("status", XBValidateUtils::test_token_failed);
      return ptree;
    }
  }

  xrt::xclbin::ip cu;
  for (const auto& ip : xclbin.get_ips()) {
    if (ip.get_type() != xrt::xclbin::ip::ip_type::ps)
      continue;
    cu = ip;
    break;
  }

  // create specified number of runs and populate with arguments
  std::vector<xrt::bo> global_args;
  std::vector<xrt::run> run_handles;
  if (!elf) {
    for (int i = 0; i < run_buffer; ++i) {
      auto run = xrt::run(kernel);
      for (const auto& arg : cu.get_args()) {
        auto arg_idx = static_cast<int>(arg.get_index());
        if (arg.get_host_type() == "uint64_t")
          run.set_arg(arg_idx, static_cast<uint64_t>(XBValidateUtils::get_opcode()));
        else if (arg.get_host_type() == "uint32_t")
          run.set_arg(arg_idx, static_cast<uint32_t>(1));
        else if (arg.get_host_type().find('*') != std::string::npos) {
          xrt::bo bo;

          if (arg.get_name() == "instruct") {
            bo = xrt::bo(hwctx, arg.get_size(), xrt::bo::flags::cacheable, kernel.group_id(arg_idx));
            auto bo_mapped = bo.map<int*>();
            for (size_t idx = 0; idx < arg.get_size(); idx++)
              bo_mapped[idx] = 0;
          } else {
            bo = xrt::bo(working_dev, arg.get_size(), xrt::bo::flags::host_only, kernel.group_id(arg_idx));
          }

          bo.sync(XCL_BO_SYNC_BO_TO_DEVICE);
          global_args.push_back(bo);
          run.set_arg(arg_idx, bo);
        }
      }
      run_handles.push_back(std::move(run));
    }
  }
  else {
    for (int i=0; i < run_buffer; ++i) {
      auto run = xrt::run(kernel);
      for (const auto& arg : cu.get_args()) {
        auto arg_idx = static_cast<int>(arg.get_index());
        if (arg.get_host_type() == "uint64_t") // opcode
          run.set_arg(arg_idx, static_cast<uint64_t>(XBValidateUtils::get_opcode()));
        else if (arg.get_host_type() == "uint32_t") // nistruct
          run.set_arg(arg_idx, static_cast<uint32_t>(0));
        else if (arg.get_host_type().find('*') != std::string::npos) {
          if (arg.get_name() == "instruct")
            run.set_arg(arg_idx, 0);
          else {
            xrt::bo bo = xrt::ext::bo{working_dev, buffer_size};
            bo.sync(XCL_BO_SYNC_BO_TO_DEVICE);
            global_args.push_back(bo);
            run.set_arg(arg_idx, bo);
          }
        }
      }
      run_handles.push_back(std::move(run));
    }
  }

  //Log
  if (XBU::getVerbose()) {
    XBValidateUtils::logger(ptree, "Details", boost::str(boost::format("Instruction size: %f bytes") % buffer_size));
    XBValidateUtils::logger(ptree, "Details", boost::str(boost::format("No. of iterations: %f") % itr_count_throughput));
  }

  // Run the test to compute throughput where we saturate NPU with jobs and then wait for all
  // completions at the end
  double elapsedSecs = 0.0;

  try {
    auto start = std::chrono::high_resolution_clock::now();

    // enqueue 9 commnds
    for (int i = 0; i < run_buffer; i++)
      run_handles[i%run_buffer].start();

    // wait for each command to finish, then start immediately
    for (int i = 0; i < (itr_count_throughput - run_buffer); i++) {
      run_handles[i%run_buffer].wait2();
      run_handles[i%run_buffer].start();
    }

    // wait for the last 9 commands to finish
    for (int i = 0; i < run_buffer; i++)
      run_handles[i%run_buffer].wait2();

    auto end = std::chrono::high_resolution_clock::now();
    elapsedSecs = std::chrono::duration_cast<std::chrono::duration<double>>(end-start).count();
  }
  catch (const std::exception& ex) {
    XBValidateUtils::logger(ptree, "Error", ex.what());
    ptree.put("status", XBValidateUtils::test_token_failed);
    return ptree;
  }

  // Compute the throughput
  const double throughput = (elapsedSecs != 0.0) ? itr_count_throughput / elapsedSecs : 0.0;

  XBValidateUtils::logger(ptree, "Details", boost::str(boost::format("Average throughput: %.1f ops") % throughput));
  ptree.put("status", XBValidateUtils::test_token_passed);
  return ptree;
}
