
#ifndef __COYPU_SPDLOGGER_H
#define __COYPU_SPDLOGGER_H

#include <string.h>
#include <memory>
#include "spdlog/spdlog.h"

namespace coypu
{
    class SPDLogger
    {
    public:
        SPDLogger(std::shared_ptr<spdlog::logger> logger) : _logger(logger)
        {
        }

        virtual ~SPDLogger()
        {
        }
//	SPDLOG_TRACE(console, "Enabled only #ifdef SPDLOG_TRACE_ON..{} ,{}", 1, 3.23);

        const void perror(int errnum, const char *msg)
        {
            char buf[1024] = {};
            strerror_r(errnum, buf, 1024);
            _logger->error("[{0}] ({1}): {2}", errnum, strerror_r(errnum, buf, 1024), msg);
        }

        template <typename... Args>
        const void debug(const char *msg, Args... args)
        {
            _logger->debug(msg, args...);
        }

        template <typename... Args>
        const void info(const char *msg, Args... args)
        {
            _logger->info(msg, args...);
        }

        template <typename... Args>
        const void warn(const char *msg, Args... args)
        {
            _logger->warn(msg, args...);
        }

        template <typename... Args>
        const void error(const char *msg, Args... args)
        {
            _logger->error(msg, args...);
        }

    private:
        SPDLogger(const SPDLogger &other) = delete;
        SPDLogger &operator=(const SPDLogger &other) = delete;

        std::shared_ptr<spdlog::logger> _logger;
    };
} // namespace coypu

#endif
