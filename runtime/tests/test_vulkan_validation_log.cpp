// SPDX-License-Identifier: GPL-2.0-or-later
#include <cstdio>
#include "sdl_window.h"
#include "video_core/renderer_vulkan/vk_platform.h"

// No window or game is created. Exercise the renderer's real Vulkan messenger
// and log sink, rather than duplicating its severity/filter implementation.
int main() {
    auto instance = Vulkan::CreateInstance(Frontend::WindowSystemType::Headless, true, false);
    auto messenger = Vulkan::CreateDebugCallback(*instance);
    const vk::DebugUtilsMessengerCallbackDataEXT warning{
        .pMessageIdName = "Lian-Validation-Warning-Probe",
        .pMessage = "Diagnostic warning must reach the default GPU log.",
    };
    const vk::DebugUtilsMessengerCallbackDataEXT error{
        .pMessageIdName = "Lian-Validation-Error-Probe",
        .pMessage = "Diagnostic error must reach the default GPU log.",
    };
    instance->submitDebugUtilsMessageEXT(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation, warning);
    instance->submitDebugUtilsMessageEXT(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation, error);
    std::puts("Validation messenger probes submitted; verify both markers in stderr.");
}
