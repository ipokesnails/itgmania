#include "HidDevice.h"

#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "RageLog.h"
#include "hidapi.h"

std::vector<int> make_pids(int base_pid, int size) {
  std::vector<int> vec(size);

  for (int i = 0; i < size; i++) {
    vec[i] = base_pid + i;
  }

  return vec;
}

HidDevice::HidDevice(
    int vid, const std::vector<int> pids, int interfaceNum,
    bool autoReconnection, bool nonBlockingRead, int deviceIndex)
    : vid{vid},
      pids{pids},
      interfaceNum{interfaceNum},
      deviceIndex{deviceIndex}, // ipokesnails change
      autoReconnection{autoReconnection},
      nonBlockingRead{nonBlockingRead} {
  bool result = TryConnect();

  if (!result) {
    LOG->Warn(
        "HidDevice with vendor_id 0x%04x and pids 0x%s %d could not connect.",
        vid, GetPidsString(pids).c_str(), interfaceNum);
  } else {
    foundOnce = true;
  }
}

HidDevice::HidDevice(
    int vid, int pid, int interfaceNum, bool autoReconnection,
    bool nonBlockingRead, int deviceIndex) // ipokesnails change
    : HidDevice(
        vid, make_pids(pid, 1), interfaceNum, autoReconnection,
        nonBlockingRead, deviceIndex) {} // ipokesnails change

HidDevice::~HidDevice() {
  Close();
  hid_exit();
}

void HidDevice::Close() {
  hid_close(handle);
  handle = nullptr;

  foundDeviceInfo.vid = 0;
  foundDeviceInfo.pid = 0;
  foundDeviceInfo.interfaceNum = 0;
  foundDeviceInfo.path = nullptr;
}

bool HidDevice::Open(const char* path) {
  handle = hid_open_path(path);

  if (nonBlockingRead) {
    hid_set_nonblocking(handle, 1);
  }

  if (handle) {
    LOG->Info(
        "HidDevice 0x%04x:0x%04x %d opened by path %s", foundDeviceInfo.vid,
        foundDeviceInfo.pid, foundDeviceInfo.interfaceNum,
        foundDeviceInfo.path);
  } else {
    LOG->Warn(
        "HidDevice 0x%04x:0x%04x %d could not be opened by path %s. Are "
        "permissions correct?",
        foundDeviceInfo.vid, foundDeviceInfo.pid, foundDeviceInfo.interfaceNum,
        foundDeviceInfo.path);
  }

  return handle != nullptr;
}

bool HidDevice::TryConnect() {
  GetDeviceInfo(vid, pids, interfaceNum, deviceIndex, &foundDeviceInfo); // ipokesnails change
  
  if (foundDeviceInfo.path == nullptr) {
    return false;
  }

  return Open(foundDeviceInfo.path);
}

bool HidDevice::CheckConnection() {
  if (IsConnected()) {
    return true;
  }

  if (!autoReconnection || !foundOnce) {
    return false;
  }

  return TryConnect();
}

const wchar_t* HidDevice::GetError() { return hid_read_error(handle); }

bool HidDevice::IsConnected() { return handle != nullptr; }

bool HidDevice::FoundOnce() { return foundOnce; }

const std::string HidDevice::GetPidsString(const std::vector<int> pids) {
  std::string pidsString;
  char pid[5] = {0};
  size_t size = pids.size();

  for (size_t i = 0; i < size; ++i) {
    sprintf(pid, "%04X", pids[i]);
    pidsString += pid;

    if (i != size - 1) {
      pidsString += ",";
    }
  }

  return pidsString;
}

void HidDevice::GetDeviceInfo(
    int vid, const std::vector<int> pids, int interfaceNumber,
    int deviceIndex, HidDeviceInfo* device_info) { // ipokesnails change
  bool found{false};
  struct hid_device_info *devs, *cur_dev;
  size_t size = pids.size();

  devs = hid_enumerate(vid, 0);
  cur_dev = devs;

  if (devs && cur_dev) {
    int matchingDeviceIndex = 0; // ipokesnails change

    // Look for the desired device by iterating connected ones.
    while (cur_dev) {
      bool matches = false;

      for (size_t i = 0; i < size; i++) {
        if (cur_dev->vendor_id == vid && cur_dev->product_id == pids[i]) {
          if (interfaceNumber == -1 ||
              cur_dev->interface_number == interfaceNumber) {
            matches = true;
            break;
          }
        }
      }

      if (matches) {
        if (matchingDeviceIndex == deviceIndex) {
          found = true;
          break;
        }

        matchingDeviceIndex++;
      }

      cur_dev = cur_dev->next;
    }
  } else {
    LOG->Warn("HidDevice with vid 0x%04x not found, is it plugged in?", vid);
  }

  if (found && cur_dev != nullptr) {
    device_info->vid = cur_dev->vendor_id;
    device_info->pid = cur_dev->product_id;
    device_info->interfaceNum = cur_dev->interface_number;
    device_info->path = cur_dev->path;
  }
}

int HidDevice::Read(unsigned char* data, size_t length) {
  if (!CheckConnection()) {
    return NotConnected;
  }

  int result = hid_read(handle, data, length);

  if (result == -1) {
    LOG->Warn(
        "HidDevice 0x%04x:0x%04x %d read failed. Fail reason %ls",
        foundDeviceInfo.vid, foundDeviceInfo.pid, foundDeviceInfo.interfaceNum,
        GetError());
    Close();
  }

  return result;
}

HidResults HidDevice::Write(const unsigned char* data, size_t length) {
  if (!CheckConnection()) {
    return NotConnected;
  }

  size_t result = static_cast<size_t>(hid_write(handle, data, length));

  if (result != length) {
    LOG->Warn(
        "HidDevice 0x%04x:0x%04x %d write failed. Fail reason %ls",
        foundDeviceInfo.vid, foundDeviceInfo.pid, foundDeviceInfo.interfaceNum,
        GetError());
    Close();
    return OperationFailed;
  }

  return Success;
}
