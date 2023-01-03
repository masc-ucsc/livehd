//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

using Port_ID                  = uint16_t;  // ports have a set order (a-b != b-a)
constexpr int Port_bits        = std::numeric_limits<Port_ID>::digits - 1;
constexpr Port_ID Port_invalid = ((1<<Port_bits)-1);

using Bits_t               = int32_t;  // bits type (future use)
constexpr int    Bits_bits = 17;
constexpr Bits_t Bits_max  = ((1ULL << Bits_bits) - 1);

