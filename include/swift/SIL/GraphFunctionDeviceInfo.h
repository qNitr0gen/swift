//===- GraphFunctionDeviceInfo.h - Utils for setting op devices *- C++ -*-===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities for assigning ops to devices.
//
//===----------------------------------------------------------------------===//

#ifndef SWIFT_SIL_GRAPHFUNCTIONDEVICEINFO_H
#define SWIFT_SIL_GRAPHFUNCTIONDEVICEINFO_H

#include "swift/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace swift {
class ASTContext;
class SILFunction;
struct GraphOperationAttribute;
class GraphOperationInst;
class SILInstruction;

namespace tf {
class GraphOperationBuilder;
struct GraphOperationInfo;

/// The device type of a tfop instruction (and its output tensors, if any).
enum class DeviceType {
  INVALID,
  CPU,
  GPU,
  TPU,
  /// Indicates this instruction should run on all devices in
  /// `GraphFunctionDeviceInfo::usedDeviceIds`. For example, a promoted
  /// scalar will run on all such devices, in case it is a loop iteration count
  /// and the loop runs on all devices.
  ALL,
  /// Device placement is done at runtime.
  RUNTIME,
};

/// The device id represents a local device, with two components: device type
/// and index. An example is "GPU:3".
// TODO: consider adding a worker (task) component to support remote devices.
struct DeviceId {
  DeviceType type;
  unsigned index;

  bool operator==(const DeviceId &rhs) const {
    return type == rhs.type && index == rhs.index;
  }

  bool operator!=(const DeviceId &rhs) const { return !(*this == rhs); }

  // Used for containers like SmallSet.
  bool operator<(const DeviceId &rhs) const {
    return (unsigned)type < (unsigned)rhs.type ||
           (type == rhs.type && index == rhs.index);
  }

  bool isValid() const { return type != DeviceType::INVALID; }

  void print(llvm::raw_ostream &OS) const;

  void dump() const { print(llvm::errs()); }
};

/// The returned string can be used to construct SIL function names.
/// e.g. "tmp3_main.tf_13_CPU.device_partition", where 13 is the device
/// index. The compiler runtime relies on the string suffix pattern
/// "13_CPU.device_partition" to decode the device index 13, and use it to place
/// the graph function invocation on a TF device like
/// "/job:localhost/replica:0/task:0/device:CPU:13".
static inline std::string getDeviceShortName(DeviceId deviceId) {
  switch (deviceId.type) {
  case DeviceType::CPU:
    return llvm::utostr(deviceId.index) + "_CPU";
  case DeviceType::GPU:
    return llvm::utostr(deviceId.index) + "_GPU";
  case DeviceType::TPU:
    return llvm::utostr(deviceId.index) + "_TPU";
  case DeviceType::ALL:
    return "ALL";
  case DeviceType::RUNTIME:
    return "RUNTIME";
  case DeviceType::INVALID:
    llvm_unreachable("Unsupported device type");
  }
}

inline void DeviceId::print(llvm::raw_ostream &OS) const {
  // This may not be the best format for human reading, but it's convenient.
  OS << getDeviceShortName(*this);
}

inline raw_ostream &operator<<(raw_ostream &OS, DeviceId deviceId) {
  deviceId.print(OS);
  return OS;
}

} // end namespace tf
} // namespace swift

