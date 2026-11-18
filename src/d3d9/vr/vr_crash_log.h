#pragma once

#include <cstdint>
#include <string>

namespace dxvk {

  /**
   * \brief Writes fatal exceptions of the game's process to the log
   *
   * A vectored exception handler, called after the handlers already
   * installed, logs an access violation or another fatal exception: its
   * code, the instruction's address with its module and offset, the
   * registers and the return addresses found on the stack, also with
   * their module. It handles nothing: the exception goes on to the game's
   * own handlers and to crash loggers. Players' crash reports then carry
   * where the game stopped even without a crash logger.
   *
   * Exceptions the game catches itself would also be logged, so only the
   * first few are.
   */
  class VrCrashLog {

  public:

    /**
     * \brief Installs the handler, once
     */
    static void install();

    /**
     * \brief Where another thread is: its instruction with its module, and
     *    the return addresses on its stack, for a log line
     *
     * Meant for a thread that stopped making progress: its stack is read
     * after it is resumed.
     */
    static std::string describeThread(uint32_t threadId);

  };

}
