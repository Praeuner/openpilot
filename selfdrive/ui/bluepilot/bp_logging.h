#pragma once

#include <iostream>
#include <string>
#include <sstream>

/**
 * @brief BluePilot logging utility class for filtered debug output
 *
 * This class provides logging functions that can be filtered based on environment variables
 * like BP_DEBUG_VIDEO, BP_DEBUG_ROUTES, and BP_DEBUG. It replaces qDebug() calls
 * with more controlled logging using iostream.
 *
 * Usage:
 *   BPLog::bpDebug() << "[bp.routes.panel] Device is onroad - showing safety message";
 *   BPLog::bpDebugVideo() << "[bp.video.controller] init " << routes_dir;
 *   BPLog::bpError() << "[bp.video] Failed to load segment " << segment_idx;
 */
class BPLog {
public:
    /**
     * @brief Info logging function - always shows output
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpInfo();

    /**
     * @brief Debug logging function - always shows output
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpDebug();

    /**
     * @brief Warning logging function - always shows output
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpWarn();

    /**
     * @brief Error logging function - always shows output
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpError();

    /**
     * @brief Video-specific debug logging function
     * Only shows output if BP_DEBUG_VIDEO environment variable is set
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpDebugVideo();

    /**
     * @brief Routes-specific debug logging function
     * Only shows output if BP_DEBUG_ROUTES environment variable is set
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpDebugRoutes();

    /**
     * @brief Onroad-specific debug logging function
     * Only shows output if BP_DEBUG_ONROAD environment variable is set
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpDebugOnroadDebug();

    /**
     * @brief General debug logging function
     * Only shows output if BP_DEBUG environment variable is set
     * @return Reference to ostream for chaining
     */
    static std::ostream& bpDebugGeneral();

private:
    /**
     * @brief Check if an environment variable is set
     * @param var_name Environment variable name
     * @return true if variable is set and not empty
     */
    static bool isEnvVarSet(const std::string& var_name);
};
