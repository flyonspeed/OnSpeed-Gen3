// VVLink.h — included by lib/GaugeWidgets/GaugeWidgets.h on the !DEVICE_M5
// path (we use -DDEVICE_HUVVER for [env:huvver-avi]). In the upstream V.R.
// Little distribution this header pulls in TFT_eSPI plus button/serial
// helpers; for our compile-feasibility prototype we just need it to bring
// in TFT_eSPI types and the M5-API stubs so GaugeWidgets.cpp compiles
// when it sees `extern M5Canvas gdraw`.

#pragma once

#include "HuvverShim.h"
