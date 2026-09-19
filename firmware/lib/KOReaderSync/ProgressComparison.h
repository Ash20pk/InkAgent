#pragma once

#include <cstdint>

#include "InkAgentPosition.h"

enum class ProgressComparison : uint8_t { LocalAhead, Synchronized, RemoteAhead, Unknown };

enum class RemoteRecordChoice : uint8_t { Primary, Alternate };

ProgressComparison compareProgress(const InkAgentPosition& local, float localPercentage, const InkAgentPosition& remote,
                                   float remotePercentage);

RemoteRecordChoice selectRemoteRecord(const InkAgentPosition& primary, float primaryPercentage,
                                      const InkAgentPosition& alternate, float alternatePercentage);
