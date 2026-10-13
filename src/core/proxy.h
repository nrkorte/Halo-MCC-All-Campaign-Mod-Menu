#pragma once

// We ship as VERSION.dll so MCC loads us automatically. Every export
// forwards to the real System32\version.dll.
namespace Proxy {
    bool Load();
}