// Support for using DeviceId as a key type for DenseMap.
namespace llvm {
using swift::tf::DeviceId;
using swift::tf::DeviceType;

template <> struct DenseMapInfo<DeviceId> {
  static DeviceId getEmptyKey() {
    return {DeviceType::INVALID, (unsigned)-2};
  }
  static DeviceId getTombstoneKey() {
    return {DeviceType::INVALID, (unsigned)-1};
  }
  static unsigned getHashValue(DeviceId Val) {
    // Map between a DeviceId object and a 64-bit int. The latter is used when
    // we use DeviceId as the key type of a set/map container. We use 32 bits to
    // encode `type`, which is wasteful.
    uint64_t code = ((uint64_t)Val.type << 32) | Val.index;
    return DenseMapInfo<uint64_t>::getHashValue(code);
  }
  static bool isEqual(DeviceId LHS, DeviceId RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

namespace swift {
namespace tf {

static const char TF_CPU_DEVICE_STRING_PREFIX[] =
    "/job:localhost/replica:0/task:0/device:CPU:";
static const char TF_GPU_DEVICE_STRING_PREFIX[] =
    "/job:localhost/replica:0/task:0/device:GPU:";
// TODO: support multiple TPU cores.
static const char TF_DEFAULT_TPU_DEVICE[] = "TPU_SYSTEM";
// This is a pseudo-device that only exist in the SIL code generated by
// TFPartition and GraphPartitioner, and will be replaced with real devices in
// TFGraphLowering.
static const char TF_ALL_DEVICES[] = "ALL_DEVICES";

static DeviceId AllDeviceId = DeviceId{DeviceType::ALL, (unsigned)-1};
static DeviceId RuntimeDeviceId = DeviceId{DeviceType::RUNTIME, 0};
static DeviceId InvalidDeviceId = DeviceId{DeviceType::INVALID, 0};

static inline DeviceType _getOpDeviceType(llvm::StringRef device) {
  if (device.startswith(TF_CPU_DEVICE_STRING_PREFIX))
    return DeviceType::CPU;
  if (device.startswith(TF_GPU_DEVICE_STRING_PREFIX))
    return DeviceType::GPU;
  if (device.str() == TF_DEFAULT_TPU_DEVICE)
    return DeviceType::TPU;
  if (device.str() == TF_ALL_DEVICES)
    return DeviceType::ALL;

  // FIXME: Consider also supporting variants of the device string, such as
  // "CPU:0".
  llvm_unreachable("Unknown device type");
}

static inline DeviceId getOpDeviceId(llvm::StringRef device) {
  if (device.startswith(TF_CPU_DEVICE_STRING_PREFIX)) {
    auto deviceIndexStr = device.substr(strlen(TF_CPU_DEVICE_STRING_PREFIX));
    unsigned deviceIndex;
    bool ret = llvm::to_integer(deviceIndexStr, deviceIndex);
    assert(ret && "Invalid device string!");
    return {DeviceType::CPU, deviceIndex};
  }

  if (device.startswith(TF_GPU_DEVICE_STRING_PREFIX)) {
    auto deviceIndexStr = device.substr(strlen(TF_GPU_DEVICE_STRING_PREFIX));
    unsigned deviceIndex;
    bool ret = llvm::to_integer(deviceIndexStr, deviceIndex);
    assert(ret && "Invalid device string!");
    return {DeviceType::GPU, deviceIndex};
  }

  return {_getOpDeviceType(device), 0};
}

/// The returned string is compatible with TF device name used in TF graphs.
/// e.g. "/job:localhost/replica:0/task:0/device:GPU:3"
static inline std::string getDeviceString(DeviceId deviceId) {
  switch (deviceId.type) {
  case DeviceType::CPU:
    return TF_CPU_DEVICE_STRING_PREFIX + llvm::utostr(deviceId.index);
  case DeviceType::GPU:
    return TF_GPU_DEVICE_STRING_PREFIX + llvm::utostr(deviceId.index);
  case DeviceType::TPU:
    return TF_DEFAULT_TPU_DEVICE;
  case DeviceType::ALL:
    return TF_ALL_DEVICES;
  case DeviceType::RUNTIME:
    return "";
  case DeviceType::INVALID:
    llvm_unreachable("Unsupported device type");
  }
}

/// This struct holds information about the deviceInfo of the graph we are
/// generating.
struct GraphFunctionDeviceInfo {
  // The primary device that takes function args (if any) and return values.
  // Only defined, and is const after finalizeUsedDevices() is called.
  DeviceId primaryDeviceId;
  const bool isTPUInfeedEnabled;

  using UsedDeviceSet = llvm::SmallSet<DeviceId, 8>;
  const UsedDeviceSet &getUsedDeviceIds() const { return usedDeviceIds; }

  /// Return the deviceInfo for the specified function.
  static GraphFunctionDeviceInfo getForFunction(SILFunction &fn,
                                                bool removeConfigInst);

  /// Whether this is an op that configures the function's device.
  static bool isConfigOp(const GraphOperationInfo &opInfo);

  void markDeviceUsed(DeviceId device) {
    assert(device.type != DeviceType::INVALID);
    if (device.type == DeviceType::ALL ||
        !usedDeviceIds.insert(device).second)
      return;
  }

  // Must be called after caller has scanned all graph_ops in the SIL function
  // being processed, and called markDeviceUsed() on them.
  void finalizeUsedDevices();

  // Choose a device for the graphOpInst under construction and track the chosen
  // device in `usedDeviceIds`.
  //
  // If `opDevice` is already set, respects that device choice. Otherwise,
  // chooses a device based on this deviceInfo and op kernel device
  // availability.
  //
  // `attributes` are a list of GraphOperation level attributes which will
  // eventually be passed to TF_OpSetAttr*. These are used here to determine
  // kernel availability on a device.
  //
  // Returns the chosen device.
  std::string
  handleDevicePlacement(llvm::StringRef opType, llvm::StringRef opDevice,
                        llvm::ArrayRef<GraphOperationAttribute> attributes);

  // Same as above, but adds a "__device" attribute to `opBuilder` instead of
  // returning the device.
  //
  // Caller should avoid adding duplicate device attributes (e.g. calling
  // handleDevicePlacement() multiple times when creating the same graph_op
  // inst). Otherwise SILVerifier will fail on that graph_op inst.
  void
  handleDevicePlacement(llvm::StringRef opType, llvm::StringRef opDevice,
                        ASTContext &ctx, GraphOperationBuilder *opBuilder);

private:
  GraphFunctionDeviceInfo(DeviceId primaryDeviceId, bool isTPUInfeedEnabled);

  // `attributes` are used here to determine kernel availability (primarily
  // dtype constraints).
  DeviceId
  chooseDevice(llvm::StringRef opType,
               llvm::ArrayRef<GraphOperationAttribute> attributes) const;

  // Actual TF devices involved in the tensor computation.

  // Invariants:
  // 1. It cannot contain DeviceType::ALL.
  // 2. It must contain primaryDeviceId after finalizeUsedDevices()
  // 3. When there are >= 2 devices, it cannot contain the RUNTIME device.
  // See finalizeUsedDevices() for more context.
  UsedDeviceSet usedDeviceIds;
};

} // namespace swift
} // end namespace swift

#endif
